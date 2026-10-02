package org.omachat.android

import android.os.Bundle
import android.util.Base64
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.VoiceSession
import org.junit.Assert.*
import org.junit.Test
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import kotlin.math.*

class VoiceAudioTest {
    private fun tone() = FloatArray(960) { (.3 * sin(2 * PI * 440 * it / 48000)).toFloat() }
    private fun energy(pcm: FloatArray) = pcm.sumOf { (it * it).toDouble() } / pcm.size

    @Test fun opusDesktopReferenceBoundsAndHandleLifetime() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val reference = instrumentation.context.assets.open("desktop-opus-440.bin").use { it.readBytes() }
        VoiceCodec(false).use { decoder ->
            val decoded = decoder.decode(reference)!!
            assertEquals(960, decoded.size)
            assertTrue(energy(decoded) > .005)
            assertEquals(960, decoder.decode(null)!!.size)
            assertNull(decoder.decode(ByteArray(1276)))
            assertNull(decoder.decode(byteArrayOf()))
            assertNull(decoder.decode(byteArrayOf(0))) // 10 ms, rejected before decoder state changes
        }
        VoiceCodec(true).use { encoder ->
            val packet = encoder.encode(tone())
            assertTrue(packet.size in 1..1275)
            instrumentation.sendStatus(0, Bundle().apply { putString("androidOpus", Base64.encodeToString(packet, Base64.NO_WRAP)) })
            VoiceCodec(false).use { decoder -> assertTrue(energy(decoder.decode(packet)!!) > .005) }
        }
        val handle = NativeOpus.create(true)
        assertNull(NativeOpus.encode(handle, FloatArray(959)))
        assertNull(NativeOpus.encode(handle, tone().also { it[1] = Float.NaN }))
        assertNull(NativeOpus.encode(handle, tone().also { it[1] = 2f }))
        assertNull(NativeOpus.decode(handle, reference)) // encoder cannot decode
        NativeOpus.destroy(handle); NativeOpus.destroy(handle)
        assertNull(NativeOpus.encode(handle, tone())) // stale ID cannot dereference freed memory
        assertNull(NativeOpus.decode(Long.MAX_VALUE, null))
        val handles = List(256) { NativeOpus.create(false).also { assertNotEquals(0L, it) } }
        try { assertEquals(0L, NativeOpus.create(false)) }
        finally { handles.forEach { NativeOpus.destroy(it) } }
    }

    @Test fun mixerConcealsActiveLossClipsAndReleasesIdleSpeakers() {
        VoiceCodec(true).use { encoder ->
            val opus = encoder.encode(tone())
            VoiceMixer().use { mixer ->
                val ticks = List(64) { VoiceTick(it + 1, VoicePacket(it + 1, 10, 1, 0, false, opus)) }
                val mixed = mixer.mix(ticks)
                assertEquals(64, mixer.size)
                assertTrue(mixed.all { it.isFinite() && abs(it) <= 1 })
                assertTrue(mixed.any { abs(it) == 1f })
                val concealed = mixer.mix(listOf(VoiceTick(1, null)))
                assertEquals(1, mixer.size)
                assertTrue(energy(concealed) > 0)
                assertTrue(mixer.mix(emptyList()).all { it == 0f })
                assertEquals(0, mixer.size)
                try { mixer.mix(List(65) { VoiceTick(it, null) }); fail("Unbounded decoders") }
                catch (_: IllegalArgumentException) { }
            }
        }
    }

    private suspend fun registered(block: suspend (VoiceMedia, DatagramSocket, ByteArray) -> Unit) {
        val udp = DatagramSocket(0, InetAddress.getLoopbackAddress()).apply { soTimeout = 3000 }
        val key = ByteArray(32).also { java.security.SecureRandom().nextBytes(it) }
        val lease = VoiceSession.newBuilder().setChannelId(1).setStreamId((1..Int.MAX_VALUE).random())
            .setMediaKey(ByteString.copyFrom(key)).setUdpPort(udp.localPort)
            .setExpiresAt(System.currentTimeMillis() + 30000).build()
        val media = VoiceMedia.open(InetAddress.getLoopbackAddress(), lease)
        try {
            val packet = DatagramPacket(ByteArray(1400), 1400)
            withContext(Dispatchers.IO) { udp.receive(packet) }
            val header = ByteBuffer.allocate(24).put(1).put(3).put(0).put(0).putInt(lease.streamId)
                .putLong(0).putInt(1).putInt(0).array()
            val ack = NativeMedia.seal(key, header, byteArrayOf(3), 2)!!
            withContext(Dispatchers.IO) { udp.send(DatagramPacket(ack, ack.size, packet.socketAddress)) }
            withTimeout(3000) { media.registered.first { it } }
            udp.connect(packet.socketAddress)
            block(media, udp, key)
        } finally { media.stop(); udp.close(); key.fill(0) }
    }

    @Test fun audioPartialIoFailureAndImmediateStopReleaseResources(): Unit = runBlocking {
        class Device(val failRead: Boolean = false) : VoiceAudioDevice {
            @Volatile var closed = false
            var reads = 0
            var writes = 0
            @Volatile var heard = false
            override fun start() { }
            override fun read(samples: FloatArray, offset: Int, count: Int): Int {
                reads++
                if (failRead) return AudioError
                if (reads % 3 == 0) return 0
                val n = minOf(count, 137)
                tone().copyInto(samples, offset, offset, offset + n)
                return n
            }
            override fun write(samples: FloatArray, offset: Int, count: Int): Int {
                writes++
                if (samples.any { abs(it) > .01f }) heard = true
                return if (writes % 3 == 0) 0 else minOf(count, 157)
            }
            override fun close() { closed = true }
        }
        registered { media, udp, key ->
            val device = Device()
            val audio = VoiceAudio.start(media, device)
            try {
                val packet = DatagramPacket(ByteArray(1400), 1400)
                withContext(Dispatchers.IO) { udp.receive(packet) }
                val opus = NativeMedia.open(key, packet.data.copyOf(packet.length), 1)!!
                VoiceCodec(false).use { assertTrue(energy(it.decode(opus)!!) > .005) }
                assertTrue(device.reads > 1)
                val reference = InstrumentationRegistry.getInstrumentation().context.assets
                    .open("desktop-opus-440.bin").use { it.readBytes() }
                for (index in 0..2) {
                    val header = ByteBuffer.allocate(24).put(1).put(1).put(0).put(0).putInt(17)
                        .putLong(42).putInt(index + 1).putInt(index * 960).array()
                    val remote = NativeMedia.seal(key, header, reference, 2)!!
                    withContext(Dispatchers.IO) { udp.send(DatagramPacket(remote, remote.size)) }
                }
                withTimeout(3000) { while (!device.heard) delay(10) }
                withTimeout(2000) { while (device.writes < 2) delay(10) }
            } finally { audio.stop() }
            assertTrue(device.closed); assertTrue(media.ended.value)
        }
        registered { media, _, _ ->
            val device = Device(true)
            val audio = VoiceAudio.start(media, device)
            withTimeout(3000) { audio.failure.first { it != null } }
            audio.stop()
            assertTrue(device.closed); assertTrue(media.ended.value)
        }
        registered { media, _, _ ->
            val device = Device()
            val audio = VoiceAudio.start(media, device)
            audio.stop() // cancellation before scheduling must still release the supplied device
            assertTrue(device.closed)
        }
    }

    @Test fun emulatorMicrophoneAndTrackStartAndStop(): Unit = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        instrumentation.uiAutomation.executeShellCommand("pm grant ${context.packageName} android.permission.RECORD_AUDIO")
            .use { android.os.ParcelFileDescriptor.AutoCloseInputStream(it).readBytes() }
        ActivityScenario.launch(MainActivity::class.java).use {
            registered { media, udp, key ->
                val audio = VoiceAudio.start(media, AndroidVoiceDevice.open(context))
                try {
                    val packet = DatagramPacket(ByteArray(1400), 1400)
                    withContext(Dispatchers.IO) { udp.receive(packet) }
                    val opus = NativeMedia.open(key, packet.data.copyOf(packet.length), 1)!!
                    VoiceCodec(false).use { decoder -> assertEquals(960, decoder.decode(opus)!!.size) }
                    assertNull(audio.failure.value)
                } finally { audio.stop() }
                assertTrue(media.ended.value)
            }
        }
    }
    companion object { private const val AudioError = -6 }
}
