package org.omachat.android

import androidx.test.core.app.ApplicationProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.ControlConnection

/** Separate fixture phases let the real desktop daemon send between processes. */
class EncryptedConversationTest {
    @Test fun prepareIndependentAndroidDevice(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val host = args.getString("serverHost")!!
        val port = args.getString("serverPort")!!.toInt()
        val peerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val peer = ControlConnection.connect(host, port, args.getString("serverFingerprint")!!, peerScope)
        try {
            peer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            peer.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername("androidcrypto").setPassword("fixture-crypto-password-123")))
            peer.call(Envelope.newBuilder().setJoinInvite(JoinInviteRequest.newBuilder().setToken(args.getString("fixtureInvite")!!)))
        } finally { peer.close(); peerScope.cancel() }
        val groupScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val groupPeer = ControlConnection.connect(host, port, args.getString("serverFingerprint")!!, groupScope)
        val groupKey = NativeCrypto.generate()
        try {
            groupPeer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            groupPeer.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder().setUsername("cryptogroup").setPassword("fixture-group-password-123")))
            groupPeer.call(Envelope.newBuilder().setJoinInvite(JoinInviteRequest.newBuilder().setToken(args.getString("fixtureInvite")!!)))
            groupPeer.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(com.google.protobuf.ByteString.copyFrom(NativeCrypto.publicKey(groupKey)!!))))
        } finally { groupKey.fill(0); groupPeer.close(); groupScope.cancel() }
        val session = ApplicationProvider.getApplicationContext<OmaChatApp>().session
        session.foreground(true)
        session.login(host, port, "androidcrypto", "fixture-crypto-password-123")
        val first = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected || it.state == ConnectionState.CertificateConfirmationRequired } }
        if (first.state == ConnectionState.CertificateConfirmationRequired) {
            assertEquals(args.getString("serverFingerprint"), first.certificate)
            session.acceptCertificate()
            withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
            session.login(host, port, "androidcrypto", "fixture-crypto-password-123")
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
        }
        assertEquals(1, session.encryptionKeys(session.ui.value.snapshot.self.id).size)
        // Exercise the real Compose settings route and cancellation without
        // revoking the key needed by subsequent interoperability phases.
        ActivityScenario.launch(MainActivity::class.java).use {
            clickText("Settings")
            clickText("Encryption devices")
            visibleText("This installation")
            visibleText(deviceFingerprint(session.encryptionKeys(session.ui.value.snapshot.self.id).single()))
            clickText("Revoke key")
            visibleText("Revoke this installation?")
            clickText("Cancel")
            assertFalse(session.ui.value.deviceRevoked)
            clickText("Done")
        }
        session.foreground(false)
        withTimeout(5000) { session.ui.first { it.state == ConnectionState.Offline } }
    }
    @Test fun decryptDesktopAndSendEncryptedReplyAndEdit(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val session = ApplicationProvider.getApplicationContext<OmaChatApp>().session
        session.foreground(true)
        val connected = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
        val channel = connected.snapshot.channelsList.single { it.type == ChannelType.CHANNEL_TYPE_DM }
        session.select(channel.id)
        val account = connected.endpoint!!.key
        val received = withTimeout(15000) { session.dao.messages(account, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "desktop encrypted secret" }
        } }.map { ChatMessage.parseFrom(it.wire) }.single { session.messageText(it) == "desktop encrypted secret" }
        assertTrue(received.content.isEmpty())
        assertFalse(received.encrypted.isEmpty)
        val other = channel.recipientIdsList.single { it != connected.snapshot.self.id }
        assertNotEquals(session.encryptionKeys(other).single(), session.encryptionKeys(connected.snapshot.self.id).single())
        assertEquals(args.getString("desktopSafety"), session.safetyNumber(other))
        session.verifyKeys(other, session.safetyNumber(other))
        withTimeout(5000) { while (!session.verifiedKeys(other)) delay(50) }
        session.send("android encrypted secret", received.id)
        val sent = withTimeout(15000) { session.dao.messages(account, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted secret" }
        } }.map { ChatMessage.parseFrom(it.wire) }.single { session.messageText(it) == "android encrypted secret" }
        assertTrue(sent.content.isEmpty())
        assertFalse(sent.encrypted.toStringUtf8().contains("android encrypted secret"))
        assertEquals(received.id, sent.replyTo)
        delay(600)
        session.edit(sent, "android encrypted edited")
        withTimeout(15000) { session.dao.messages(account, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted edited" }
        } }
        val incomingFile = withTimeout(15000) { session.dao.messages(account, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "desktop encrypted file" }
        } }.map { ChatMessage.parseFrom(it.wire) }.single { session.messageText(it) == "desktop encrypted file" }
        val files = session.messageFiles(incomingFile)
        assertEquals("desktop-private.bin", files.single().filename)
        assertTrue(incomingFile.attachmentsList.single().filename.endsWith(".enc"))
        assertFalse(incomingFile.attachmentsList.single().filename.contains("desktop-private"))
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val downloaded = java.io.File(app.cacheDir, "integration-downloaded.bin")
        downloaded.delete()
        session.download(incomingFile, incomingFile.attachmentsList.single(), android.net.Uri.fromFile(downloaded))
        withTimeout(15000) { while (!downloaded.exists() || downloaded.length() != 200000L) delay(50) }
        assertArrayEquals(ByteArray(200000) { (it * 13).toByte() }, downloaded.readBytes())
        val outgoing = java.io.File(app.cacheDir, "android-private.bin")
        outgoing.writeBytes(ByteArray(131072) { (it * 7).toByte() })
        session.send("android encrypted file", attachment = android.net.Uri.fromFile(outgoing))
        withTimeout(15000) { session.dao.messages(account, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted file" }
        } }
        downloaded.delete(); outgoing.delete()
        val group = session.ui.value.snapshot.channelsList.single { it.type == ChannelType.CHANNEL_TYPE_GROUP_DM }
        session.select(group.id)
        withTimeout(15000) { session.dao.messages(account, group.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "desktop encrypted group" }
        } }
        session.send("android encrypted group")
        withTimeout(15000) { session.dao.messages(account, group.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted group" }
        } }
        val peerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val peer = ControlConnection.connect(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(), args.getString("serverFingerprint")!!, peerScope)
        val newDevice = NativeCrypto.generate()
        val newPublic = com.google.protobuf.ByteString.copyFrom(NativeCrypto.publicKey(newDevice)!!)
        try {
            peer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            peer.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder().setUsername("androidfixture").setPassword("fixture-password-123")))
            peer.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(newPublic)))
            withTimeout(10000) { while (other !in session.changedKeys()) delay(50) }
            assertFalse(session.verifiedKeys(other))
            val operation = java.util.UUID.randomUUID()
            val operationBytes = java.nio.ByteBuffer.allocate(16).putLong(operation.mostSignificantBits).putLong(operation.leastSignificantBits).array()
            val stale = SendMessageRequest.newBuilder().setChannelId(channel.id).setEncrypted(sent.encrypted)
                .setOperationId(com.google.protobuf.ByteString.copyFrom(operationBytes)).build()
            session.dao.put(PendingSend(account, operation.toString(), channel.id, stale.toByteArray()))
            session.retry(operation.toString())
            withTimeout(10000) { while (session.dao.pending(account, operation.toString())?.state != "Failed") delay(50) }
            assertTrue(session.dao.pending(account, operation.toString())!!.error.contains("changed device keys"))
            session.cancel(operation.toString())
            assertNotEquals(args.getString("desktopSafety"), session.safetyNumber(other))
            session.select(channel.id)
            session.send("must stay blocked")
            withTimeout(10000) { session.ui.first { it.error.contains("Device keys changed") } }
            assertFalse(session.dao.messages(account, channel.id).first().any { session.messageText(ChatMessage.parseFrom(it.wire)) == "must stay blocked" })
            peer.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder().setPublicKey(newPublic)))
            withTimeout(10000) { while (session.encryptionKeys(other).size != 1) delay(50) }
            assertTrue(other in session.changedKeys())
        } finally { newDevice.fill(0); peer.close(); peerScope.cancel() }
        session.saveDraft(channel.id, "private persisted draft")
        assertEquals("", session.dao.draft(account, channel.id)?.text)
        assertEquals("private persisted draft", session.draftText(channel.id))
        // Leave wrapped installation identity/session in place for restart validation.
        session.foreground(false)
        withTimeout(5000) { session.ui.first { it.state == ConnectionState.Offline } }
    }
    @Test fun encryptedHistorySurvivesForceStop(): Unit = runBlocking {
        val session = ApplicationProvider.getApplicationContext<OmaChatApp>().session
        val offline = withTimeout(10000) { session.ui.first { it.state == ConnectionState.Offline } }
        val offlineChannel = offline.snapshot.channelsList.single { it.type == ChannelType.CHANNEL_TYPE_DM }
        assertTrue(session.dao.messages(offline.endpoint!!.key, offlineChannel.id).first().any {
            session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted edited"
        })
        assertEquals("private persisted draft", session.draftText(offlineChannel.id))
        session.foreground(true)
        val state = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
        val channel = state.snapshot.channelsList.single { it.type == ChannelType.CHANNEL_TYPE_DM }
        session.select(channel.id)
        assertEquals("private persisted draft", session.draftText(channel.id))
        withTimeout(15000) { session.dao.messages(state.endpoint!!.key, channel.id).first { rows ->
            rows.any { session.messageText(ChatMessage.parseFrom(it.wire)) == "android encrypted edited" }
        } }
        val other = channel.recipientIdsList.single { it != state.snapshot.self.id }
        assertFalse(session.verifiedKeys(other))
        assertTrue(other in session.changedKeys())
        session.verifyKeys(other, session.safetyNumber(other))
        withTimeout(5000) { while (!session.verifiedKeys(other)) delay(50) }
        assertEquals(InstrumentationRegistry.getArguments().getString("desktopSafety"), session.safetyNumber(other))
        // Revoking this installation must not be silently undone on reconnect.
        val args = InstrumentationRegistry.getArguments()
        val peerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val peer = ControlConnection.connect(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(), args.getString("serverFingerprint")!!, peerScope)
        try {
            peer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            peer.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder().setUsername("androidcrypto").setPassword("fixture-crypto-password-123")))
            val localKey = session.encryptionKeys(state.snapshot.self.id).single()
            val extraSecret = NativeCrypto.generate()
            val extraKey = try { com.google.protobuf.ByteString.copyFrom(NativeCrypto.publicKey(extraSecret)!!) } finally { extraSecret.fill(0) }
            peer.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(extraKey)))
            withTimeout(10000) { session.ui.first { it.ownDevices.size == 2 } }
            session.refreshDevices()
            withTimeout(10000) { session.ui.first { !it.devicesBusy && it.ownDevices.any { d -> d.publicKey == extraKey } } }
            assertEquals(localKey, session.ui.value.localDeviceKey)
            assertTrue(session.ui.value.ownDevices.all { it.createdAt > 0 })
            // A contact's key is never a target for this account's device action.
            val foreignKey = session.encryptionKeys(other).single()
            session.revokeDevice(foreignKey)
            withTimeout(10000) { session.ui.first { it.error.contains("no longer registered") && !it.devicesBusy } }
            assertEquals(listOf(foreignKey), session.encryptionKeys(other))
            session.revokeDevice(extraKey)
            withTimeout(10000) { session.ui.first { !it.devicesBusy && it.ownDevices.map { d -> d.publicKey } == listOf(localKey) } }
            assertFalse(session.ui.value.deviceRevoked)
            session.revokeDevice(localKey)
            withTimeout(10000) { session.ui.first { !it.devicesBusy && it.deviceRevoked && it.ownDevices.isEmpty() } }
            assertEquals(localKey, session.ui.value.localDeviceKey)
            session.foreground(false)
            withTimeout(5000) { session.ui.first { it.state == ConnectionState.Offline } }
            session.foreground(true)
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertTrue(session.encryptionKeys(state.snapshot.self.id).isEmpty())
            session.select(channel.id)
            session.send("must stay revoked")
            withTimeout(10000) { session.ui.first { it.error.contains("revoked") } }
        } finally { peer.close(); peerScope.cancel() }
        session.signOut()
        withTimeout(10000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
        session.foreground(false)
    }
    @Test fun nativeCryptoRejectsTamperingWrongContextAndBounds() {
        val a = NativeCrypto.generate(); val b = NativeCrypto.generate(); val c = NativeCrypto.generate()
        try {
            val body = E2EBody.newBuilder().setContent("private text").build().toByteArray()
            val keys = arrayOf(NativeCrypto.publicKey(a)!!, NativeCrypto.publicKey(b)!!)
            val sealed = NativeCrypto.seal(body, 7, 1, a, keys)!!
            assertArrayEquals(body, NativeCrypto.open(sealed, 7, 1, b))
            assertNull(NativeCrypto.open(sealed, 8, 1, b))
            assertNull(NativeCrypto.open(sealed, 7, 2, b))
            assertNull(NativeCrypto.open(sealed, 7, 1, c))
            sealed[2][0] = (sealed[2][0].toInt() xor 1).toByte()
            assertNull(NativeCrypto.open(sealed, 7, 1, b))
            assertNull(NativeCrypto.seal(body, 7, 1, a, emptyArray()))
            assertNull(NativeCrypto.publicKey(ByteArray(5)))
            runBlocking {
                val context = ApplicationProvider.getApplicationContext<OmaChatApp>()
                val account = java.util.UUID.randomUUID().toString()
                val binding = "$account/fixture/1"
                fun address(suffix: String) = java.security.MessageDigest.getInstance("SHA-256").digest("$binding/$suffix".toByteArray()).joinToString("") { "%02x".format(it) }
                val store = SecretStore(context)
                val crypto = EncryptedConversations(context)
                store.write(address("identity"), a)
                store.write(address("published"), byteArrayOf(1))
                store.write(address("known/1"), DeviceKeyList.newBuilder().addKeys(DeviceKey.newBuilder().setUserId(1).setPublicKey(com.google.protobuf.ByteString.copyFrom(keys[0]))).build().toByteArray())
                try {
                    crypto.restoreOffline(account, "fixture", 1, emptyList())
                    crypto.forget()
                    assertNull(store.read(address("identity")))
                    assertNull(store.read(address("published")))
                    store.write(address("identity"), ByteArray(5))
                    var rejected = false
                    try { crypto.restoreOffline(account, "fixture", 1, emptyList()) } catch (_: IllegalStateException) { rejected = true }
                    assertTrue(rejected)
                    assertEquals(5, store.read(address("identity"))!!.size)
                } finally { crypto.forget(); store.remove(address("known/1")) }
            }
            val context = ApplicationProvider.getApplicationContext<OmaChatApp>()
            val plain = java.io.File.createTempFile("native-test", ".plain", context.cacheDir)
            val cipher = java.io.File.createTempFile("native-test", ".enc", context.cacheDir)
            val back = java.io.File.createTempFile("native-test", ".back", context.cacheDir)
            try {
                for (size in listOf(0, 1, 65535, 65536, 65537, 131072)) {
                    val data = ByteArray(size) { (it * 13).toByte() }; plain.writeBytes(data)
                    val fileKey = NativeCrypto.encryptFile(plain.path, cipher.path)!!
                    try {
                        assertTrue(NativeCrypto.decryptFile(cipher.path, back.path, fileKey))
                        assertArrayEquals(data, back.readBytes())
                        cipher.appendBytes(byteArrayOf(1))
                        assertFalse(NativeCrypto.decryptFile(cipher.path, back.path, fileKey))
                        assertFalse(back.exists())
                    } finally { fileKey.fill(0) }
                }
            } finally { plain.delete(); cipher.delete(); back.delete() }
            assertEquals("60386 21733 18440 13865 34198 52190 70934 68099 73903 08503 78776 68350", NativeCrypto.safety(1, arrayOf(ByteArray(32) { 97 }, ByteArray(32) { 98 }), 2, arrayOf(ByteArray(32) { 99 })))
            assertEquals(NativeCrypto.safety(1, arrayOf(keys[0]), 2, arrayOf(keys[1])), NativeCrypto.safety(2, arrayOf(keys[1]), 1, arrayOf(keys[0])))
        } finally { a.fill(0); b.fill(0); c.fill(0) }
    }

    private suspend fun visibleText(text: String): android.view.accessibility.AccessibilityNodeInfo = try { withTimeout(10000) {
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        while (true) {
            // Compose's virtual node provider does not implement Android's
            // findAccessibilityNodeInfosByText. Walk its exposed node tree.
            fun find(node: android.view.accessibility.AccessibilityNodeInfo?): android.view.accessibility.AccessibilityNodeInfo? {
                node ?: return null
                if (node.text?.toString()?.contains(text) == true) return node
                for (index in 0 until node.childCount) find(node.getChild(index))?.let { return it }
                return null
            }
            val node = find(automation.rootInActiveWindow)
            if (node != null) return@withTimeout node
            delay(100)
        }
        @Suppress("UNREACHABLE_CODE") error("Text not found: $text")
    } } catch (e: TimeoutCancellationException) { throw AssertionError("UI did not expose: $text", e) }
    private suspend fun clickText(text: String) {
        var node: android.view.accessibility.AccessibilityNodeInfo? = visibleText(text)
        while (node != null && !node.isClickable) node = node.parent
        assertTrue("Could not click $text", node?.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK) == true)
    }
}
