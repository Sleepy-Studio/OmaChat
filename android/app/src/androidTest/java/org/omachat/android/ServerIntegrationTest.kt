package org.omachat.android

import androidx.test.core.app.ApplicationProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.*

/** Runs only against the disposable server started by tools/integration.py. */
class ServerIntegrationTest {
    @Test fun androidSessionExchangesMessagesWithDesktopAndPersistsOutbox() = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val port = args.getString("serverPort")?.toInt() ?: error("Run android/tools/integration.py")
        val host = args.getString("serverHost") ?: "10.0.2.2"
        val expectedFingerprint = args.getString("serverFingerprint") ?: error("Missing fixture fingerprint")
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val session = app.session
        session.foreground(true)
        try {
            session.login(host, port, "androidfixture", "fixture-password-123")
            val challenge = withTimeout(20000) { session.ui.first { it.state == ConnectionState.CertificateConfirmationRequired } }
            assertEquals(expectedFingerprint, challenge.certificate)
            assertFalse(challenge.changedCertificate)
            session.acceptCertificate()
            withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
            session.login(host, port, "androidfixture", "fixture-password-123")
            val connected = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertTrue("messages.idempotency.v1" in connected.capabilities)
            val channel = connected.snapshot.channelsList.single { it.name == "general" && it.type == ChannelType.CHANNEL_TYPE_TEXT }
            session.select(channel.id)
            val account = connected.endpoint!!.key
            withTimeout(10000) { session.dao.messages(account, channel.id).first { rows ->
                rows.any { ChatMessage.parseFrom(it.wire).content == "desktop-to-android" }
            } }
            session.saveDraft(channel.id, "persisted draft")
            assertEquals("persisted draft", session.dao.draft(account, channel.id)?.text)
            session.send("android-to-desktop")
            val sent = withTimeout(10000) { session.dao.messages(account, channel.id).first { rows ->
                rows.any { ChatMessage.parseFrom(it.wire).content == "android-to-desktop" }
            } }.map { ChatMessage.parseFrom(it.wire) }.single { it.content == "android-to-desktop" }
            assertEquals(connected.snapshot.self.id, sent.authorId)
            session.markRead(sent)
            withTimeout(10000) { session.ui.first { it.state == ConnectionState.Connected } }
            session.foreground(false)
            withTimeout(5000) { session.ui.first { it.state == ConnectionState.Offline } }
            // More events than the transport queue: replay must never block sync replies.
            val peerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
            val peer = ControlConnection.connect(host, port, expectedFingerprint, peerScope)
            try {
                peer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
                peer.call(Envelope.newBuilder().setLogin(LoginRequest.newBuilder()
                    .setUsername("androidfixture").setPassword("fixture-password-123")))
                repeat(100) { index ->
                    delay(510) // Respect the real server's two-message/second limit.
                    peer.call(Envelope.newBuilder().setSendMessage(SendMessageRequest.newBuilder()
                        .setChannelId(channel.id).setContent("offline replay $index")))
                }
                session.foreground(true)
                withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
                withTimeout(10000) { session.dao.messages(account, channel.id).first { rows ->
                    rows.any { ChatMessage.parseFrom(it.wire).content == "offline replay 99" }
                } }
                // Older history is fetched independently of the structural sync snapshot.
                session.loadHistory(channel.id, session.dao.messages(account, channel.id).first().last().id)
                session.loadHistory(channel.id, session.dao.messages(account, channel.id).first().last().id)
                assertTrue(session.dao.messages(account, channel.id).first().any { it.id == sent.id })
            } finally { peer.close(); peerScope.cancel() }
            assertEquals("persisted draft", session.dao.draft(account, channel.id)?.text)
            session.foreground(false)
            withTimeout(5000) { session.ui.first { it.state == ConnectionState.Offline } }
            session.foreground(true)
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            // This message is older than the newest 50; short replay must retain
            // reconciled older pages instead of clearing all history again.
            assertTrue(session.dao.messages(account, channel.id).first().any { it.id == sent.id })
            ActivityScenario.launch(MainActivity::class.java).use { activity ->
                activity.recreate()
                activity.onActivity { screen -> assertSame(session, (screen.application as OmaChatApp).session) }
                assertEquals(ConnectionState.Connected, session.ui.value.state)
                assertEquals(channel.id, session.ui.value.selected)
            }
        } finally {
            session.signOut()
            withTimeout(10000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
            session.foreground(false)
        }
    }
}
