package org.omachat.android

import android.content.Context
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import omachat.proto.Network.*
import org.omachat.protocol.ControlConnection
import org.omachat.protocol.ServerError

/** No lease, media key or rejoin intent is persisted. */
data class VoiceUi(val channel: Long = 0, val starting: Boolean = false, val connected: Boolean = false,
    val muted: Boolean = false, val serverMuted: Boolean = false, val transferChannel: Long = 0,
    val error: String = "") {
    val active get() = starting || connected
}

internal class VoiceCall(private val context: Context, private val ended: (Long, String) -> Unit) {
    private val lock = Mutex()
    private val mutable = MutableStateFlow(VoiceUi())
    val ui = mutable.asStateFlow()
    private var control: ControlConnection? = null
    private var stream = 0
    private var token = 0L
    private var observedSequence = 0L
    private var owner = 0L
    private var user = 0L
    private var media: VoiceMedia? = null
    private var audio: VoiceAudio? = null
    private var service: VoiceService? = null
    private var monitor: Job? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    fun reserve(channel: Long): Boolean {
        val old = mutable.value
        return !old.active && mutable.compareAndSet(old, VoiceUi(channel = channel, starting = true))
    }
    suspend fun join(requestToken: Long, socket: ControlConnection, sessionId: Long, userId: Long, channel: Long,
        transfer: Boolean, foregroundService: VoiceService, valid: () -> Boolean) = lock.withLock {
        check(mutable.value.starting && mutable.value.channel == channel)
        token = requestToken; control = socket; owner = sessionId; user = userId; service = foregroundService
        var accepted = false
        try {
            val reply = socket.call(Envelope.newBuilder().setJoinVoice(JoinVoiceRequest.newBuilder()
                .setChannelId(channel).setTransfer(transfer)))
            check(reply.hasVoiceSession() && reply.voiceSession.channelId == channel) { "Invalid voice lease" }
            accepted = true
            stream = reply.voiceSession.streamId
            media = VoiceMedia.open(socket.peerAddress, reply.voiceSession)
            withTimeout(12000) { media!!.registered.first { it || media!!.ended.value } }
            check(!media!!.ended.value) { "Voice relay unavailable" }
            // An ordinary Sync request observes current ownership without discarding
            // the coordinator's queued events or resetting its replay barrier.
            val snapshot = socket.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState
            val state = snapshot.voiceStatesList.firstOrNull { it.userId == user }
            check(state != null && matches(state)) { "Voice ownership changed while joining" }
            check(valid()) { "Voice join cancelled" }
            observedSequence = snapshot.lastSequence
            foregroundService.acquireFocus()
            check(valid()) { "Voice join cancelled" }
            audio = VoiceAudio.start(media!!, AndroidVoiceDevice.open(context),
                muted = state.selfMute || state.serverMute || state.selfDeaf || state.serverDeaf,
                deafened = state.selfDeaf || state.serverDeaf)
            mutable.value = VoiceUi(channel = channel, connected = true, muted = state.selfMute, serverMuted = state.serverMute)
            val worker = audio!!; val transport = media!!
            monitor = scope.launch {
                coroutineScope {
                    launch { transport.ended.first { it }; ended(requestToken, "Voice relay ended. Join again when ready.") }
                    launch { val failure = worker.failure.first { it != null }; ended(requestToken, failure!!) }
                }
            }
        } catch (problem: Exception) {
            withContext(NonCancellable) {
                release()
                // Never attempt leave after ownership conflict: another device owns it.
                if (accepted) runCatching { withTimeout(2500) {
                    socket.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance()))
                } }
                if (!accepted && problem !is ServerError) socket.close() // ambiguous join: end this control owner
                mutable.value = if (problem is ServerError && problem.code == 8 && !accepted)
                    VoiceUi(transferChannel = channel) else VoiceUi(error = if (problem is CancellationException) "" else problem.message ?: "Voice join failed")
            }
            if (problem is CancellationException) throw problem
        }
    }
    private fun matches(state: VoiceState) = state.channelId == mutable.value.channel &&
        state.ownerSessionId == owner && state.streamId == stream
    suspend fun observe(state: VoiceState, sequence: Long = 0) = lock.withLock {
        // Events already represented by the join snapshot cannot end a newer lease.
        if (sequence != 0L && sequence <= observedSequence) return@withLock
        if (control == null || state.userId != user) return@withLock
        if (!matches(state)) {
            val endedToken = token
            release()
            mutable.value = VoiceUi(error = "Voice ended or moved to another device.")
            ended(endedToken, mutable.value.error)
        } else {
            audio?.muted = state.selfMute || state.serverMute || state.selfDeaf || state.serverDeaf
            audio?.deafened = state.selfDeaf || state.serverDeaf
            mutable.update { it.copy(muted = state.selfMute, serverMuted = state.serverMute) }
        }
    }
    suspend fun reconcile(snapshot: SyncState) {
        if (ui.value.connected) observe(snapshot.voiceStatesList.firstOrNull { it.userId == user }
            ?: VoiceState.newBuilder().setUserId(user).build(), snapshot.lastSequence)
    }
    suspend fun mute() = lock.withLock {
        val worker = audio ?: return@withLock
        val socket = control ?: return@withLock
        val requested = !mutable.value.muted
        // Stop local transmission before asking the server; failed unmute stays muted.
        worker.muted = true
        try {
            socket.call(Envelope.newBuilder().setSetVoiceState(SetVoiceStateRequest.newBuilder().setSelfMute(requested)))
            mutable.update { it.copy(muted = requested) }
            worker.muted = requested || mutable.value.serverMuted || worker.deafened
        } catch (_: Exception) { ended(token, "Voice mute could not be confirmed. Join again when ready.") }
    }
    suspend fun stop(reason: String = "", leave: Boolean = false) = lock.withLock {
        val socket = control
        release()
        mutable.value = VoiceUi(error = reason)
        if (leave && socket != null) runCatching { withTimeout(2500) {
            socket.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance()))
        } }
    }
    fun dismissTransfer() { mutable.update { it.copy(transferChannel = 0) } }
    private suspend fun release() {
        monitor?.cancel(); monitor = null
        audio?.stop(); audio = null
        media?.stop(); media = null
        control = null; stream = 0; token = 0; owner = 0; user = 0
        service?.finish(); service = null
    }
}
