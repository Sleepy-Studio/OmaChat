package org.omachat.android

import android.content.Context
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import androidx.room.withTransaction
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import omachat.proto.Network.*
import org.omachat.protocol.*
import java.nio.ByteBuffer
import java.security.MessageDigest
import java.util.UUID
import kotlin.random.Random

private val Context.preferences by preferencesDataStore("omachat")
enum class ConnectionState {
    NotConfigured, Connecting, CertificateConfirmationRequired, Authenticating,
    Synchronizing, Connected, Reconnecting, Offline, LoginRequired, IncompatibleServer, Error
}
data class Endpoint(val host: String, val port: Int, val username: String) {
    val key: String = MessageDigest.getInstance("SHA-256").digest("${host.lowercase()}:$port/$username".toByteArray())
        .joinToString("") { "%02x".format(it) }
    val pinKey: Preferences.Key<String> = stringPreferencesKey("pin/${host.lowercase()}:$port")
}
data class SessionUi(
    val state: ConnectionState = ConnectionState.NotConfigured,
    val endpoint: Endpoint? = null,
    val error: String = "",
    val certificate: String? = null,
    val changedCertificate: Boolean = false,
    val capabilities: Set<String> = emptySet(),
    val instanceId: String = "",
    val snapshot: SyncState = SyncState.getDefaultInstance(),
    val selected: Long = 0,
    val hasMore: Boolean = false,
    val loadingHistory: Boolean = false,
    val composing: Boolean = false,
    val ownDevices: List<DeviceKey> = emptyList(),
    val localDeviceKey: ByteString? = null,
    val deviceRevoked: Boolean = false,
    val devicesBusy: Boolean = false,
    val loginSessions: List<LoginSession> = emptyList(),
    val sessionsBefore: Long = 0,
    val sessionsBusy: Boolean = false,
    val encryptionRevision: Long = 0
)

/** Owns exactly one foreground connection. Activity recreation retains this
 * application coordinator. Background closes idle sockets; no fake push service. */
class SessionCoordinator(private val context: Context, val database: ChatDatabase) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val secrets = SecretStore(context)
    private val encryption = EncryptedConversations(context)
    private val attachments = Attachments(context)
    private val transfers = Transfers()
    val transfer = transfers.state
    fun cancelTransfer() = transfers.cancel()
    fun dismissTransfer() = transfers.dismiss()
    val dao = database.chat()
    private val messageCache = MessageCache(database, encryption::remember)
    private val mutable = MutableStateFlow(SessionUi())
    val ui = mutable.asStateFlow()
    private val voiceCall = VoiceCall(context) { token, reason -> if (token != 0L && token == voiceRequest) leaveVoice(reason) }
    val voice = voiceCall.ui
    private val voiceIds = java.util.concurrent.atomic.AtomicLong(0)
    @Volatile private var voiceRequest = 0L
    @Volatile private var voiceJoin: Job? = null
    @Volatile private var voiceSocket: ControlConnection? = null
    private var voiceChannel = 0L
    private var voiceTransfer = false
    /** Invoked on Main by visible UI, including after a permission result. */
    fun joinVoice(channel: Long, transfer: Boolean = false) {
        val state = mutable.value
        if (!androidx.lifecycle.ProcessLifecycleOwner.get().lifecycle.currentState.isAtLeast(androidx.lifecycle.Lifecycle.State.STARTED)
            || state.state != ConnectionState.Connected || "voice.ownership.v1" !in state.capabilities
            || state.snapshot.channelsList.none { it.id == channel && it.type == ChannelType.CHANNEL_TYPE_VOICE }) return
        if (context.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) return
        if (!voiceCall.reserve(channel)) return
        voiceSocket = connection; voiceChannel = channel; voiceTransfer = transfer
        val token = voiceIds.incrementAndGet(); voiceRequest = token
        try {
            context.startForegroundService(android.content.Intent(context, VoiceService::class.java).putExtra("request", token))
        } catch (_: Exception) { leaveVoice("Microphone service could not start. Open OmaChat and join again.") }
    }
    internal fun voiceRequestValid(token: Long) = token != 0L && token == voiceRequest && voice.value.active
    internal fun voiceServiceStarted(token: Long, service: VoiceService) {
        voiceJoin = scope.launch { try { lifecycleLock.withLock {
            if (!voiceRequestValid(token) || voiceSocket !== connection || mutable.value.state != ConnectionState.Connected) {
                service.finish()
                if (token == voiceRequest) { voiceRequest = 0; voiceCall.stop("Connection changed before voice joined.") }
                return@withLock
            }
            voiceCall.join(token, connection!!, auth!!.sessionId, mutable.value.snapshot.self.id, voiceChannel, voiceTransfer, service) { voiceRequestValid(token) && voiceSocket === connection }
            if (!voice.value.active) { voiceRequest = 0; voiceSocket = null; service.finish() }
        } } finally { withContext(NonCancellable) { if (!voiceRequestValid(token)) service.finish() } } }
    }
    internal fun voiceServiceLost(token: Long) { if (voiceRequestValid(token)) leaveVoice("Microphone service stopped. Join again when ready.") }
    fun leaveVoice(reason: String = "") {
        // Revoke staged service commands before any suspended network operation.
        voiceRequest = 0
        voiceJoin?.cancel()
        scope.launch { lifecycleLock.withLock {
            voiceCall.stop(reason, leave = true); voiceSocket = null
            if (!foreground) { stopLoop(); mutable.update { it.copy(state = if (it.endpoint == null) ConnectionState.NotConfigured else ConnectionState.Offline) } }
        } }
    }
    fun muteVoice() { scope.launch { lifecycleLock.withLock { voiceCall.mute() } } }
    fun cancelVoiceTransfer() = voiceCall.dismissTransfer()
    private var connection: ControlConnection? = null
    private var job: Job? = null
    @Volatile private var foreground = false
    private var auth: AuthResult? = null
    private var initialPassword: String? = null
    private val lifecycleLock = Mutex()
    private val sendLock = Mutex()
    private val compositionLock = Mutex()
    private val historyLock = Mutex()
    private var initialized = false

    init {
        scope.launch { lifecycleLock.withLock {
            val prefs = context.preferences.data.first()
            val host = prefs[stringPreferencesKey("host")]
            val username = prefs[stringPreferencesKey("username")]
            if (host != null && username != null) {
                val endpoint = Endpoint(host, prefs[intPreferencesKey("port")] ?: 6473, username)
                auth = runCatching { secrets.read(endpoint.key)?.let { AuthResult.parseFrom(it) } }.getOrNull()
                dao.interruptSends(endpoint.key)
                val savedSnapshot = dao.snapshot(endpoint.key)
                val cached = savedSnapshot?.let { SyncState.parseFrom(it.wire) } ?: SyncState.getDefaultInstance()
                mutable.update { it.copy(instanceId = savedSnapshot?.instanceId ?: "") }
                if (cached.self.id != 0L) runCatching {
                    encryption.restoreOffline(endpoint.key, savedSnapshot?.instanceId ?: "", cached.self.id,
                        cached.channelsList.filter { privateChannel(it) }.flatMap { it.recipientIdsList })
                }.onFailure { mutable.update { it.copy(error = "Saved encryption keys could not be opened. Private messages are unavailable.") } }
                // Cached structure supports offline reading, but never authorizes sends.
                mutable.update { it.copy(snapshot = cached) }
                mutable.update { it.copy(endpoint = endpoint, selected = prefs[longPreferencesKey("selectedChannel")] ?: 0, state = if (auth == null) ConnectionState.LoginRequired else ConnectionState.Offline) }
            }
            initialized = true
            if (foreground && auth != null) start()
        } }
    }
    fun foreground(active: Boolean) {
        // TLS close may write close-notify. Lifecycle callbacks run on Main;
        // stopLoop closes before cancelling, within this coordinator's I/O scope.
        scope.launch { lifecycleLock.withLock {
            foreground = active
            if (active && initialized && (auth != null || initialPassword != null)) start()
            else if (!active && !voice.value.active) {
                stopLoop()
                if (mutable.value.endpoint != null) mutable.update { it.copy(state = ConnectionState.Offline) }
            }
        } }
    }
    fun networkChanged() {
        val hadVoice = voice.value.active
        if (hadVoice) { voiceRequest = 0; voiceJoin?.cancel() }
        scope.launch { lifecycleLock.withLock {
            if ((!foreground && !hadVoice && !voice.value.active) || mutable.value.endpoint == null) return@withLock
            stopLoop()
            if (!foreground) mutable.update { it.copy(state = ConnectionState.Offline) }
            if (auth != null || initialPassword != null) start()
        } }
    }
    private suspend fun stopLoop() {
        voiceRequest = 0; voiceSocket = null
        voiceJoin?.cancel()
        voiceCall.stop("", leave = false)
        encryption.endDirectoryObservation()
        connection?.close(); connection = null
        job?.cancelAndJoin(); job = null
    }
    fun login(host: String, port: Int, username: String, password: String) {
        if (host.isBlank() || port !in 1..65535 || username.isBlank() || password.isBlank()) {
            mutable.update { it.copy(error = "Enter a server, port, username and password.") }; return
        }
        voiceRequest = 0; voiceJoin?.cancel()
        scope.launch { lifecycleLock.withLock {
            stopLoop()
            val endpoint = Endpoint(host.trim().lowercase(), port, username.trim().lowercase())
            initialPassword = password
            auth = null
            mutable.value = SessionUi(state = ConnectionState.Connecting, endpoint = endpoint)
            context.preferences.edit {
                it[stringPreferencesKey("host")] = endpoint.host
                it[intPreferencesKey("port")] = endpoint.port
                it[stringPreferencesKey("username")] = endpoint.username
            }
            start()
        } }
    }
    fun acceptCertificate() {
        val state = mutable.value
        val endpoint = state.endpoint ?: return
        val fingerprint = state.certificate ?: return
        scope.launch { lifecycleLock.withLock {
            stopLoop()
            context.preferences.edit { it[endpoint.pinKey] = fingerprint }
            mutable.update { it.copy(certificate = null, error = "") }
            if (auth == null) mutable.update { it.copy(state = ConnectionState.LoginRequired) }
            else start()
        } }
    }
    private fun start() {
        if ((!foreground && !voice.value.active) || job?.isActive == true) return
        val endpoint = mutable.value.endpoint ?: return
        job = scope.launch {
            var attempt = 0
            while (isActive && (foreground || voice.value.active)) {
                encryption.endDirectoryObservation()
                var socket: ControlConnection? = null
                try {
                    mutable.update { it.copy(state = if (attempt == 0) ConnectionState.Connecting else ConnectionState.Reconnecting, error = "") }
                    val pin = context.preferences.data.first()[endpoint.pinKey]
                    socket = ControlConnection.connect(endpoint.host, endpoint.port, pin, this)
                    connection = socket
                    val hello = socket.call(Envelope.newBuilder().setHello(Hello.newBuilder()
                        .setProtocolMajor(1).setProtocolMinor(2).setClientVersion("android/0.1.0-dev")
                        .addCapabilities("messages.idempotency.v1").addCapabilities("read.markers.v1").addCapabilities("e2e.v1").addCapabilities("dm.group")))
                    check(hello.hasHelloReply() && hello.helloReply.protocolMajor == 1) { "Incompatible server" }
                    val instance = hello.helloReply.instanceId
                    val previous = dao.snapshot(endpoint.key)
                    if (instance.isNotEmpty() && previous?.instanceId?.isNotEmpty() == true && instance != previous.instanceId) {
                        previous?.let { SyncState.parseFrom(it.wire).channelsList.filter { privateChannel(it) }.forEach { channel -> secrets.remove(draftAddress(endpoint.key, channel.id)) } }
                        database.withTransaction {
                            messageCache.clear(endpoint.key); dao.clearOutbox(endpoint.key)
                            dao.clearDrafts(endpoint.key); dao.clearMarkers(endpoint.key); dao.clearSnapshot(endpoint.key)
                        }
                        throw ServerError(4, "The server instance changed. Sign in again before using it.")
                    }
                    mutable.update { it.copy(instanceId = instance, capabilities = hello.helloReply.capabilitiesList.toSet(), state = ConnectionState.Authenticating) }
                    var replayCount: Int? = null
                    val password = initialPassword
                    if (password != null) {
                        auth = socket.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder()
                            .setUsername(endpoint.username).setPassword(password))).authResult
                        initialPassword = null
                        secrets.write(endpoint.key, auth!!.toByteArray())
                    } else {
                        val saved = auth ?: throw ServerError(4, "Log in to continue")
                        var resumed = false
                        if (saved.accessExpiresAt > System.currentTimeMillis()) {
                            try {
                                val reply = socket.call(Envelope.newBuilder().setResume(ResumeRequest.newBuilder()
                                    .setAccessToken(saved.accessToken).setSessionId(saved.sessionId).setLastSequence(previous?.let { SyncState.parseFrom(it.wire).lastSequence } ?: 0)))
                                if (previous != null && reply.hasResumeResult() && reply.resumeResult.replayedEvents in 0..32)
                                    replayCount = reply.resumeResult.replayedEvents.toInt()
                                resumed = true
                            } catch (e: ServerError) {
                                if (e.code == 12) resumed = true // authenticated, replay unavailable; full sync below
                                else if (e.code != 4) throw e
                            }
                        }
                        if (!resumed) {
                            try {
                                auth = socket.call(Envelope.newBuilder().setRefresh(RefreshRequest.newBuilder()
                                    .setRefreshToken(saved.refreshToken))).authResult
                                secrets.write(endpoint.key, auth!!.toByteArray())
                            } catch (e: Exception) {
                                if (e is CancellationException) throw e
                                // A dropped rotation response cannot safely be replayed.
                                throw ServerError(4, "Session refresh could not be confirmed. Log in again.")
                            }
                        }
                    }
                    val priorSelf = previous?.let { SyncState.parseFrom(it.wire).self.id } ?: 0
                    if (priorSelf != 0L && priorSelf != auth?.user?.id) database.withTransaction {
                        messageCache.clear(endpoint.key); dao.clearOutbox(endpoint.key)
                        dao.clearDrafts(endpoint.key); dao.clearMarkers(endpoint.key); dao.clearSnapshot(endpoint.key)
                    }
                    mutable.update { it.copy(state = ConnectionState.Synchronizing) }
                    val snapshot: SyncState
                    if (replayCount != null && priorSelf == auth?.user?.id) {
                        // Room's snapshot and cursor commit atomically with each event.
                        // Only bounded complete replay can retain this cached history.
                        mutable.update { it.copy(snapshot = SyncState.parseFrom(previous!!.wire)) }
                        val replayed = withTimeout(20_000) { List(replayCount) { socket.events.receive() } }
                        // A structural event may perform full sync and advance beyond
                        // subsequent replay entries. Consume the bounded replay first.
                        for (event in replayed) if (event.sequence == 0L || event.sequence > mutable.value.snapshot.lastSequence)
                            applyEvent(endpoint.key, event)
                        snapshot = mutable.value.snapshot
                    } else {
                        snapshot = socket.synchronize().syncState
                        // Structural sync cannot reconcile historical edits/deletions.
                        database.withTransaction {
                            messageCache.clear(endpoint.key)
                            dao.put(CachedSnapshot(endpoint.key, snapshot.toByteArray(), mutable.value.instanceId))
                            dao.clearMarkers(endpoint.key)
                            snapshot.readMarkersList.forEach { dao.put(LocalReadMarker(endpoint.key, it.channelId, it.messageId, it.timestamp)) }
                        }
                    }
                    if ("e2e.v1" in mutable.value.capabilities) {
                        encryption.initialize(endpoint.key, instance, snapshot.self.id, socket)
                        encryption.refresh(snapshot.channelsList.filter { privateChannel(it) }.flatMap { it.recipientIdsList }, socket)
                        mutable.update { it.copy(localDeviceKey = encryption.localDeviceKey(), deviceRevoked = encryption.revoked) }
                    }
                    mutable.update {
                        it.copy(snapshot = snapshot, selected = it.selected.takeIf { id -> snapshot.channelsList.any { c -> c.id == id } } ?: 0,
                            state = ConnectionState.Connected, error = "")
                    }
                    val selected = mutable.value.selected
                    if (selected != 0L) loadHistory(selected)
                    attempt = 0
                    coroutineScope {
                        launch { drainOutbox() }
                        for (event in socket.events) {
                            if (event.sequence == 0L || event.sequence > mutable.value.snapshot.lastSequence) applyEvent(endpoint.key, event)
                        }
                    }
                    error("Server disconnected")
                } catch (e: CancellationException) { throw e }
                catch (e: CertificateConfirmation) {
                    initialPassword = null
                    mutable.update { it.copy(state = ConnectionState.CertificateConfirmationRequired, certificate = e.fingerprint,
                        changedCertificate = e.changed, error = "Compare this fingerprint with your server operator before trusting it. After confirmation, log in again.") }
                    break
                } catch (e: ServerError) {
                    if (e.code == 4 || e.code == 5) {
                        initialPassword = null; auth = null
                        secrets.remove(endpoint.key)
                        mutable.update { it.copy(state = ConnectionState.LoginRequired, error = e.message ?: "Login required") }
                        break
                    }
                    if (e.code == 3) {
                        mutable.update { it.copy(state = ConnectionState.IncompatibleServer, error = "Server protocol is incompatible") }; break
                    }
                    mutable.update { it.copy(state = ConnectionState.Error, error = e.message ?: "Server rejected the request") }; break
                } catch (e: Exception) {
                    initialPassword = null
                    if (auth == null) {
                        mutable.update { it.copy(state = ConnectionState.LoginRequired, error = "Connection failed. Check the server and log in again.") }; break
                    }
                    mutable.update { it.copy(state = ConnectionState.Reconnecting, error = "Connection lost. Retrying…") }
                } finally {
                    withContext(NonCancellable) {
                        voiceRequest = 0; voiceSocket = null
                        voiceJoin?.cancel()
                        voiceCall.stop(if (voice.value.active) "Control connection lost. Join voice again when ready." else voice.value.error)
                    }
                    socket?.close()
                    if (connection === socket) connection = null
                    dao.interruptSends(endpoint.key)
                }
                delay(Random.nextLong(500, 1500) + (1000L shl attempt.coerceAtMost(5)))
                attempt++
            }
        }
    }
    private suspend fun applyEvent(account: String, event: Event) {
        if (event.hasVoiceStateUpdate()) voiceCall.observe(event.voiceStateUpdate, event.sequence)
        if (event.hasDeviceKeysChanged()) connection?.let { socket ->
            encryption.refresh(listOf(event.deviceKeysChanged.userId), socket)
            val devices = if (event.deviceKeysChanged.userId == mutable.value.snapshot.self.id) encryption.devices(socket) else null
            mutable.update { it.copy(ownDevices = devices ?: it.ownDevices, deviceRevoked = encryption.revoked, encryptionRevision = it.encryptionRevision + 1) }
        }
        val structural = event.hasChannelCreate() || event.hasChannelUpdate() || event.hasChannelDelete()
            || event.hasPermissionsChanged() || event.hasMemberLeave() || event.hasMemberJoin() || event.hasMemberUpdate()
            || event.hasRoleUpdate() || event.hasRoleDelete() || event.hasServerDelete() || event.hasServerCreate() || event.hasServerUpdate()
        if (structural) {
            val snapshot = connection?.synchronize()?.syncState ?: return
            voiceCall.reconcile(snapshot)
            val selected = mutable.value.selected
            database.withTransaction {
                messageCache.clear(account)
                dao.put(CachedSnapshot(account, snapshot.toByteArray(), mutable.value.instanceId))
            }
            mutable.update { it.copy(snapshot = snapshot, selected = selected.takeIf { id -> snapshot.channelsList.any { c -> c.id == id } } ?: 0) }
            if ("e2e.v1" in mutable.value.capabilities) connection?.let { encryption.refresh(snapshot.channelsList.filter { privateChannel(it) }.flatMap { it.recipientIdsList }, it) }
            if (mutable.value.selected != 0L) loadHistory(mutable.value.selected)
            return
        }
        var snapshot = mutable.value.snapshot
        if (event.hasVoiceStateUpdate()) snapshot = snapshot.toBuilder().clearVoiceStates()
            .addAllVoiceStates(snapshot.voiceStatesList.filter { it.userId != event.voiceStateUpdate.userId } + event.voiceStateUpdate).build()
        if (event.hasUserUpdate()) snapshot = snapshot.toBuilder().clearUsers()
            .addAllUsers(snapshot.usersList.filter { it.id != event.userUpdate.id } + event.userUpdate).build()
        if (event.hasPresenceUpdate()) snapshot = snapshot.toBuilder().clearUsers().addAllUsers(snapshot.usersList.map {
            if (it.id == event.presenceUpdate.userId) it.toBuilder().setStatus(event.presenceUpdate.status).build() else it
        }).build()
        if (event.hasReadMarker()) snapshot = snapshot.toBuilder().clearReadMarkers()
            .addAllReadMarkers(snapshot.readMarkersList.filter { it.channelId != event.readMarker.channelId } + event.readMarker).build()
        if (event.sequence != 0L) snapshot = snapshot.toBuilder().setLastSequence(event.sequence).build()
        database.withTransaction {
            when {
                event.hasMessageCreate() -> messageCache.event(account, event.messageCreate)
                event.hasMessageUpdate() -> messageCache.event(account, event.messageUpdate)
                event.hasMessageDelete() -> messageCache.delete(account, event.messageDelete.messageId)
                event.hasReadMarker() -> event.readMarker.let { dao.put(LocalReadMarker(account, it.channelId, it.messageId, it.timestamp)) }
            }
            if (event.sequence != 0L) dao.put(CachedSnapshot(account, snapshot.toByteArray(), mutable.value.instanceId))
        }
        mutable.update { it.copy(snapshot = snapshot) }
        if (event.hasReaction() && event.reaction.channelId == mutable.value.selected) loadHistory(event.reaction.channelId)
    }
    fun select(channel: Long) {
        mutable.update { it.copy(selected = channel, hasMore = false) }
        if (channel != 0L) scope.launch {
            context.preferences.edit { it[longPreferencesKey("selectedChannel")] = channel }
            action { loadHistory(channel) }
        }
    }
    suspend fun loadHistory(channel: Long, before: Long = 0) = historyLock.withLock {
        val endpoint = mutable.value.endpoint ?: return@withLock
        if (mutable.value.snapshot.channelsList.none { it.id == channel }) return@withLock
        mutable.update { it.copy(loadingHistory = true) }
        try {
            val socket = connection ?: return@withLock
            val fence = messageCache.fence(endpoint.key)
            val page = socket.call(Envelope.newBuilder().setGetMessages(GetMessagesRequest.newBuilder()
                .setChannelId(channel).setBeforeMessageId(before).setLimit(50))).messagePage
            if (socket !== connection || mutable.value.endpoint?.key != endpoint.key) return@withLock
            messageCache.history(endpoint.key, page.messagesList, fence)
            if (mutable.value.selected == channel) mutable.update { it.copy(hasMore = page.hasMore) }
        } finally { mutable.update { it.copy(loadingHistory = false) } }
    }
    fun older() {
        val state = mutable.value
        val account = state.endpoint?.key ?: return
        scope.launch { action {
            val oldest = dao.oldest(account, state.selected) ?: return@action
            loadHistory(state.selected, oldest.id)
        } }
    }
    private fun draftAddress(account: String, channel: Long) = MessageDigest.getInstance("SHA-256")
        .digest("private-draft/$account/$channel".toByteArray()).joinToString("") { "%02x".format(it) }
    suspend fun draftText(channel: Long): String {
        val state = mutable.value
        val account = state.endpoint?.key ?: return ""
        return if (state.snapshot.channelsList.any { it.id == channel && privateChannel(it) })
            secrets.read(draftAddress(account, channel))?.toString(Charsets.UTF_8) ?: ""
        else dao.draft(account, channel)?.text ?: ""
    }
    suspend fun saveDraft(channel: Long, text: String) = sendLock.withLock {
        val state = mutable.value
        val account = state.endpoint?.key ?: return@withLock
        if (state.snapshot.channelsList.any { it.id == channel && privateChannel(it) }) {
            secrets.write(draftAddress(account, channel), text.take(4000).toByteArray())
            dao.put(Draft(account, channel, ""))
        } else dao.put(Draft(account, channel, text))
    }
    fun send(text: String, replyTo: Long = 0, attachment: android.net.Uri? = null, onQueued: () -> Unit = {}) {
        val state = mutable.value
        val endpoint = state.endpoint ?: return
        val channel = state.snapshot.channelsList.firstOrNull { it.id == state.selected } ?: return
        if (channel.type != ChannelType.CHANNEL_TYPE_TEXT && !privateChannel(channel)) return
        if ((text.isBlank() && attachment == null) || text.length > 4000) return
        scope.launch {
            if (!compositionLock.tryLock()) return@launch
            mutable.update { it.copy(composing = true, error = "") }
            try { action {
                val id = UUID.randomUUID()
                val bytes = ByteBuffer.allocate(16).putLong(id.mostSignificantBits).putLong(id.leastSignificantBits).array()
                val request = SendMessageRequest.newBuilder().setChannelId(channel.id)
                    .setReplyTo(replyTo).setOperationId(ByteString.copyFrom(bytes))
                val socket = connection ?: error("Connect before sending")
                val body = E2EBody.newBuilder().setContent(text)
                if (privateChannel(channel)) check("e2e.v1" in state.capabilities) { "This server does not support encrypted conversations" }
                attachment?.let { uri ->
                    val uploaded = transfers.run(channel.id, "Upload") { progress -> attachments.upload(uri, channel, socket::call, progress) }
                    request.addAttachmentIds(uploaded.first.id)
                    uploaded.second?.let { body.addFiles(it) }
                }
                if (privateChannel(channel)) {
                    check("e2e.v1" in state.capabilities) { "This server does not support encrypted conversations" }
                    request.setEncrypted(encryption.seal(channel, body.build(), socket))
                } else request.setContent(text)
                val payload = request.build()
                sendLock.withLock {
                    if (mutable.value.endpoint?.key != endpoint.key) return@action
                    dao.put(PendingSend(endpoint.key, id.toString(), channel.id, payload.toByteArray()))
                    // Edits made while the file uploads belong to the next composition.
                    if (draftText(channel.id) == text) {
                        dao.put(Draft(endpoint.key, channel.id, ""))
                        if (privateChannel(channel)) secrets.remove(draftAddress(endpoint.key, channel.id))
                    }
                }
                withContext(Dispatchers.Main) { onQueued() }
                drainOutbox()
            } } finally {
                mutable.update { it.copy(composing = false) }
                compositionLock.unlock()
            }
        }
    }
    private suspend fun drainOutbox() = sendLock.withLock {
        val endpoint = mutable.value.endpoint ?: return@withLock
        if (mutable.value.state != ConnectionState.Connected) return@withLock
        for (pending in dao.queued(endpoint.key)) {
            val socket = connection ?: break
            dao.put(pending.copy(state = "Sending"))
            try {
                val request = SendMessageRequest.parseFrom(pending.wire).toBuilder()
                if (!request.encrypted.isEmpty) {
                    val channel = mutable.value.snapshot.channelsList.firstOrNull { it.id == request.channelId }
                        ?: error("Conversation access changed")
                    encryption.validatePending(channel, request.encrypted, socket)
                }
                if ("messages.idempotency.v1" !in mutable.value.capabilities) request.clearOperationId()
                val fence = messageCache.fence(endpoint.key)
                val accepted = socket.call(Envelope.newBuilder().setSendMessage(request))
                check(accepted.hasChatMessage()) { "Missing message acknowledgement" }
                database.withTransaction { messageCache.acknowledge(endpoint.key, accepted.chatMessage, fence); dao.cancel(endpoint.key, pending.operation) }
            } catch (e: Exception) {
                dao.put(pending.copy(state = if (e is ServerError || e is IllegalStateException) "Failed" else "Uncertain",
                    error = if (e is ServerError || e is IllegalStateException) e.message ?: "Rejected" else "No acknowledgement. Delivery may have succeeded."))
                if (e is CancellationException) throw e
            }
        }
    }
    fun retry(operation: String) {
        val endpoint = mutable.value.endpoint ?: return
        scope.launch {
            val pending = dao.pending(endpoint.key, operation) ?: return@launch
            if (pending.state == "Uncertain" && "messages.idempotency.v1" !in mutable.value.capabilities) {
                mutable.update { it.copy(error = "This server cannot deduplicate. Check history before composing a new message.") }; return@launch
            }
            dao.put(pending.copy(state = "Queued", error = "")); drainOutbox()
        }
    }
    fun cancel(operation: String) { mutable.value.endpoint?.let { scope.launch { dao.cancel(it.key, operation) } } }
    fun markRead(message: ChatMessage) {
        if ("read.markers.v1" !in mutable.value.capabilities || mutable.value.selected != message.channelId) return
        scope.launch { action {
            connection?.call(Envelope.newBuilder().setSetReadMarker(SetReadMarkerRequest.newBuilder()
                .setChannelId(message.channelId).setMessageId(message.id)))
        } }
    }
    fun edit(message: ChatMessage, text: String) { scope.launch { action {
        val channel = mutable.value.snapshot.channelsList.firstOrNull { it.id == message.channelId } ?: return@action
        val socket = connection ?: return@action
        val request = EditMessageRequest.newBuilder().setMessageId(message.id)
        if (privateChannel(channel)) {
            check("e2e.v1" in mutable.value.capabilities)
            val original = encryption.open(message) ?: error("Cannot edit an unreadable encrypted message")
            request.setEncrypted(encryption.seal(channel, original.toBuilder().setContent(text).build(), socket))
        } else request.setContent(text)
        socket.call(Envelope.newBuilder().setEditMessage(request))
    } } }
    fun delete(message: ChatMessage) { scope.launch { action {
        connection?.call(Envelope.newBuilder().setDeleteMessage(DeleteMessageRequest.newBuilder().setMessageId(message.id)))
    } } }
    fun react(message: ChatMessage, emoji: String, add: Boolean) { scope.launch { action {
        connection?.call(Envelope.newBuilder().setReaction(ReactionRequest.newBuilder().setMessageId(message.id).setEmoji(emoji).setAdd(add)))
    } } }
    fun signOut() {
        voiceRequest = 0; voiceJoin?.cancel()
        val endpoint = mutable.value.endpoint ?: return
        scope.launch { lifecycleLock.withLock {
            voiceRequest = 0; voiceSocket = null
            voiceCall.stop("", leave = true)
            transfers.cancelAndJoin()
            val privateDrafts = mutable.value.snapshot.channelsList.filter { privateChannel(it) }.map { it.id }
            privateDrafts.forEach { secrets.remove(draftAddress(endpoint.key, it)) }
            // Hide the account immediately and prevent queued actions from repopulating it.
            mutable.value = SessionUi(state = ConnectionState.Offline)
            runCatching { connection?.let { encryption.revoke(it) } }
            encryption.forget()
            // Local removal is always possible; only LogoutRequest revokes this session.
            runCatching { connection?.call(Envelope.newBuilder().setLogout(LogoutRequest.getDefaultInstance())) }
            stopLoop()
            auth = null; initialPassword = null; secrets.remove(endpoint.key)
            sendLock.withLock {
                database.withTransaction { messageCache.clear(endpoint.key); dao.clearOutbox(endpoint.key); dao.clearDrafts(endpoint.key); dao.clearMarkers(endpoint.key); dao.clearSnapshot(endpoint.key) }
            }
            context.preferences.edit { it.remove(stringPreferencesKey("host")); it.remove(stringPreferencesKey("username")); it.remove(intPreferencesKey("port")); it.remove(longPreferencesKey("selectedChannel")) }
            mutable.value = SessionUi()
        } }
    }
    private fun privateChannel(channel: Channel) = channel.type == ChannelType.CHANNEL_TYPE_DM || channel.type == ChannelType.CHANNEL_TYPE_GROUP_DM
    fun messageFiles(message: ChatMessage): List<E2EFile> = if (message.encrypted.isEmpty) emptyList() else encryption.open(message)?.filesList ?: emptyList()
    fun download(message: ChatMessage, attachment: Attachment, destination: android.net.Uri) { scope.launch { action {
        val private = !message.encrypted.isEmpty
        val metadata = if (private) messageFiles(message).firstOrNull { it.attachmentId == attachment.id }
            ?: error("Missing authenticated attachment key") else null
        val socket = connection ?: error("Connect to download")
        transfers.run(message.channelId, "Download") { progress -> attachments.download(attachment, metadata, destination, socket::call, progress) }
    } } }
    fun createGroup(users: List<Long>) { scope.launch { action {
        check("e2e.v1" in mutable.value.capabilities)
        val reply = connection?.call(Envelope.newBuilder().setCreateGroupDm(CreateGroupDmRequest.newBuilder().addAllUserIds(users))) ?: return@action
        val snapshot = connection?.synchronize()?.syncState ?: return@action
        mutable.update { it.copy(snapshot = snapshot) }
        connection?.let { encryption.refresh(reply.channel.recipientIdsList, it) }
        select(reply.channel.id)
    } } }
    fun historicalObservation(message: ChatMessage): Long? = encryption.historicalObservation(message)
    fun messageText(message: ChatMessage): String = if (message.encrypted.isEmpty) message.content
        else encryption.open(message)?.content ?: "Encrypted message unavailable · new device, revoked/unknown sender, or altered payload"
    suspend fun safetyNumber(user: Long) = withContext(Dispatchers.IO) { encryption.safety(user) }
    fun verifiedKeys(user: Long) = encryption.verified(user)
    fun verifyKeys(user: Long, number: String) { scope.launch { action { encryption.verify(user, number); mutable.update { it.copy(error = "") } } } }
    fun encryptionKeys(user: Long) = encryption.keys(user)
    fun refreshLoginSessions(before: Long = 0) { loginSessionAction(before) }
    fun revokeLoginSession(id: Long) { loginSessionAction(0, id) }
    private fun loginSessionAction(before: Long, revoke: Long? = null) {
        scope.launch { lifecycleLock.withLock {
            mutable.update { it.copy(sessionsBusy = true, error = "") }
            try { action {
                check(mutable.value.state == ConnectionState.Connected && "sessions.manage.v1" in mutable.value.capabilities) { "Connect to a server supporting login-session management." }
                val socket = connection ?: error("Connect to manage login sessions.")
                val current = auth ?: error("Sign in to manage login sessions.")
                if (revoke != null) {
                    check(revoke > 0 && revoke != current.sessionId) { "Use Sign out to remove this installation's current login." }
                    val response = socket.call(Envelope.newBuilder().setRevokeLoginSession(RevokeLoginSessionRequest.newBuilder().setSessionId(revoke)))
                    check(response.hasOk()) { "Session revocation could not be confirmed. Refresh the list." }
                }
                val response = socket.call(Envelope.newBuilder().setListLoginSessions(ListLoginSessionsRequest.newBuilder().setBeforeId(before)))
                check(response.hasLoginSessionList()) { "Invalid login-session response" }
                val list = response.loginSessionList
                check(list.sessionsCount <= 100 && list.sessionsList.all { it.id > 0 && (before == 0L || it.id < before) && it.current == (it.id == current.sessionId) }
                    && list.sessionsList.map { it.id }.zipWithNext().all { (a, b) -> a > b }
                    && (list.nextBeforeId == 0L || list.nextBeforeId == list.sessionsList.lastOrNull()?.id)) { "Invalid login-session list" }
                check(socket === connection && auth?.sessionId == current.sessionId) { "Connection changed. Refresh the session list." }
                mutable.update { it.copy(loginSessions = list.sessionsList, sessionsBefore = list.nextBeforeId) }
            } } finally { mutable.update { it.copy(sessionsBusy = false) } }
        } }
    }
    fun refreshDevices() { deviceAction { socket -> encryption.devices(socket) } }
    fun revokeDevice(key: ByteString) { deviceAction { socket -> encryption.revokeDevice(key, socket) } }
    private fun deviceAction(block: suspend (ControlConnection) -> List<DeviceKey>) {
        scope.launch { lifecycleLock.withLock {
            if (mutable.value.devicesBusy) return@withLock
            mutable.update { it.copy(devicesBusy = true, error = "") }
            try { action {
                check(mutable.value.state == ConnectionState.Connected && "e2e.v1" in mutable.value.capabilities) { "Connect to manage encryption devices." }
                val socket = connection ?: error("Connect to manage encryption devices.")
                val devices = block(socket)
                check(socket === connection) { "Connection changed. Refresh the device list." }
                mutable.update { it.copy(ownDevices = devices, localDeviceKey = encryption.localDeviceKey(), deviceRevoked = encryption.revoked, encryptionRevision = it.encryptionRevision + 1) }
            } } finally { mutable.update { it.copy(devicesBusy = false) } }
        } }
    }
    fun changedKeys() = encryption.changed.toSet()
    fun acceptKeys(user: Long, number: String) { scope.launch { action { encryption.accept(user, number); mutable.update { it.copy(error = "") } } } }
    fun openDm(user: Long) { scope.launch { action {
        check("e2e.v1" in mutable.value.capabilities) { "Encrypted conversations require e2e.v1" }
        val reply = connection?.call(Envelope.newBuilder().setOpenDm(OpenDmRequest.newBuilder().setUserId(user))) ?: return@action
        check(reply.hasChannel())
        val snapshot = connection?.synchronize()?.syncState ?: return@action
        mutable.update { it.copy(snapshot = snapshot) }
        connection?.let { encryption.refresh(reply.channel.recipientIdsList, it) }
        select(reply.channel.id)
    } } }
    private suspend fun action(block: suspend () -> Unit) {
        try { block() } catch (e: CancellationException) { throw e }
        catch (e: Exception) { mutable.update { it.copy(error = if (e is ServerError) e.message ?: "Request rejected" else e.message ?: "Request could not be confirmed. Reconnect and check the server state.") } }
    }
}
