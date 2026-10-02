package org.omachat.android

import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.ControlConnection
import org.omachat.protocol.ServerError

/** Disposable fixture only. Does not touch hosted or physical-device accounts. */
class LoginSessionTest {
    @Test fun settingsRevokesAnotherLoginAndRetainsEncryptionKey(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val host = args.getString("serverHost")!!
        val port = args.getString("serverPort")!!.toInt()
        val fingerprint = args.getString("serverFingerprint")!!
        val peerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val remote = ControlConnection.connect(host, port, fingerprint, peerScope)
        val session = ApplicationProvider.getApplicationContext<OmaChatApp>().session
        val identity = NativeCrypto.generate()
        val key = ByteString.copyFrom(NativeCrypto.publicKey(identity)!!)
        identity.fill(0)
        try {
            val hello = remote.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            assertTrue("sessions.manage.v1" in hello.helloReply.capabilitiesList)
            val login = remote.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername("sessionfixture").setPassword("fixture-sessions-password-123"))).authResult
            remote.call(Envelope.newBuilder().setCreateServer(CreateServerRequest.newBuilder().setName("Session fixture")))
            remote.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(key)))
            session.foreground(true)
            session.login(host, port, "sessionfixture", "fixture-sessions-password-123")
            val first = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected || it.state == ConnectionState.CertificateConfirmationRequired } }
            if (first.state == ConnectionState.CertificateConfirmationRequired) {
                assertEquals(fingerprint, first.certificate)
                session.acceptCertificate()
                withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
                session.login(host, port, "sessionfixture", "fixture-sessions-password-123")
                withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            }
            session.select(0)
            ActivityScenario.launch(MainActivity::class.java).use {
                clickText("Settings")
                clickText("Login sessions")
                withTimeout(10000) { session.ui.first { !it.sessionsBusy && it.loginSessions.size == 2 } }
                visibleText("Current login")
                visibleText("Session ${login.sessionId}")
                assertEquals(1, session.ui.value.loginSessions.count { it.current })
                val current = session.ui.value.loginSessions.single { it.current }.id
                assertNotEquals(login.sessionId, current)
                clickText("Revoke session ${login.sessionId}")
                visibleText("Revoke login session?")
                clickText("Cancel")
                assertEquals(2, session.ui.value.loginSessions.size)
                assertTrue(remote.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).hasSyncState())
                clickText("Revoke session ${login.sessionId}")
                clickText("Revoke session", exact = true)
                withTimeout(10000) { session.ui.first { !it.sessionsBusy && it.loginSessions.size == 1 } }
                assertEquals(current, session.ui.value.loginSessions.single().id)
                assertEquals(ConnectionState.Connected, session.ui.value.state)
                assertTrue(session.ui.value.error.isEmpty())
                clickText("Done")
                val probe = ControlConnection.connect(host, port, fingerprint, peerScope)
                try {
                    probe.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
                    try {
                        probe.call(Envelope.newBuilder().setResume(ResumeRequest.newBuilder().setSessionId(login.sessionId).setAccessToken(login.accessToken)))
                        fail("Revoked access token resumed")
                    } catch (expected: ServerError) { assertEquals(4, expected.code) }
                    try {
                        probe.call(Envelope.newBuilder().setRefresh(RefreshRequest.newBuilder().setRefreshToken(login.refreshToken)))
                        fail("Revoked refresh token worked")
                    } catch (expected: ServerError) { assertEquals(4, expected.code) }
                } finally { probe.close() }
                session.refreshDevices()
                withTimeout(10000) { session.ui.first { !it.devicesBusy && it.ownDevices.any { device -> device.publicKey == key } } }
                // Local sign-out remains the only UI route for this login; API guard too.
                session.revokeLoginSession(session.ui.value.loginSessions.single().id)
                withTimeout(5000) { session.ui.first { it.error.contains("Use Sign out") } }
                assertEquals(ConnectionState.Connected, session.ui.value.state)
                val localKey = session.ui.value.localDeviceKey
                val account = session.ui.value.endpoint!!.key
                val channel = session.ui.value.snapshot.channelsList.first { it.type == ChannelType.CHANNEL_TYPE_TEXT }.id
                session.saveDraft(channel, "Keep this draft after remote revocation")
                val controller = ControlConnection.connect(host, port, fingerprint, peerScope)
                try {
                    controller.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
                    controller.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder().setUsername("sessionfixture").setPassword("fixture-sessions-password-123")))
                    val current = session.ui.value.loginSessions.single().id
                    controller.call(Envelope.newBuilder().setRevokeLoginSession(RevokeLoginSessionRequest.newBuilder().setSessionId(current)))
                    withTimeout(20000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
                    assertEquals("Keep this draft after remote revocation", session.dao.draft(account, channel)?.text)
                    assertEquals(localKey, session.ui.value.localDeviceKey)
                    session.login(host, port, "sessionfixture", "fixture-sessions-password-123")
                    withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
                    assertEquals(localKey, session.ui.value.localDeviceKey)
                    assertEquals("Keep this draft after remote revocation", session.dao.draft(account, channel)?.text)
                } finally { controller.close() }
            }
        } finally {
            remote.close(); peerScope.cancel()
            session.signOut()
            withTimeout(10000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
            session.foreground(false)
        }
    }

    private suspend fun visibleText(text: String, exact: Boolean = false): android.view.accessibility.AccessibilityNodeInfo = withTimeout(10000) {
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        while (true) {
            fun find(node: android.view.accessibility.AccessibilityNodeInfo?): android.view.accessibility.AccessibilityNodeInfo? {
                node ?: return null
                val value = node.text?.toString()
                if (if (exact) value == text else value?.contains(text) == true) return node
                for (index in 0 until node.childCount) find(node.getChild(index))?.let { return it }
                return null
            }
            find(automation.rootInActiveWindow)?.let { return@withTimeout it }
            delay(100)
        }
        @Suppress("UNREACHABLE_CODE") error("Text not found: $text")
    }
    private suspend fun clickText(text: String, exact: Boolean = false) {
        var node: android.view.accessibility.AccessibilityNodeInfo? = visibleText(text, exact)
        while (node != null && !node.isClickable) node = node.parent
        assertTrue("Could not click $text", node?.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK) == true)
    }
}
