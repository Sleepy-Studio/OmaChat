package org.omachat.android

import androidx.test.platform.app.InstrumentationRegistry
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.ControlConnection
import org.omachat.protocol.ServerError
import java.net.InetAddress
import java.net.DatagramSocket
import java.net.DatagramPacket
import java.nio.ByteBuffer

class VoiceMediaTest {
    @Test fun sharedNativePacketVectorAndAuthenticationBounds() {
        val key = ByteArray(32) { it.toByte() }
        val header = ByteBuffer.allocate(24).put(1).put(1).put(1).put(0)
            .putInt(0x10203040).putLong(0).putInt(1).putInt(960).array()
        // Independently serialized Python struct + host libsodium AEAD vector.
        val expected = "0101010010203040000000000000000000000001000003c0a47e6c8d30eeb5c8ebb14d872d8b46be9570205bbfb64a787869e38c"
            .chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        assertArrayEquals(expected, NativeMedia.seal(key, header, "voice-vector".toByteArray(), 1))
        assertArrayEquals("voice-vector".toByteArray(), NativeMedia.open(key, expected, 1))
        assertNull(NativeMedia.open(key, expected, 2))
        for (index in expected.indices) {
            val changed = expected.copyOf().also { it[index] = (it[index].toInt() xor 1).toByte() }
            assertNull("accepted changed byte $index", NativeMedia.open(key, changed, 1))
        }
        assertNull(NativeMedia.open(ByteArray(31), expected, 1))
        assertNull(NativeMedia.open(key, ByteArray(1401), 1))
        assertNull(NativeMedia.open(key, ByteArray(39), 1))
        assertNull(NativeMedia.seal(key, header, ByteArray(1361), 1))
        assertNull(NativeMedia.seal(key, header.copyOf(25), byteArrayOf(1), 1))
        assertNull(NativeMedia.seal(key, header, byteArrayOf(1), 0))
        assertNotNull(NativeMedia.seal(key, header, ByteArray(1360), 1))
        key.fill(0)
    }

    @Test fun replayAndJitterRemainBoundedAcrossReorderingLossAndStalls() {
        val counter = VoiceSequence(0xfffffffeL)
        assertEquals(0xffffffffL, counter.next())
        repeat(2) {
            try { counter.next(); fail("Nonce counter wrapped") } catch (_: IllegalStateException) { }
        }
        val replay = VoiceReplay()
        assertTrue(replay.accept(100)); assertTrue(replay.accept(99)); assertFalse(replay.accept(99))
        assertTrue(replay.accept(164)); assertFalse(replay.accept(100)); assertTrue(replay.accept(101))
        assertTrue(replay.accept(0xffffffffL)); assertFalse(replay.accept(0))
        val jitter = VoiceJitter()
        fun packet(timestamp: Long) = VoicePacket(1, 2, timestamp / 960, timestamp, false, byteArrayOf(7))
        jitter.offer(packet(960), 0); jitter.offer(packet(0), 1); jitter.offer(packet(2880), 2)
        assertEquals(0L, jitter.poll(3)?.timestamp)
        assertEquals(960L, jitter.poll(23)?.timestamp)
        assertNull(jitter.poll(43)) // lost 1920; never wait indefinitely
        assertEquals(2880L, jitter.poll(63)?.timestamp)
        jitter.offer(packet(0), 64); assertEquals(0, jitter.size) // late frame cannot rewind
        for (index in 1..200) jitter.offer(packet(10000L + index * 960), 70)
        assertEquals(64, jitter.size)
        assertNotNull(jitter.poll(80)); assertTrue(jitter.size <= 3)
        assertNull(jitter.poll(400)); assertEquals(0, jitter.size)
        // One packet starts after target time, never requiring a full queue.
        jitter.offer(packet(0), 500); assertNull(jitter.poll(559)); assertNotNull(jitter.poll(560))
    }

    @Test fun isolatedRelayRegistersAuthenticatesAndTransfersOwnership(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val host = args.getString("serverHost")!!
        val port = args.getString("serverPort")!!.toInt()
        val pin = args.getString("serverFingerprint")!!
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val connections = mutableListOf<ControlConnection>()
        val media = mutableListOf<VoiceMedia>()
        suspend fun connect(): ControlConnection = ControlConnection.connect(host, port, pin, scope).also {
            connections += it
            val hello = it.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            assertTrue("voice.ownership.v1" in hello.helloReply.capabilitiesList)
        }
        suspend fun open(control: ControlConnection, channel: Long, transfer: Boolean = false): Pair<VoiceSession, VoiceMedia> {
            val reply = control.call(Envelope.newBuilder().setJoinVoice(JoinVoiceRequest.newBuilder().setChannelId(channel).setTransfer(transfer)))
            assertTrue(reply.hasVoiceSession())
            val transport = VoiceMedia.open(InetAddress.getByName(host), reply.voiceSession)
            media += transport
            withTimeout(12000) { transport.registered.first { it } }
            return reply.voiceSession to transport
        }
        try {
            val first = connect()
            val firstLogin = first.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername("voicefixture").setPassword("fixture-voice-password-123"))).authResult
            val community = first.call(Envelope.newBuilder().setCreateServer(CreateServerRequest.newBuilder().setName("Voice fixture"))).server
            val channel = first.call(Envelope.newBuilder().setCreateChannel(CreateChannelRequest.newBuilder()
                .setServerId(community.id).setName("voice").setType(ChannelType.CHANNEL_TYPE_VOICE))).channel.id
            val invite = first.call(Envelope.newBuilder().setCreateInvite(CreateInviteRequest.newBuilder().setServerId(community.id))).invite.token
            val peer = connect()
            val peerLogin = peer.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername("voicepeer").setPassword("fixture-voice-peer-password-123"))).authResult
            peer.call(Envelope.newBuilder().setJoinInvite(JoinInviteRequest.newBuilder().setToken(invite)))
            val (lease, sender) = open(first, channel)
            val (_, receiver) = open(peer, channel)
            // Real relay carries actual desktop-compatible Opus, decoded per remote stream.
            val encoder = VoiceCodec(true)
            val tone = FloatArray(960) { (0.3 * kotlin.math.sin(2 * Math.PI * 440 * it / 48000)).toFloat() }
            val opus = try { encoder.encode(tone) } finally { encoder.close() }
            for (index in 0..2) sender.sendAudio(index * 960L, opus)
            val received = withTimeout(5000) {
                while (true) {
                    receiver.playout().firstOrNull()?.let { return@withTimeout it }
                    delay(20)
                }
                @Suppress("UNREACHABLE_CODE") error("No relay audio")
            }
            assertEquals(firstLogin.user.id, received.sender)
            assertEquals(lease.streamId, received.stream)
            assertEquals(0L, received.timestamp)
            assertArrayEquals(opus, received.opus)
            VoiceCodec(false).use { decoder ->
                val pcm = decoder.decode(received.opus)!!
                assertTrue(pcm.sumOf { (it * it).toDouble() } / 960 > 0.005)
            }
            // Opposite direction uses a separate stream/key and correct sender identity.
            for (index in 0..2) receiver.sendAudio(index * 960L, opus)
            val reverse = withTimeout(5000) {
                while (true) {
                    sender.playout().firstOrNull()?.let { return@withTimeout it }
                    delay(20)
                }
                @Suppress("UNREACHABLE_CODE") error("No reverse relay audio")
            }
            assertEquals(peerLogin.user.id, reverse.sender)
            VoiceCodec(false).use { decoder -> assertEquals(960, decoder.decode(reverse.opus)!!.size) }
            val replacement = connect()
            val owner = replacement.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder()
                .setUsername("voicefixture").setPassword("fixture-voice-password-123"))).authResult
            try { open(replacement, channel); fail("Implicit voice transfer succeeded") }
            catch (expected: ServerError) { assertEquals(8, expected.code) }
            val (newLease, newMedia) = open(replacement, channel, transfer = true)
            assertNotEquals(lease.streamId, newLease.streamId)
            assertNotEquals(lease.mediaKey, newLease.mediaKey)
            val state = replacement.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState
                .voiceStatesList.single { it.userId == firstLogin.user.id }
            assertEquals(owner.sessionId, state.ownerSessionId)
            try { first.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance())); fail("Former owner left") }
            catch (expected: ServerError) { assertEquals(8, expected.code) }
            // Owner integration is still pending; tests explicitly stop stale local transport.
            sender.stop()
            for (index in 0..2) newMedia.sendAudio((index + 20) * 960L, opus)
            val moved = withTimeout(5000) {
                while (true) {
                    receiver.playout().firstOrNull { it.stream == newLease.streamId }?.let { return@withTimeout it }
                    delay(20)
                }
                @Suppress("UNREACHABLE_CODE") error("No transferred relay audio")
            }
            assertArrayEquals(opus, moved.opus)
            VoiceCodec(false).use { decoder -> assertEquals(960, decoder.decode(moved.opus)!!.size) }
            replacement.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance()))
            peer.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance()))
        } finally {
            media.forEach { it.stop() }
            connections.forEach { it.close() }
            scope.cancel()
        }
    }

    @Test fun registrationTimeoutExpiryAndCloseReleaseSocket(): Unit = runBlocking {
        val udp = DatagramSocket(0, InetAddress.getLoopbackAddress())
        val key = ByteArray(32) { it.toByte() }
        val now = java.util.concurrent.atomic.AtomicLong(0)
        val lease = VoiceSession.newBuilder().setChannelId(1).setStreamId(10).setMediaKey(ByteString.copyFrom(key))
            .setUdpPort(udp.localPort).setExpiresAt(System.currentTimeMillis() + 30000).build()
        val transport = VoiceMedia.open(InetAddress.getLoopbackAddress(), lease, clock = { now.get() })
        try {
            udp.soTimeout = 3000
            val packet = DatagramPacket(ByteArray(1400), 1400)
            withContext(Dispatchers.IO) { udp.receive(packet) }
            val data = packet.data.copyOf(packet.length)
            assertArrayEquals(byteArrayOf(1), NativeMedia.open(key, data, 1))
            try { transport.sendAudio(0, byteArrayOf(1)); fail("Audio sent before register ACK") }
            catch (_: IllegalStateException) { }
            val ackHeader = ByteBuffer.allocate(24).put(1).put(3).put(0).put(0).putInt(10).putLong(0).putInt(1).putInt(0).array()
            val ack = NativeMedia.seal(key, ackHeader, byteArrayOf(3), 2)!!
            // Forged ACK must not register or poison the replay window for the valid ACK.
            val bad = ack.copyOf().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
            withContext(Dispatchers.IO) {
                udp.send(DatagramPacket(bad, bad.size, packet.socketAddress))
            }
            delay(100); assertFalse(transport.registered.value)
            withContext(Dispatchers.IO) { udp.send(DatagramPacket(ack, ack.size, packet.socketAddress)) }
            withTimeout(3000) { transport.registered.first { it } }
            transport.stop()
            assertFalse(transport.registered.value)
            try { transport.sendAudio(0, byteArrayOf(1)); fail("Closed media sent") }
            catch (_: IllegalStateException) { }
            // Registration deadline is monotonic; no indefinite socket/service lifetime.
            now.set(0)
            try {
                VoiceMedia.open(InetAddress.getLoopbackAddress(), lease)
                fail("Reopened lease reset nonce counters")
            } catch (_: IllegalStateException) { }
            val silent = VoiceMedia.open(InetAddress.getLoopbackAddress(), lease.toBuilder().setStreamId(11).build(), clock = { now.get() })
            try {
                // Observe its first register packet before advancing the fake clock;
                // thread scheduling must not become the timeout test's premise.
                withContext(Dispatchers.IO) {
                    do {
                        packet.length = packet.data.size
                        udp.receive(packet)
                    } while (ByteBuffer.wrap(packet.data).getInt(4) != 11)
                }
                now.set(10001)
                withTimeout(3000) { silent.ended.first { it } }
                try { silent.sendAudio(0, byteArrayOf(1)); fail("Unregistered media sent") }
                catch (_: IllegalStateException) { }
                assertFalse(silent.registered.value)
            } finally { silent.stop() }
            val expiry = java.util.concurrent.atomic.AtomicLong(100)
            val expiring = VoiceMedia.open(InetAddress.getLoopbackAddress(), lease.toBuilder().setStreamId(12).setExpiresAt(200).build(),
                wallClock = { expiry.get() })
            try {
                expiry.set(201)
                withTimeout(3000) { expiring.ended.first { it } }
                assertFalse(expiring.registered.value)
            } finally { expiring.stop() }
            try {
                VoiceMedia.open(InetAddress.getLoopbackAddress(), lease.toBuilder().setExpiresAt(1).build())
                fail("Expired lease opened")
            } catch (_: IllegalArgumentException) { }
        } finally { transport.stop(); udp.close(); key.fill(0) }
    }
}
