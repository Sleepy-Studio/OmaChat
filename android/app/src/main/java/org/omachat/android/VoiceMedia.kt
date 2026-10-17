package org.omachat.android

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import omachat.proto.Network.VoiceSession
import java.io.Closeable
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.TreeMap
import java.security.MessageDigest

/** Shared desktop AEAD implementation. No keys, packets or audio are persisted. */
internal object NativeMedia {
    init { System.loadLibrary("omachat_crypto") }
    external fun seal(key: ByteArray, header: ByteArray, payload: ByteArray, direction: Int): ByteArray?
    external fun open(key: ByteArray, datagram: ByteArray, direction: Int): ByteArray?
}

internal data class VoicePacket(val stream: Int, val sender: Long, val sequence: Long,
    val timestamp: Long, val endOfSpeech: Boolean, val opus: ByteArray)

/** Unsigned non-wrapping sequences: a lease must be replaced before nonce reuse.
 * Authentication precedes admission; invalid ciphertext cannot advance the window. */
internal class VoiceSequence(private var last: Long = 0) {
    init { require(last in 0..0xffffffffL) }
    fun next(): Long {
        check(last < 0xffffffffL) { "Voice nonce space exhausted; obtain a fresh lease" }
        return ++last
    }
}

internal class VoiceReplay {
    private var highest = -1L
    private var bits = 0L
    fun accept(sequence: Long): Boolean {
        require(sequence in 0..0xffffffffL)
        if (highest < 0) { highest = sequence; bits = 1; return true }
        if (sequence > highest) {
            val delta = sequence - highest
            bits = if (delta >= 64) 1 else (bits shl delta.toInt()) or 1
            highest = sequence
            return true
        }
        val behind = highest - sequence
        if (behind >= 64 || bits and (1L shl behind.toInt()) != 0L) return false
        bits = bits or (1L shl behind.toInt())
        return true
    }
}

/** Fixed 60 ms startup target, 64 frames per speaker; timestamps choose playout
 * slots because upstream audio sequences need not be contiguous after relay loss.
 * null at an active playout tick means a decoder should perform packet-loss concealment.
 * Adaptive jitter and Opus FEC remain follow-up work. Call only under the transport lock. */
internal class VoiceJitter {
    private val frames = TreeMap<Long, VoicePacket>()
    private var next: Long? = null
    private var lastArrival = 0L
    private var firstArrival = 0L
    val size get() = frames.size
    val active get() = next != null
    fun offer(packet: VoicePacket, now: Long) {
        if (next?.let { packet.timestamp < it } == true) return
        if (frames.isEmpty() && next == null) firstArrival = now
        lastArrival = now
        frames.putIfAbsent(packet.timestamp, packet)
        while (frames.size > 64) frames.pollFirstEntry()
    }
    fun poll(now: Long): VoicePacket? {
        if (now - lastArrival > 240) { frames.clear(); next = null; return null }
        if (next == null) {
            if (frames.isEmpty() || (frames.size < 3 && now - firstArrival < 60)) return null
            next = frames.firstKey()
        }
        // Bound latency to six frames (120 ms); skip backlog rather than accumulate it.
        if (frames.size > 6) {
            while (frames.size > 3) frames.pollFirstEntry()
            next = frames.firstKey()
        }
        val timestamp = next!!
        next = timestamp + 960
        val packet = frames.remove(timestamp)
        if (packet?.endOfSpeech == true) { frames.clear(); next = null }
        return packet
    }
}

/** One UDP socket and one fresh server lease. No automatic rejoin/transfer.
 * Owner must stop this on control disconnect, ownership loss or network changes.
 * Opus bytes are deliberately opaque here; this is not microphone/playback yet. */
internal class VoiceMedia private constructor(
    private val socket: DatagramSocket,
    private val stream: Int,
    private val key: ByteArray,
    private val expiresAt: Long,
    private val clock: () -> Long,
    private val wallClock: () -> Long
) : Closeable {
    private val lock = Any()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val mutable = MutableStateFlow(false)
    val registered = mutable.asStateFlow()
    private val stopped = MutableStateFlow(false)
    val ended = stopped.asStateFlow()
    @Volatile private var closed = false
    private val audioSequence = VoiceSequence()
    private val controlSequence = VoiceSequence()
    private val replay = HashMap<Pair<Int, Int>, VoiceReplay>()
    private val speakers = HashMap<Int, VoiceJitter>()
    private var receiver: Job? = null
    private var controls: Job? = null

    private fun launch() {
        receiver = scope.launch {
            val bytes = ByteArray(1401) // oversized packets are dropped, never truncated into valid frames
            try {
                while (isActive && !closed && wallClock() < expiresAt) {
                    val packet = DatagramPacket(bytes, bytes.size)
                    try { socket.receive(packet) } catch (_: SocketTimeoutException) { continue }
                    if (packet.length !in 40..1400) continue
                    val data = bytes.copyOf(packet.length)
                    synchronized(lock) {
                        if (closed || wallClock() >= expiresAt) return@synchronized
                        val plain = NativeMedia.open(key, data, 2) ?: return@synchronized
                        val header = ByteBuffer.wrap(data).order(ByteOrder.BIG_ENDIAN)
                        val type = data[1].toInt()
                        val remoteStream = header.getInt(4)
                        val sender = header.getLong(8)
                        val sequence = header.getInt(16).toLong() and 0xffffffffL
                        if (type == 3) {
                            if (remoteStream != stream || sender != 0L || plain.size != 1 || plain[0] != 3.toByte()) return@synchronized
                        } else if (type != 1 || sender <= 0 || remoteStream == 0 || plain.isEmpty() || data[2].toInt() and 0xfe != 0) {
                            return@synchronized
                        }
                        val id = remoteStream to type
                        if (id !in replay && replay.size >= 128) return@synchronized
                        if (type == 1 && remoteStream !in speakers && speakers.size >= 64) return@synchronized
                        if (!replay.getOrPut(id) { VoiceReplay() }.accept(sequence)) return@synchronized
                        if (type == 3) mutable.value = true
                        else speakers.getOrPut(remoteStream) { VoiceJitter() }.offer(VoicePacket(remoteStream, sender, sequence,
                            header.getInt(20).toLong() and 0xffffffffL, data[2].toInt() and 1 != 0, plain), clock())
                    }
                }
            } catch (_: Exception) { /* closure/failure ends this lease; never restart its counters */ }
            finally { close() }
        }
        controls = scope.launch {
            val started = clock()
            try {
                while (isActive && !closed && wallClock() < expiresAt) {
                    if (!registered.value && clock() - started >= 10_000) break
                    send(3, 0, 0, byteArrayOf(if (registered.value) 2 else 1))
                    delay(if (registered.value) 15_000 else 500)
                }
            } catch (_: Exception) { /* failure closes media; owning control layer decides recovery */ }
            finally { close() }
        }
    }
    private fun send(type: Int, flags: Int, timestamp: Long, payload: ByteArray) = synchronized(lock) {
        check(!closed && wallClock() < expiresAt) { "Voice lease ended" }
        require(payload.size <= 1360 && timestamp in 0..0xffffffffL)
        val sequence = (if (type == 1) audioSequence else controlSequence).next()
        val header = ByteBuffer.allocate(24).order(ByteOrder.BIG_ENDIAN)
            .put(1).put(type.toByte()).put(flags.toByte()).put(0).putInt(stream).putLong(0)
            .putInt(sequence.toInt()).putInt(timestamp.toInt()).array()
        val data = checkNotNull(NativeMedia.seal(key, header, payload, 1))
        socket.send(DatagramPacket(data, data.size))
    }
    fun sendAudio(timestamp: Long, opus: ByteArray, endOfSpeech: Boolean = false) {
        check(registered.value) { "Voice relay has not acknowledged registration" }
        require(opus.isNotEmpty())
        send(1, if (endOfSpeech) 1 else 0, timestamp, opus)
    }
    fun playout(): List<VoicePacket> = synchronized(lock) {
        if (closed) emptyList() else speakers.values.mapNotNull { it.poll(clock()) }
    }
    fun playoutTicks(): List<VoiceTick> = synchronized(lock) {
        if (closed) emptyList() else speakers.mapNotNull { (stream, jitter) ->
            val packet = jitter.poll(clock())
            if (packet != null || jitter.active) VoiceTick(stream, packet) else null
        }
    }
    override fun close() {
        // Unblock receive before taking the key/sequence lock. No blocking joins on Main.
        socket.close()
        synchronized(lock) {
            if (closed) return
            closed = true
            key.fill(0); replay.clear(); speakers.clear(); mutable.value = false; stopped.value = true
        }
        scope.cancel()
    }
    suspend fun stop() { close(); receiver?.join(); controls?.join() }

    companion object {
        // Never reopen a key/stream and reset its counters, even after failed registration.
        // Digests contain no usable media key. No eviction: exhausting the process bound
        // requires restarting the app, rather than silently reauthorizing old nonces.
        private val usedLeases = HashSet<String>()
        private fun claim(lease: VoiceSession) = synchronized(usedLeases) {
            val material = lease.mediaKey.toByteArray() + ByteBuffer.allocate(4).putInt(lease.streamId).array()
            val digest = try {
                MessageDigest.getInstance("SHA-256").digest(material).joinToString("") { "%02x".format(it) }
            } finally { material.fill(0) }
            check(digest !in usedLeases) { "Voice lease already used; obtain a fresh lease" }
            check(usedLeases.size < 1024) { "Voice lease limit reached; restart the app" }
            usedLeases.add(digest)
        }
        suspend fun open(address: InetAddress, lease: VoiceSession,
            clock: () -> Long = { android.os.SystemClock.elapsedRealtime() },
            wallClock: () -> Long = { System.currentTimeMillis() }): VoiceMedia = withContext(Dispatchers.IO) {
            require(lease.streamId != 0 && lease.mediaKey.size() == 32 && lease.udpPort in 1..65535 && lease.channelId > 0)
            require(lease.expiresAt > wallClock()) { "Voice lease expired" }
            claim(lease)
            suspendCancellableCoroutine { continuation ->
                val socket = DatagramSocket()
                continuation.invokeOnCancellation { socket.close() }
                try {
                    socket.connect(address, lease.udpPort)
                    socket.soTimeout = 500
                    socket.sendBufferSize = 64 * 1024
                    socket.receiveBufferSize = 128 * 1024
                    val media = VoiceMedia(socket, lease.streamId, lease.mediaKey.toByteArray(), lease.expiresAt, clock, wallClock)
                    media.launch()
                    continuation.resume(media) { _, value, _ -> value.close() }
                } catch (failure: Exception) {
                    socket.close()
                    continuation.resumeWith(Result.failure(failure))
                }
            }
        }
    }
}
