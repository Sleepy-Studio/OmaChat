package org.omachat.android

import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import androidx.test.platform.app.InstrumentationRegistry
import org.omachat.protocol.ControlConnection
import org.omachat.protocol.ServerError
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.security.MessageDigest

class TransferTest {
    private val context get() = ApplicationProvider.getApplicationContext<OmaChatApp>()
    private fun stagingEmpty() = assertTrue(File(context.cacheDir, "transfers").listFiles().orEmpty().isEmpty())
    private fun attachment(bytes: ByteArray) = Attachment.newBuilder().setId(42).setSize(bytes.size.toLong())
        .setSha256(ByteString.copyFrom(MessageDigest.getInstance("SHA-256").digest(bytes))).build()
    private fun serve(bytes: ByteArray): suspend (Envelope.Builder) -> Envelope = { request ->
        val offset = request.download.offset.toInt()
        val end = minOf(bytes.size, offset + request.download.length)
        Envelope.newBuilder().setFileChunk(FileChunk.newBuilder().setAttachmentId(42).setOffset(offset.toLong())
            .setTotalSize(bytes.size.toLong()).setData(ByteString.copyFrom(bytes, offset, end - offset))).build()
    }

    @Test fun cancellationReleasesTicketAndBoundsWorkers(): Unit = runBlocking {
        val input = File(context.cacheDir, "cancel-upload.bin").apply { writeBytes(ByteArray(150000) { it.toByte() }) }
        val transfers = Transfers()
        val arrived = CompletableDeferred<Unit>()
        var cancelled = false
        var finished = false
        val attachments = Attachments(context)
        val job = launch {
            transfers.run(7, "Upload") { progress ->
                attachments.upload(Uri.fromFile(input), Channel.newBuilder().setId(7).build(), { request ->
                    when {
                        request.hasBeginUpload() -> Envelope.newBuilder().setUploadTicket(UploadTicket.newBuilder()
                            .setAttachmentId(42).setChunkSize(65536)).build()
                        request.hasUploadChunk() -> { arrived.complete(Unit); awaitCancellation() }
                        request.hasCancelUpload() -> { cancelled = true; Envelope.getDefaultInstance() }
                        request.hasFinishUpload() -> { finished = true; error("Must not finish cancelled transfer") }
                        else -> error("Unexpected request")
                    }
                }, progress)
            }
        }
        try {
            withTimeout(5000) { arrived.await() }
            assertEquals("Uploading", transfers.state.value!!.stage)
            try { transfers.run(8, "Download") { error("Must not start second worker") }; fail("Worker limit ignored") }
            catch (expected: IllegalStateException) { assertTrue(expected.message!!.contains("current file transfer")) }
            transfers.cancelAndJoin()
            assertTrue(job.isCancelled)
            assertTrue(cancelled)
            assertFalse(finished)
            assertEquals("Cancelled", transfers.state.value!!.stage)
            stagingEmpty()
            transfers.run(8, "Download") { progress -> progress("Downloading", 4, 4); Unit }
            assertEquals("Complete", transfers.state.value!!.stage)
        } finally { job.cancelAndJoin(); input.delete() }
    }

    @Test fun authenticatedExportFailureAndCancellationCleanStaging(): Unit = runBlocking {
        val bytes = ByteArray(150000) { (it * 3).toByte() }
        var opened = 0
        var closed = false
        var written = 0
        val attachments = Attachments(context) {
            opened++
            object : OutputStream() {
                override fun write(value: Int) { error("Use bounded buffers") }
                override fun write(buffer: ByteArray, offset: Int, length: Int) {
                    if (written > 0) throw IOException("Provider ran out of storage")
                    written += length
                }
                override fun close() { closed = true }
            }
        }
        val bad = attachment(bytes).toBuilder().setSha256(ByteString.copyFrom(ByteArray(32))).build()
        try { attachments.download(bad, null, Uri.EMPTY, serve(bytes)); fail("Checksum accepted") }
        catch (expected: IllegalStateException) { assertTrue(expected.message!!.contains("checksum")) }
        assertEquals(0, opened)
        stagingEmpty()
        try { attachments.download(attachment(bytes), null, Uri.EMPTY, serve(bytes)); fail("Provider failure ignored") }
        catch (expected: IOException) { assertTrue(expected.message!!.contains("storage")) }
        assertEquals(1, opened)
        assertEquals(65536, written)
        assertTrue(closed)
        stagingEmpty()
        val job = launch {
            val ownJob = currentCoroutineContext().job
            attachments.download(attachment(bytes), null, Uri.EMPTY, serve(bytes)) { stage, _, _ ->
                if (stage == "Saving file") ownJob.cancel()
            }
        }
        job.join()
        assertTrue(job.isCancelled)
        assertEquals(1, opened) // cancellation before export never opens/truncates destination
        stagingEmpty()
    }

    @Test fun encryptedAuthenticationPrecedesDestinationAndProviderOpenFailureCleans(): Unit = runBlocking {
        val source = File(context.cacheDir, "export-source.bin").apply { writeBytes(ByteArray(65536) { it.toByte() }) }
        val encrypted = File(context.cacheDir, "export-cipher.bin")
        val key = NativeCrypto.encryptFile(source.path, encrypted.path)!!
        try {
            val bytes = encrypted.readBytes()
            val metadata = E2EFile.newBuilder().setAttachmentId(42).setSize(source.length())
                .setKey(ByteString.copyFrom(ByteArray(32))).build()
            var opened = 0
            val attachments = Attachments(context) { opened++; ByteArrayOutputStream() }
            try { attachments.download(attachment(bytes), metadata, Uri.EMPTY, serve(bytes)); fail("Wrong key accepted") }
            catch (expected: IllegalStateException) { assertTrue(expected.message!!.contains("authentication")) }
            assertEquals(0, opened)
            stagingEmpty()
            val realProvider = Attachments(context)
            try {
                realProvider.download(attachment(bytes), metadata.toBuilder().setKey(ByteString.copyFrom(key)).build(),
                    Uri.parse("content://org.omachat.missing.documents/document/test"), serve(bytes))
                fail("Missing document provider accepted")
            } catch (_: java.io.FileNotFoundException) { }
            stagingEmpty()
        } finally { key.fill(0); source.delete(); encrypted.delete() }
    }


    @Test fun realServerCancellationRemovesAcknowledgedUpload(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val socketScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val socket = ControlConnection.connect(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(),
            args.getString("serverFingerprint")!!, socketScope)
        val input = File(context.cacheDir, "real-cancel-upload.bin").apply { writeBytes(ByteArray(700000) { it.toByte() }) }
        var ticket = 0L
        var acknowledged = 0L
        val attachments = Attachments(context)
        try {
            socket.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            socket.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder().setUsername("androidfixture").setPassword("fixture-password-123")))
            val channel = socket.synchronize().syncState.channelsList.single { it.name == "general" && it.type == ChannelType.CHANNEL_TYPE_TEXT }
            val job = launch {
                val ownJob = currentCoroutineContext().job
                attachments.upload(Uri.fromFile(input), channel, { request ->
                    socket.call(request).also { if (it.hasUploadTicket()) ticket = it.uploadTicket.attachmentId }
                }) { stage, bytes, _ ->
                    if (stage == "Uploading" && bytes > 0) { acknowledged = bytes; ownJob.cancel() }
                }
            }
            withTimeout(15000) { job.join() }
            assertTrue(job.isCancelled)
            assertTrue(ticket != 0L)
            assertEquals(524288L, acknowledged)
            try {
                socket.call(Envelope.newBuilder().setResumeUpload(ResumeUploadRequest.newBuilder().setAttachmentId(ticket)))
                fail("Cancelled server upload still exists")
            } catch (expected: ServerError) { assertEquals(ErrorCode.ERROR_NOT_FOUND_VALUE, expected.code) }
            stagingEmpty()
        } finally { input.delete(); socket.close(); socketScope.cancel() }
    }

    @Test fun copyHonorsLimitAndCancellationBetweenChunks(): Unit = runBlocking {
        val output = ByteArrayOutputStream()
        try { copyTransfer(ByteArrayInputStream(ByteArray(65537)), output, 65536) {}; fail("Limit ignored") }
        catch (_: IllegalStateException) { }
        assertEquals(65536, output.size())
        val cancelledOutput = ByteArrayOutputStream()
        val job = launch {
            val ownJob = currentCoroutineContext().job
            copyTransfer(ByteArrayInputStream(ByteArray(150000)), cancelledOutput, MAX_ATTACHMENT_BYTES) {
                ownJob.cancel()
            }
        }
        job.join()
        assertTrue(job.isCancelled)
        assertEquals(65536, cancelledOutput.size())
    }
}
