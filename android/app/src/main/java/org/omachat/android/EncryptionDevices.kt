package org.omachat.android

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.google.protobuf.ByteString
import java.security.MessageDigest
import java.text.DateFormat
import java.util.Date

internal fun deviceFingerprint(key: ByteString): String = MessageDigest.getInstance("SHA-256")
    .digest(key.toByteArray()).joinToString("") { "%02x".format(it) }.chunked(8).joinToString(" ")

@Composable internal fun EncryptionDevices(state: SessionUi, session: SessionCoordinator, dismiss: () -> Unit) {
    var selected by remember { mutableStateOf<ByteString?>(null) }
    val connected = state.state == ConnectionState.Connected
    AlertDialog(onDismissRequest = dismiss, title = { Text("Encryption devices") }, text = {
        LazyColumn(verticalArrangement = Arrangement.spacedBy(16.dp)) {
            item { Text("These keys receive new private messages for your account. Compare the full SHA-256 fingerprint with the device you intend to remove.") }
            if (state.error.isNotEmpty()) item { Text(state.error, color = MaterialTheme.colorScheme.error) }
            if (state.deviceRevoked) item { Text("This installation's key is revoked. New encrypted sends are blocked. Sign out and log in to create a new identity.", color = MaterialTheme.colorScheme.error) }
            if (!connected) item { Text("Connect to refresh this list or revoke a key. The displayed list may be out of date.") }
            if (state.devicesBusy) item { LinearProgressIndicator(Modifier.fillMaxWidth()) }
            if (state.ownDevices.isEmpty() && !state.devicesBusy) item { Text("No device keys loaded. Refresh while connected to check the directory.") }
            items(state.ownDevices, key = { deviceFingerprint(it.publicKey) }) { device ->
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(if (device.publicKey == state.localDeviceKey) "This installation" else "Other encryption device", style = MaterialTheme.typography.titleSmall)
                    SelectionContainer { Text(deviceFingerprint(device.publicKey), style = MaterialTheme.typography.bodySmall) }
                    if (device.createdAt > 0) Text("Registered ${DateFormat.getDateTimeInstance().format(Date(device.createdAt))}", style = MaterialTheme.typography.labelSmall)
                    TextButton(onClick = { selected = device.publicKey }, enabled = connected && !state.devicesBusy) { Text("Revoke key") }
                    HorizontalDivider()
                }
            }
            item { Text("Revoking a key stops future messages targeting it. It does not sign that device out, erase old messages, or revoke its login session.", style = MaterialTheme.typography.bodySmall) }
        }
    }, confirmButton = { TextButton(onClick = dismiss) { Text("Done") } },
        dismissButton = { TextButton(onClick = session::refreshDevices, enabled = connected && !state.devicesBusy) { Text("Refresh") } })
    selected?.let { key ->
        val current = key == state.localDeviceKey
        AlertDialog(onDismissRequest = { selected = null }, title = { Text(if (current) "Revoke this installation?" else "Revoke encryption key?") },
            text = { Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                SelectionContainer { Text(deviceFingerprint(key), style = MaterialTheme.typography.bodySmall) }
                Text(if (current) "This installation will lose the ability to send new private messages. Its saved identity stays available for old recipient wraps until sign-out."
                    else "New private messages will no longer be encrypted for this key. The other device's login session will remain active.")
                Text("Contacts must review the changed device directory before sending again.")
            } }, confirmButton = { TextButton(onClick = { session.revokeDevice(key); selected = null }, enabled = connected && !state.devicesBusy && state.ownDevices.any { it.publicKey == key }) { Text("Revoke key") } },
            dismissButton = { TextButton(onClick = { selected = null }) { Text("Cancel") } })
    }
}
