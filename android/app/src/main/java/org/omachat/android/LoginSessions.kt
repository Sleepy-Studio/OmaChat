package org.omachat.android

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import omachat.proto.Network.LoginSession
import java.text.DateFormat
import java.util.Date

@Composable internal fun LoginSessions(state: SessionUi, session: SessionCoordinator, dismiss: () -> Unit) {
    var selected by remember { mutableStateOf<LoginSession?>(null) }
    val connected = state.state == ConnectionState.Connected && "sessions.manage.v1" in state.capabilities
    val enabled = connected && !state.sessionsBusy
    AlertDialog(onDismissRequest = dismiss, title = { Text("Login sessions") }, text = {
        LazyColumn(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            item { Text("Each login has a separate session. Revoking a session signs out its connections and stops its voice session.") }
            item { AnimatedVisibility(state.sessionsBusy) { LinearProgressIndicator(Modifier.fillMaxWidth()) } }
            if (!connected) item { Text("Connect to refresh or revoke. This list is a last-observed snapshot.") }
            if (state.error.isNotEmpty()) item { Text(state.error, color = MaterialTheme.colorScheme.error) }
            if (state.loginSessions.isEmpty() && !state.sessionsBusy) item { Text("Refresh while connected to load your account's sessions.") }
            items(state.loginSessions, key = { it.id }) { login ->
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(if (login.current) "Current login" else "Other login", style = MaterialTheme.typography.titleSmall)
                    SelectionContainer { Text("Session ${login.id}", style = MaterialTheme.typography.bodySmall) }
                    Text(if (login.connected) "Connected when refreshed" else "Disconnected when refreshed", style = MaterialTheme.typography.labelSmall)
                    if (login.createdAt > 0) Text("Created ${DateFormat.getDateTimeInstance().format(Date(login.createdAt))}", style = MaterialTheme.typography.bodySmall)
                    if (login.expiresAt > 0) Text("Expires ${DateFormat.getDateTimeInstance().format(Date(login.expiresAt))}", style = MaterialTheme.typography.bodySmall)
                    if (login.current) Text("Use Sign out in Settings to remove this login and its local data.", style = MaterialTheme.typography.bodySmall)
                    else TextButton(onClick = { selected = login }, enabled = enabled) { Text("Revoke session ${login.id}") }
                    HorizontalDivider()
                }
            }
            if (state.sessionsBefore != 0L) item {
                TextButton(onClick = { session.refreshLoginSessions(state.sessionsBefore) }, enabled = enabled) { Text("Older sessions") }
            }
            item { Text("Encryption keys and previously downloaded messages stay on the other installation. Use Encryption devices to revoke its key separately. Device names are unavailable.", style = MaterialTheme.typography.bodySmall) }
        }
    }, confirmButton = { TextButton(onClick = dismiss) { Text("Done") } },
        dismissButton = { TextButton(onClick = { session.refreshLoginSessions() }, enabled = enabled) { Text("Refresh") } })
    selected?.let { login ->
        AlertDialog(onDismissRequest = { selected = null }, title = { Text("Revoke login session?") },
            text = { Text("Session ${login.id} will lose access and must log in again. Its active voice session will stop. Saved messages and encryption keys remain on that installation.") },
            confirmButton = { TextButton(onClick = { session.revokeLoginSession(login.id); selected = null }, enabled = enabled) { Text("Revoke session") } },
            dismissButton = { TextButton(onClick = { selected = null }) { Text("Cancel") } })
    }
}
