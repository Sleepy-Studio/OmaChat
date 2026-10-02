package org.omachat.android

import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.*
import java.security.SecureRandom
import java.util.Base64
import java.util.UUID

/** Explicit opt-in only: registers a real test account and posts ONE marked
 * community message. Requires an operator-verified certificate and invite.
 * No production credentials are accepted or printed by this test. */
class HostedServerTest {
    @Test fun phoneJoinsAuthorizedInviteAndSendsRealMessage() = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        check(args.getString("allowHostedTest") == "yes") { "Hosted testing requires explicit authorization" }
        val host = args.getString("serverHost") ?: error("Missing selected server")
        val expected = args.getString("serverFingerprint") ?: error("Missing verified certificate")
        val invite = args.getString("inviteToken") ?: error("Missing authorized invite")
        val port = args.getString("serverPort")?.toInt() ?: 6473
        val username = "android_test_" + UUID.randomUUID().toString().take(8)
        val passwordBytes = ByteArray(32).also(SecureRandom()::nextBytes)
        val password = Base64.getEncoder().encodeToString(passwordBytes)
        passwordBytes.fill(0)
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val session = app.session
        session.foreground(true)
        var peer: ControlConnection? = null
        try {
            // First validate the phone-observed certificate through its trust flow.
            session.login(host, port, username, password)
            val challenge = withTimeout(20000) { session.ui.first { it.state == ConnectionState.CertificateConfirmationRequired || it.state == ConnectionState.LoginRequired } }
            if (challenge.certificate != null) {
                assertEquals(expected, challenge.certificate)
                session.acceptCertificate()
                withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
            }
            peer = ControlConnection.connect(host, port, expected, this)
            val hello = peer.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)
                .setClientVersion("android/0.1.0-dev authorized hosted test"))).helloReply
            assertTrue(hello.registrationOpen)
            val registered = peer.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername(username).setPassword(password).setDisplayName("Android connectivity test")))
            assertTrue(registered.hasAuthResult())
            val joined = peer.call(Envelope.newBuilder().setJoinInvite(JoinInviteRequest.newBuilder().setToken(invite)))
            assertTrue(joined.hasServer())
            session.login(host, port, username, password)
            val ready = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertEquals(registered.authResult.user.id, ready.snapshot.self.id)
            val channels = ready.snapshot.channelsList.filter {
                it.serverId == joined.server.id && it.type == ChannelType.CHANNEL_TYPE_TEXT && (it.effectivePermissions and 2L) != 0L
            }
            val channel = channels.firstOrNull { it.name.equals("general", true) } ?: channels.firstOrNull() ?: error("No permitted text channel in invite")
            session.select(channel.id)
            val text = "[Android connectivity test] Native OmaChat on Samsung SM-S908U: direct server login and message send. Test account: $username."
            session.send(text)
            val stored = withTimeout(15000) { session.dao.messages(ready.endpoint!!.key, channel.id).first { rows ->
                rows.any { ChatMessage.parseFrom(it.wire).content == text }
            } }.map { ChatMessage.parseFrom(it.wire) }.single { it.content == text }
            val history = peer.call(Envelope.newBuilder().setGetMessages(GetMessagesRequest.newBuilder().setChannelId(channel.id).setLimit(50))).messagePage
            assertEquals(1, history.messagesList.count { it.id == stored.id && it.content == text })
            assertEquals(registered.authResult.user.id, stored.authorId)
            // Keep the phone's Keystore-protected login so the user can try the app.
            // Revoke only the temporary verification peer's independent session.
            peer.call(Envelope.newBuilder().setLogout(LogoutRequest.getDefaultInstance()))
            println("HOSTED TEST PASS: account=$username channel=${channel.name} message=${stored.id}")
        } finally {
            peer?.close()
            session.foreground(false)
        }
    }
}
