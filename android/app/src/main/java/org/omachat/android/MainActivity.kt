package org.omachat.android

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.paging.compose.*
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.distinctUntilChanged
import omachat.proto.Network.*

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        val suggestedHost = intent.getStringExtra("serverHost")?.takeIf {
            it.length <= 253 && it.matches(Regex("[A-Za-z0-9.-]+"))
        } ?: ""
        setContent { OmaChatTheme { OmaChat(viewModel(), suggestedHost) } }
    }
}
@Composable private fun OmaChatTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = darkColorScheme(
        primary = Color(0xFFB5A0EE), secondary = Color(0xFF8CCBC4),
        background = Color(0xFF101014), surface = Color(0xFF19191F),
        onBackground = Color(0xFFE9E5EF), onSurface = Color(0xFFE9E5EF)
    ), content = content)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun OmaChat(model: ChatViewModel, suggestedHost: String) {
    val state by model.ui.collectAsStateWithLifecycle()
    val voice by model.session.voice.collectAsStateWithLifecycle()
    val transfer by model.session.transfer.collectAsStateWithLifecycle()
    val messages = model.messages.collectAsLazyPagingItems()
    val pending by model.pending.collectAsStateWithLifecycle()
    var destination by rememberSaveable { mutableStateOf("Servers") }
    var devicesOpen by rememberSaveable { mutableStateOf(false) }
    var sessionsOpen by rememberSaveable { mutableStateOf(false) }
    val channel = state.snapshot.channelsList.firstOrNull { it.id == state.selected }
    val login = state.endpoint == null || state.state == ConnectionState.LoginRequired
    BackHandler(channel != null) { model.session.select(0) }
    Scaffold(
        topBar = {
            TopAppBar(title = { Column {
                Text(channel?.let { "#${it.name}" } ?: "OmaChat")
                Text(state.state.name.replace(Regex("([a-z])([A-Z])"), "$1 $2"), style = MaterialTheme.typography.labelSmall)
            } }, navigationIcon = { if (channel != null) TextButton(onClick = { model.session.select(0) }) { Text("Back") } })
        },
        bottomBar = { if (!login && channel == null) NavigationBar {
            listOf("Servers", "Messages", "Activity", "Settings").forEach { name ->
                NavigationBarItem(selected = destination == name, onClick = { destination = name },
                    icon = { Text(name.take(1)) }, label = { Text(name) })
            }
        } }
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            if (state.error.isNotEmpty()) Surface(color = MaterialTheme.colorScheme.errorContainer) {
                Text(state.error, Modifier.fillMaxWidth().padding(16.dp), color = MaterialTheme.colorScheme.onErrorContainer)
            }
            AnimatedVisibility(transfer != null && !login) {
                transfer?.let { progress ->
                    Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
                        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
                            Row {
                                Text("${progress.direction} · ${progress.stage}", Modifier.weight(1f), style = MaterialTheme.typography.labelLarge)
                                TextButton(onClick = { if (progress.active) model.session.cancelTransfer() else model.session.dismissTransfer() }) {
                                    Text(if (progress.active) "Cancel transfer" else "Dismiss")
                                }
                            }
                            if (progress.active && progress.stage == "Saving file") Text(
                                "Cancelling while saving can leave a partial file at the selected location.", style = MaterialTheme.typography.labelSmall)
                            if (progress.active) {
                                if (progress.total != null && progress.total > 0) {
                                    LinearProgressIndicator(progress = { (progress.bytes.toFloat() / progress.total).coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth())
                                    Text("${progress.bytes} / ${progress.total} bytes", style = MaterialTheme.typography.labelSmall)
                                } else LinearProgressIndicator(Modifier.fillMaxWidth())
                            }
                        }
                    }
                }
            }
            if (!login && (voice.active || voice.error.isNotEmpty())) Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
                Column(Modifier.fillMaxWidth().padding(12.dp)) {
                    Text(if (voice.active) "Voice · ${state.snapshot.channelsList.firstOrNull { it.id == voice.channel }?.name ?: voice.channel}" else voice.error)
                    if (voice.active) Row {
                        Text(if (voice.starting) "Joining…" else if (voice.serverMuted) "Muted by moderator" else "Connected", Modifier.weight(1f))
                        TextButton(onClick = model.session::muteVoice, enabled = voice.connected) { Text(if (voice.muted) "Unmute" else "Mute") }
                        TextButton(onClick = { model.session.leaveVoice() }) { Text("Leave voice") }
                    }
                    Text("Voice is encrypted to the relay; the server can access audio.", style = MaterialTheme.typography.labelSmall)
                }
            }
            if (login) Login(state, model.session, suggestedHost)
            else if (channel != null) Conversation(state, channel, messages, pending.filter { it.channel == channel.id }, model.session)
            else when (destination) {
                "Servers" -> Channels(state, model.session)
                "Messages" -> Channels(state, model.session, privateOnly = true)
                "Activity" -> Text("Read state synchronizes when you view messages on a supporting server. Mentions and activity navigation are pending.", Modifier.padding(24.dp))
                "Settings" -> Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    Text(state.endpoint?.let { "${it.username}@${it.host}:${it.port}" } ?: "No account", style = MaterialTheme.typography.titleMedium)
                    Text("Background delivery is unavailable in this build. Idle connections close in the background. An active voice call keeps its microphone service and connection running.")
                    Text("Community messages are readable by the server. Private conversations use device encryption. Voice uses relay encryption; the server can access audio.")
                    TextButton(onClick = model.session::networkChanged) { Text("Reconnect") }
                    OutlinedButton(onClick = { devicesOpen = true; model.session.refreshDevices() }, enabled = "e2e.v1" in state.capabilities) { Text("Encryption devices") }
                    OutlinedButton(onClick = { sessionsOpen = true; model.session.refreshLoginSessions() }, enabled = "sessions.manage.v1" in state.capabilities) { Text("Login sessions") }
                    if ("sessions.manage.v1" !in state.capabilities) Text("This server does not support remote login-session management.", style = MaterialTheme.typography.bodySmall)
                    OutlinedButton(onClick = model.session::signOut) { Text("Sign out and remove local data") }
                }
            }
        }
    }
    if (sessionsOpen && !login) LoginSessions(state, model.session) { sessionsOpen = false }
    if (devicesOpen && !login) EncryptionDevices(state, model.session) { devicesOpen = false }
    state.certificate?.let { fingerprint ->
        AlertDialog(onDismissRequest = {}, title = { Text(if (state.changedCertificate) "Server certificate changed" else "Trust this server?") },
            text = { Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("${state.endpoint?.host}:${state.endpoint?.port}")
                Text("SHA-256\n$fingerprint", style = MaterialTheme.typography.bodySmall)
                Text("Confirm the exact fingerprint with the operator. Accepting trusts this certificate for this endpoint, including its hostname.")
            } },
            confirmButton = { TextButton(onClick = model.session::acceptCertificate) { Text("Trust exact certificate") } },
            dismissButton = { TextButton(onClick = model.session::signOut) { Text("Cancel") } })
    }
}
@Composable private fun Login(state: SessionUi, session: SessionCoordinator, suggestedHost: String) {
    var host by rememberSaveable { mutableStateOf(state.endpoint?.host ?: suggestedHost) }
    var port by rememberSaveable { mutableStateOf(state.endpoint?.port?.toString() ?: "6473") }
    var username by rememberSaveable { mutableStateOf(state.endpoint?.username ?: "") }
    // Password deliberately absent from saved state, preferences and ViewModel.
    var password by remember { mutableStateOf("") }
    LazyColumn(Modifier.fillMaxSize().imePadding().padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item { Text("Your communities, wherever you are.", style = MaterialTheme.typography.headlineSmall) }
        item { Text("Connect directly to your OmaChat server. Your desktop can stay off.") }
        item { OutlinedTextField(host, { host = it }, label = { Text("Server hostname") }, singleLine = true, modifier = Modifier.fillMaxWidth()) }
        item { OutlinedTextField(port, { port = it.filter(Char::isDigit).take(5) }, label = { Text("TLS port") }, singleLine = true, modifier = Modifier.fillMaxWidth()) }
        item { OutlinedTextField(username, { username = it }, label = { Text("Username") }, singleLine = true, modifier = Modifier.fillMaxWidth()) }
        item { OutlinedTextField(password, { password = it }, label = { Text("Password") }, visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth()) }
        item { Button(onClick = {
            session.login(host, port.toIntOrNull() ?: 0, username, password)
            password = ""
        }, enabled = state.state != ConnectionState.Connecting && state.state != ConnectionState.Authenticating) { Text("Connect") } }
    }
}
@Composable private fun Channels(state: SessionUi, session: SessionCoordinator, privateOnly: Boolean = false) {
    val voice by session.voice.collectAsStateWithLifecycle()
    val context = androidx.compose.ui.platform.LocalContext.current
    var voiceTarget by rememberSaveable { mutableLongStateOf(0L) }
    var transferTarget by rememberSaveable { mutableStateOf(false) }
    var permissionError by remember { mutableStateOf("") }
    val notifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    fun startVoice() {
        session.joinVoice(voiceTarget, transferTarget)
        if (android.os.Build.VERSION.SDK_INT >= 33 && context.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED)
            notifications.launch(android.Manifest.permission.POST_NOTIFICATIONS)
    }
    val microphone = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) startVoice() else permissionError = "Microphone permission is required to join voice. You can grant it in Android app settings."
    }
    fun requestVoice(id: Long, transfer: Boolean) {
        voiceTarget = id; transferTarget = transfer
        if (context.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) == android.content.pm.PackageManager.PERMISSION_GRANTED) startVoice()
        else microphone.launch(android.Manifest.permission.RECORD_AUDIO)
    }
    if (permissionError.isNotEmpty()) AlertDialog(onDismissRequest = { permissionError = "" }, title = { Text("Microphone permission") },
        text = { Text(permissionError) }, confirmButton = { TextButton(onClick = { permissionError = "" }) { Text("Close") } })
    if (voice.transferChannel != 0L) AlertDialog(onDismissRequest = session::cancelVoiceTransfer,
        title = { Text("Move voice to this phone?") }, text = { Text("Your other device will stop voice. This phone will use its microphone.") },
        confirmButton = { TextButton(onClick = { val id = voice.transferChannel; session.cancelVoiceTransfer(); requestVoice(id, true) }) { Text("Move voice here") } },
        dismissButton = { TextButton(onClick = session::cancelVoiceTransfer) { Text("Cancel") } })
    var grouping by remember { mutableStateOf(false) }
    var groupUsers by remember { mutableStateOf(setOf<Long>()) }
    if (grouping) AlertDialog(onDismissRequest = { grouping = false }, title = { Text("New encrypted group") }, text = {
        LazyColumn { items(state.snapshot.usersList.filter { it.id != state.snapshot.self.id }) { user ->
            Row { Checkbox(user.id in groupUsers, { checked -> groupUsers = if (checked) groupUsers + user.id else groupUsers - user.id }); Text(user.displayName.ifBlank { user.username }) }
        } }
    }, confirmButton = { TextButton(onClick = { session.createGroup(groupUsers.toList()); grouping = false }, enabled = groupUsers.size in 2..9) { Text("Create group") } }, dismissButton = { TextButton(onClick = { grouping = false }) { Text("Cancel") } })
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(16.dp)) {
        if ("e2e.v1" in state.capabilities) item { TextButton(onClick = { groupUsers = emptySet(); grouping = true }) { Text("New encrypted group") } }
        if (!privateOnly && state.snapshot.serversCount == 0) item {
            Text("No communities loaded. Log in to an account that has joined a community.", Modifier.padding(16.dp))
        }
        item { Text("Private conversations", Modifier.padding(12.dp), style = MaterialTheme.typography.titleLarge) }
        items(state.snapshot.channelsList.filter { it.type == ChannelType.CHANNEL_TYPE_DM || it.type == ChannelType.CHANNEL_TYPE_GROUP_DM }, key = { "dm/${it.id}" }) { channel ->
            ListItem(headlineContent = { Text(channel.name.ifBlank { "Conversation ${channel.id}" }) }, supportingContent = { Text("Encrypted · history begins with this device's key") }, modifier = Modifier.combinedClickableWithoutLongPress { session.select(channel.id) })
        }
        if ("e2e.v1" in state.capabilities) items(state.snapshot.usersList.filter { it.id != state.snapshot.self.id }, key = { "person/${it.id}" }) { user ->
            TextButton(onClick = { session.openDm(user.id) }) { Text("Message ${user.displayName.ifBlank { user.username }} privately") }
        }
        if (!privateOnly) state.snapshot.serversList.forEach { server ->
            item(key = "server/${server.id}") { Text(server.name, Modifier.padding(12.dp), style = MaterialTheme.typography.titleLarge) }
            items(state.snapshot.channelsList.filter { it.serverId == server.id }.sortedBy { it.position }, key = { "channel/${it.id}" }) { channel ->
                if (channel.type == ChannelType.CHANNEL_TYPE_CATEGORY) Text(channel.name, Modifier.padding(12.dp), style = MaterialTheme.typography.labelLarge)
                else ListItem(headlineContent = { Text(if (channel.type == ChannelType.CHANNEL_TYPE_VOICE) "Voice · ${channel.name}" else "# ${channel.name}") },
                    supportingContent = { Text(if (channel.type == ChannelType.CHANNEL_TYPE_VOICE) if ("voice.ownership.v1" !in state.capabilities) "Server does not support safe voice ownership" else if (voice.active) "Leave your current call to join another channel" else "Tap to join voice" else channel.topic) },
                    modifier = Modifier.combinedClickableWithoutLongPress { if (channel.type == ChannelType.CHANNEL_TYPE_TEXT) session.select(channel.id) else if (channel.type == ChannelType.CHANNEL_TYPE_VOICE && !voice.active) requestVoice(channel.id, false) })
            }
        }
    }
}
@OptIn(ExperimentalFoundationApi::class)
private fun Modifier.combinedClickableWithoutLongPress(click: () -> Unit) = combinedClickable(onClick = click)

@OptIn(ExperimentalFoundationApi::class)
@Composable private fun Conversation(state: SessionUi, channel: Channel, messages: LazyPagingItems<ChatMessage>, pending: List<PendingSend>, session: SessionCoordinator) {
    val coroutine = rememberCoroutineScope()
    val transfer by session.transfer.collectAsStateWithLifecycle()
    var draft by remember(channel.id) { mutableStateOf("") }
    var reply by remember(channel.id) { mutableStateOf<ChatMessage?>(null) }
    var action by remember { mutableStateOf<ChatMessage?>(null) }
    var editing by remember { mutableStateOf<ChatMessage?>(null) }
    var editText by remember { mutableStateOf("") }
    var safetyUser by remember(channel.id) { mutableStateOf<Long?>(null) }
    var safety by remember(channel.id) { mutableStateOf("") }
    LaunchedEffect(safetyUser, state.snapshot.lastSequence, state.encryptionRevision) { safetyUser?.let { safety = session.safetyNumber(it) } }
    var attachment by remember(channel.id) { mutableStateOf<android.net.Uri?>(null) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { attachment = it }
    var saving by remember { mutableStateOf<Pair<ChatMessage, Attachment>?>(null) }
    val saver = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        saving?.let { (message, file) -> if (uri != null) session.download(message, file, uri) }; saving = null
    }
    val list = rememberLazyListState()
    LaunchedEffect(channel.id) { draft = session.draftText(channel.id) }
    LaunchedEffect(channel.id, messages.itemSnapshotList.items.firstOrNull()?.id, state.state) {
        val loaded = messages.itemSnapshotList
        val newest = (if (loaded.size > 0) loaded[0] else null) ?: return@LaunchedEffect
        snapshotFlow { list.layoutInfo.visibleItemsInfo.any { it.key == "message/${newest.id}" } }
            .distinctUntilChanged().collect { visible ->
                if (visible && state.state == ConnectionState.Connected) session.markRead(newest)
            }
    }
    Column(Modifier.fillMaxSize().imePadding()) {
        if (channel.type == ChannelType.CHANNEL_TYPE_DM || channel.type == ChannelType.CHANNEL_TYPE_GROUP_DM) {
            Text("Encrypted · this device cannot read messages sent before its key was added.", Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.labelSmall)
            if (channel.recipientIdsList.any { it in session.changedKeys() }) Text("Device keys changed. Review before sending.", Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
            Row { channel.recipientIdsList.filter { it != state.snapshot.self.id }.forEach { user ->
                TextButton(onClick = { safetyUser = user }) { Text("Verify user $user") }
            } }
        }
        LazyColumn(Modifier.weight(1f).fillMaxWidth(), state = list, reverseLayout = true, contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            items(pending.reversed(), key = { "pending/${it.operation}" }) { send ->
                Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
                    Column(Modifier.fillMaxWidth().padding(12.dp)) {
                        Text(SendMessageRequest.parseFrom(send.wire).let { if (it.encrypted.isEmpty) it.content else "Encrypted pending message" })
                        Text(send.state + if (send.error.isNotEmpty()) " · ${send.error}" else "", style = MaterialTheme.typography.labelSmall)
                        Row {
                            if (send.state == "Failed" || send.state == "Uncertain") TextButton(onClick = { session.retry(send.operation) }) { Text("Retry") }
                            if (send.state != "Sending") TextButton(onClick = { session.cancel(send.operation) }) { Text("Remove pending send") }
                        }
                    }
                }
            }
            if (messages.itemCount == 0 && !state.loadingHistory) item { Text("No messages cached. Send the first message or load history when connected.") }
            items(count = messages.itemCount, key = messages.itemKey { "message/${it.id}" }) { index ->
                val message = messages[index]
                if (message == null) { Text("Loading cached message…"); return@items }
                Column(Modifier.fillMaxWidth().combinedClickable(onClick = {}, onLongClick = { action = message }).padding(vertical = 6.dp)) {
                    val author = state.snapshot.usersList.firstOrNull { it.id == message.authorId }
                    Text(author?.displayName?.ifBlank { author.username } ?: "User ${message.authorId}", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
                    if (message.replyTo != 0L) Text("Reply to ${message.replyTo}", style = MaterialTheme.typography.labelSmall)
                    Text(session.messageText(message))
                    session.historicalObservation(message)?.let { observed ->
                        Text("Sender key no longer registered · authenticated here on ${java.text.DateFormat.getDateTimeInstance().format(java.util.Date(observed))} using the then-observed directory. Identity was not necessarily verified.",
                            style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error)
                    }
                    if (message.editedAt != 0L) Text("Edited", style = MaterialTheme.typography.labelSmall)
                    message.attachmentsList.forEach { file ->
                        val metadata = session.messageFiles(message).firstOrNull { it.attachmentId == file.id }
                        TextButton(onClick = { saving = message to file; saver.launch(metadata?.filename ?: file.filename) }, enabled = transfer?.active != true) { Text("Save ${metadata?.filename ?: file.filename} (${metadata?.size ?: file.size} bytes)") }
                    }
                    Row { message.reactionsList.forEach { reaction ->
                        TextButton(onClick = { session.react(message, reaction.emoji, !reaction.me) }) { Text("${reaction.emoji} ${reaction.count}") }
                    } }
                }
            }
            if (state.hasMore && messages.itemCount > 0) item { TextButton(onClick = { session.older() }, enabled = !state.loadingHistory) { Text("Load older messages") } }
            if (state.loadingHistory) item { LinearProgressIndicator(Modifier.fillMaxWidth()) }
        }
        if (reply != null) Row(Modifier.padding(horizontal = 16.dp)) {
            Text("Replying to ${reply!!.id}", Modifier.weight(1f)); TextButton(onClick = { reply = null }) { Text("Cancel") }
        }
        Row(Modifier.padding(horizontal = 16.dp)) {
            TextButton(onClick = { picker.launch(arrayOf("*/*")) }, enabled = state.state == ConnectionState.Connected) { Text("Attach file") }
            if (attachment != null) TextButton(onClick = { attachment = null }) { Text("Remove attachment") }
        }
        val canSend = state.state == ConnectionState.Connected && (channel.effectivePermissions and (1L shl 1)) != 0L
        Row(Modifier.fillMaxWidth().padding(12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(draft, { draft = it.take(4000); coroutine.launch { session.saveDraft(channel.id, draft) } },
                label = { Text("Message #${channel.name}") }, modifier = Modifier.weight(1f), maxLines = 5)
            Button(onClick = {
                val sentText = draft; val sentReply = reply; val sentFile = attachment
                session.send(sentText, sentReply?.id ?: 0, sentFile) {
                    if (draft == sentText && reply == sentReply && attachment == sentFile) {
                        draft = ""; reply = null; attachment = null
                    }
                }
            }, enabled = canSend && !state.composing && transfer?.active != true && (draft.isNotBlank() || attachment != null)) { Text("Send") }
        }
        if (!canSend) Text(if (state.state != ConnectionState.Connected) "Reconnect to send. Your draft stays on this phone." else "You do not have permission to send here.", Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.labelSmall)
    }
    safetyUser?.let { user ->
        AlertDialog(onDismissRequest = { safetyUser = null }, title = { Text("Encryption safety number") }, text = {
            Column {
                Text(safety)
                Text("Compare this number with user $user through a trusted channel. Both devices must show the same number.")
                Text("${session.encryptionKeys(user).size} devices · " + if (session.verifiedKeys(user)) "Verified" else "Unverified")
            }
        }, confirmButton = { TextButton(onClick = { session.verifyKeys(user, safety); safetyUser = null }, enabled = safety.isNotEmpty()) { Text("Numbers match") } },
            dismissButton = { TextButton(onClick = { session.acceptKeys(user, safety); safetyUser = null }) { Text("Accept unverified keys") } })
    }
    action?.let { message ->
        AlertDialog(onDismissRequest = { action = null }, title = { Text("Message actions") }, text = {
            Column {
                TextButton(onClick = { reply = message; action = null }) { Text("Reply") }
                TextButton(onClick = { session.react(message, "👍", true); action = null }) { Text("React 👍") }
                if (message.authorId == state.snapshot.self.id) {
                    TextButton(onClick = { editing = message; editText = session.messageText(message); action = null }) { Text("Edit") }
                    TextButton(onClick = { session.delete(message); action = null }) { Text("Delete") }
                }
            }
        }, confirmButton = { TextButton(onClick = { action = null }) { Text("Close") } })
    }
    editing?.let { message ->
        AlertDialog(onDismissRequest = { editing = null }, title = { Text("Edit message") }, text = { OutlinedTextField(editText, { editText = it.take(4000) }, label = { Text("Message") }) },
            confirmButton = { TextButton(onClick = { session.edit(message, editText); editing = null }) { Text("Save") } },
            dismissButton = { TextButton(onClick = { editing = null }) { Text("Cancel") } })
    }
}
