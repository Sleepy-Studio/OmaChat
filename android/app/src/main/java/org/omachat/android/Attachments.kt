package org.omachat.android

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import omachat.proto.Network.*
import java.io.File
import java.security.MessageDigest

internal const val MAX_ATTACHMENT_BYTES = 100L * 1024 * 1024
/** App-private staging only; no whole-file buffers and no untrusted filename paths. */
internal class Attachments(
    private val context: Context,
    private val openDestination: (Uri) -> java.io.OutputStream? = { context.contentResolver.openOutputStream(it, "wt") }
) {
    private val staging = File(context.cacheDir, "transfers").apply { mkdirs() }
    init { staging.listFiles()?.filter { it.isFile }?.forEach { it.delete() } }
    suspend fun upload(uri: Uri, channel: Channel, request: suspend (Envelope.Builder) -> Envelope, progress: (String, Long, Long?) -> Unit = { _, _, _ -> }): Pair<Attachment, E2EFile?> = withContext(Dispatchers.IO) {
        val plain = File.createTempFile("upload-", ".tmp", staging)
        val sealed = File.createTempFile("encrypted-", ".tmp", staging)
        var ticket: UploadTicket? = null
        var key: ByteArray? = null
        try {
            var name = "attachment"
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) name = it.getString(0) ?: name
            }
            name = name.substringAfterLast('/').substringAfterLast('\\').take(200).ifBlank { "attachment" }
            val mime = context.contentResolver.getType(uri) ?: "application/octet-stream"
            context.contentResolver.openInputStream(uri)?.use { input -> plain.outputStream().use { output ->
                progress("Reading file", 0, null)
                copyTransfer(input, output, MAX_ATTACHMENT_BYTES) { progress("Reading file", it, null) }
            } } ?: error("Cannot read the selected file")
            val private = channel.type == ChannelType.CHANNEL_TYPE_DM || channel.type == ChannelType.CHANNEL_TYPE_GROUP_DM
            val file = if (private) {
                currentCoroutineContext().ensureActive()
                progress("Encrypting file", 0, null)
                key = NativeCrypto.encryptFile(plain.path, sealed.path) ?: error("Attachment encryption failed")
                sealed
            } else plain
            currentCoroutineContext().ensureActive()
            check(file.length() > 0) { "Empty attachments are unsupported by this server" }
            val reply = request(Envelope.newBuilder().setBeginUpload(BeginUploadRequest.newBuilder()
                .setChannelId(channel.id).setFilename(if (private) "file.enc" else name)
                .setMimeType(if (private) "application/octet-stream" else mime).setSize(file.length())))
            check(reply.hasUploadTicket())
            ticket = reply.uploadTicket
            check(ticket.chunkSize in 1..524288 && ticket.received == 0L) { "Invalid upload ticket" }
            val digest = MessageDigest.getInstance("SHA-256")
            progress("Uploading", 0, file.length())
            file.inputStream().use { input ->
                val buffer = ByteArray(ticket.chunkSize); var offset = 0L
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer); if (count < 0) break
                    digest.update(buffer, 0, count)
                    request(Envelope.newBuilder().setUploadChunk(UploadChunkRequest.newBuilder()
                        .setAttachmentId(ticket.attachmentId).setOffset(offset).setData(ByteString.copyFrom(buffer, 0, count))))
                    offset += count
                    progress("Uploading", offset, file.length())
                }
            }
            progress("Finishing upload", file.length(), file.length())
            val finished = request(Envelope.newBuilder().setFinishUpload(FinishUploadRequest.newBuilder()
                .setAttachmentId(ticket.attachmentId).setSha256(ByteString.copyFrom(digest.digest()))))
            check(finished.hasAttachment())
            finished.attachment to key?.let { E2EFile.newBuilder().setAttachmentId(ticket.attachmentId)
                .setFilename(name).setMimeType(mime).setSize(plain.length()).setKey(ByteString.copyFrom(it)).build() }
        } catch (e: Exception) {
            // Cancellation must still release a known server ticket. Bound cleanup;
            // an unknown/lost begin reply is reclaimed by server upload expiry.
            withContext(NonCancellable) {
                withTimeoutOrNull(3000) {
                    ticket?.let { runCatching { request(Envelope.newBuilder().setCancelUpload(CancelUploadRequest.newBuilder().setAttachmentId(it.attachmentId))) } }
                }
            }
            throw e
        } finally { key?.fill(0); plain.delete(); sealed.delete() }
    }
    suspend fun download(attachment: Attachment, metadata: E2EFile?, destination: Uri, request: suspend (Envelope.Builder) -> Envelope, progress: (String, Long, Long?) -> Unit = { _, _, _ -> }) = withContext(Dispatchers.IO) {
        val cipher = File.createTempFile("download-", ".tmp", staging)
        val plain = File.createTempFile("decrypted-", ".tmp", staging)
        try {
            check(attachment.size in 1..(MAX_ATTACHMENT_BYTES + if (metadata != null) 32768 else 0)) { "Attachment exceeds mobile limit" }
            if (metadata != null) check(metadata.attachmentId == attachment.id && metadata.size in 0..MAX_ATTACHMENT_BYTES) { "Invalid authenticated attachment metadata" }
            val digest = MessageDigest.getInstance("SHA-256")
            progress("Downloading", 0, attachment.size)
            cipher.outputStream().use { output ->
                var offset = 0L
                while (offset < attachment.size) {
                    currentCoroutineContext().ensureActive()
                    val reply = request(Envelope.newBuilder().setDownload(DownloadRequest.newBuilder()
                        .setAttachmentId(attachment.id).setOffset(offset).setLength(524288)))
                    check(reply.hasFileChunk())
                    val chunk = reply.fileChunk
                    check(chunk.attachmentId == attachment.id && chunk.offset == offset && chunk.totalSize == attachment.size
                        && chunk.data.size() in 1..524288 && chunk.data.size() <= attachment.size - offset) { "Invalid file chunk" }
                    val bytes = chunk.data.toByteArray(); digest.update(bytes); output.write(bytes); offset += bytes.size
                    progress("Downloading", offset, attachment.size)
                }
            }
            check(attachment.sha256.isEmpty || MessageDigest.isEqual(attachment.sha256.toByteArray(), digest.digest())) { "Attachment checksum mismatch" }
            val file = if (metadata != null) {
                progress("Authenticating file", 0, null)
                currentCoroutineContext().ensureActive()
                val key = metadata.key.toByteArray()
                try { check(NativeCrypto.decryptFile(cipher.path, plain.path, key)) { "Encrypted attachment failed authentication" } }
                finally { key.fill(0) }
                check(plain.length() == metadata.size) { "Attachment size mismatch" }
                plain
            } else cipher
            // Nothing reaches the document provider until complete authentication.
            currentCoroutineContext().ensureActive()
            progress("Saving file", 0, file.length())
            currentCoroutineContext().ensureActive()
            openDestination(destination)?.use { output -> file.inputStream().use {
                copyTransfer(it, output, MAX_ATTACHMENT_BYTES) { bytes -> progress("Saving file", bytes, file.length()) }
            } }
                ?: error("Cannot save to the selected location")
        } finally { cipher.delete(); plain.delete() }
    }
}
