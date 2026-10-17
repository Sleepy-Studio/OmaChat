package org.omachat.android

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.*
import android.media.audiofx.AcousticEchoCanceler
import android.os.SystemClock
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.Closeable

internal object NativeOpus {
    init { System.loadLibrary("omachat_crypto") }
    external fun create(encode: Boolean): Long
    external fun destroy(handle: Long)
    external fun encode(handle: Long, samples: FloatArray): ByteArray?
    external fun decode(handle: Long, packet: ByteArray?): FloatArray?
}

/** Exactly desktop 48 kHz mono / 960 samples / 20 ms. One handle per stream. */
internal class VoiceCodec(private val encoder: Boolean) : Closeable {
    private var handle = NativeOpus.create(encoder).also { check(it != 0L) { "Opus allocation failed" } }
    @Synchronized fun encode(samples: FloatArray): ByteArray {
        check(handle != 0L && encoder)
        return checkNotNull(NativeOpus.encode(handle, samples)) { "Invalid Opus capture frame" }
    }
    @Synchronized fun decode(packet: ByteArray?): FloatArray? {
        check(handle != 0L && !encoder)
        return NativeOpus.decode(handle, packet)
    }
    @Synchronized override fun close() { NativeOpus.destroy(handle); handle = 0 }
}

internal data class VoiceTick(val stream: Int, val packet: VoicePacket?)

/** Bounded speaker state; loss uses PLC only while jitter says speech is active.
 * Malformed frames become one concealed tick. No concealment during startup/idle. */
internal class VoiceMixer : Closeable {
    private val decoders = HashMap<Int, VoiceCodec>()
    val size get() = decoders.size
    fun mix(ticks: List<VoiceTick>): FloatArray {
        require(ticks.size <= 64 && ticks.map { it.stream }.distinct().size == ticks.size)
        val active = ticks.map { it.stream }.toSet()
        decoders.keys.filter { it !in active }.forEach { decoders.remove(it)?.close() }
        val mixed = FloatArray(960)
        for (tick in ticks) {
            val decoder = decoders.getOrPut(tick.stream) { VoiceCodec(false) }
            val pcm = decoder.decode(tick.packet?.opus) ?: decoder.decode(null) ?: continue
            for (i in mixed.indices) mixed[i] += pcm[i]
        }
        for (i in mixed.indices) mixed[i] = mixed[i].coerceIn(-1f, 1f)
        return mixed
    }
    override fun close() { decoders.values.forEach { it.close() }; decoders.clear() }
}

/** Device boundary permits deterministic partial-read/write and failure tests. */
internal interface VoiceAudioDevice : Closeable {
    fun start()
    fun read(samples: FloatArray, offset: Int, count: Int): Int
    fun write(samples: FloatArray, offset: Int, count: Int): Int
}

/** Caller must obtain microphone permission and foreground ownership before open.
 * Never constructed by background reconnect; no microphone starts implicitly. */
internal class AndroidVoiceDevice private constructor(
    private val recorder: AudioRecord, private val track: AudioTrack,
    private val echo: AcousticEchoCanceler?
) : VoiceAudioDevice {
    override fun start() {
        recorder.startRecording()
        check(recorder.recordingState == AudioRecord.RECORDSTATE_RECORDING) { "Microphone did not start" }
        track.play()
    }
    override fun read(samples: FloatArray, offset: Int, count: Int) =
        recorder.read(samples, offset, count, AudioRecord.READ_NON_BLOCKING)
    override fun write(samples: FloatArray, offset: Int, count: Int) =
        track.write(samples, offset, count, AudioTrack.WRITE_NON_BLOCKING)
    override fun close() {
        try { recorder.stop() } catch (_: IllegalStateException) { }
        try { track.stop() } catch (_: IllegalStateException) { }
        echo?.release(); recorder.release(); track.release()
    }
    companion object {
        fun open(context: Context): AndroidVoiceDevice {
            check(context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                "Microphone permission required"
            }
            val inputSize = AudioRecord.getMinBufferSize(48000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT)
            val outputSize = AudioTrack.getMinBufferSize(48000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT)
            check(inputSize > 0 && outputSize > 0) { "48 kHz float audio unavailable" }
            var recorder: AudioRecord? = null
            var track: AudioTrack? = null
            var echo: AcousticEchoCanceler? = null
            try {
                recorder = AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.VOICE_COMMUNICATION)
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setChannelMask(AudioFormat.CHANNEL_IN_MONO).build())
                    .setBufferSizeInBytes(maxOf(inputSize, 960 * 4 * 4)).build()
                check(recorder.state == AudioRecord.STATE_INITIALIZED)
                track = AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                    .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(maxOf(outputSize, 960 * 4 * 4)).build()
                check(track.state == AudioTrack.STATE_INITIALIZED)
                if (AcousticEchoCanceler.isAvailable()) {
                    echo = AcousticEchoCanceler.create(recorder.audioSessionId)
                    echo?.enabled = true
                }
                return AndroidVoiceDevice(recorder, track, echo)
            } catch (failure: Throwable) {
                echo?.release(); recorder?.release(); track?.release()
                throw failure
            }
        }
    }
}

/** Audio worker. Owns device + codecs; VoiceCall/service owns the application
 * lease. Explicit stop joins workers before releasing device. Failure ends media.
 * No audio focus/routing/foreground service ownership is implied by this layer. */
internal class VoiceAudio private constructor(private val media: VoiceMedia,
    private val device: VoiceAudioDevice, private val scope: CoroutineScope, initiallyMuted: Boolean, initiallyDeafened: Boolean) {
    private val mutable = MutableStateFlow<String?>(null)
    val failure = mutable.asStateFlow()
    @Volatile var muted = initiallyMuted
    @Volatile var deafened = initiallyDeafened
    @OptIn(ExperimentalCoroutinesApi::class)
    private val job = scope.launch(start = CoroutineStart.ATOMIC) {
        var encoder: VoiceCodec? = null
        val mixer = VoiceMixer()
        try {
            encoder = VoiceCodec(true)
            device.start()
            coroutineScope {
                launch {
                    val frame = FloatArray(960)
                    var timestamp = 0L
                    while (isActive && !media.ended.value) {
                        var filled = 0
                        val deadline = SystemClock.elapsedRealtime() + 1000
                        while (filled < frame.size) {
                            ensureActive()
                            val n = device.read(frame, filled, frame.size - filled)
                            check(n in 0..(frame.size - filled)) { "Microphone read failed" }
                            filled += n
                            check(SystemClock.elapsedRealtime() < deadline) { "Microphone stalled" }
                            if (n == 0) delay(2)
                        }
                        // No timestamp wrap; obtain another lease before ~24.8 hours.
                        check(timestamp <= 0xffffffffL) { "Voice timestamp exhausted" }
                        if (!muted) media.sendAudio(timestamp, encoder.encode(frame))
                        timestamp += 960
                    }
                }
                launch {
                    var nextTick = SystemClock.elapsedRealtime()
                    while (isActive && !media.ended.value) {
                        val frame = mixer.mix(media.playoutTicks())
                        if (deafened) frame.fill(0f)
                        var written = 0
                        val deadline = SystemClock.elapsedRealtime() + 80
                        while (written < frame.size) {
                            ensureActive()
                            val n = device.write(frame, written, frame.size - written)
                            check(n in 0..(frame.size - written)) { "Speaker write failed" }
                            written += n
                            check(SystemClock.elapsedRealtime() < deadline) { "Speaker stalled" }
                            if (n == 0) delay(2)
                        }
                        nextTick += 20
                        val now = SystemClock.elapsedRealtime()
                        if (nextTick < now - 20) nextTick = now // skip missed ticks, never burst a backlog
                        delay(maxOf(0, nextTick - now))
                    }
                }
                launch { media.ended.collect { if (it) this@coroutineScope.cancel() } }
            }
        } catch (cancelled: CancellationException) { throw cancelled }
        catch (problem: Exception) { mutable.value = problem.message ?: "Voice audio failed" }
        finally { media.close(); encoder?.close(); mixer.close(); device.close(); scope.cancel() }
    }
    suspend fun stop() { job.cancelAndJoin(); media.stop(); scope.cancel() }
    companion object {
        fun start(media: VoiceMedia, device: VoiceAudioDevice, muted: Boolean = false, deafened: Boolean = false): VoiceAudio {
            check(media.registered.value && !media.ended.value) { "Voice registration required" }
            return VoiceAudio(media, device, CoroutineScope(SupervisorJob() + Dispatchers.IO), muted, deafened)
        }
    }
}
