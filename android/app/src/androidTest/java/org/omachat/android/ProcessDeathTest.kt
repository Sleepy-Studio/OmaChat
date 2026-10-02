package org.omachat.android

import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.util.UUID

/** Two separately invoked instrumentation phases. The fixture force-stops the
 * app between phases; these methods must not run as a generic test suite. */
class ProcessDeathTest {
    private val operation = UUID.fromString("4bf89ba3-9910-4459-9082-9ba731004def")
    private fun payload(channel: Long): ByteArray = SendMessageRequest.newBuilder()
        .setChannelId(channel).setContent("process-death persisted operation")
        .setOperationId(ByteString.copyFrom(ByteBuffer.allocate(16)
            .putLong(operation.mostSignificantBits).putLong(operation.leastSignificantBits).array()))
        .build().toByteArray()

    @Test fun seedBeforeProcessDeath() = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val port = args.getString("serverPort")?.toInt() ?: error("Run android/tools/integration.py")
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val session = app.session
        session.foreground(true)
        session.login(args.getString("serverHost") ?: "10.0.2.2", port, "androidfixture", "fixture-password-123")
        val connected = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
        val channel = connected.snapshot.channelsList.single { it.name == "general" && it.type == ChannelType.CHANNEL_TYPE_TEXT }.id
        val account = connected.endpoint!!.key
        session.select(channel)
        session.saveDraft(channel, "draft survives real force-stop")
        session.foreground(false)
        withTimeout(10000) { session.ui.first { it.state == ConnectionState.Offline } }
        // Persist the exact state at the transmit boundary. No production fault hook.
        session.dao.put(PendingSend(account, operation.toString(), channel, payload(channel), "Sending"))
        app.getSharedPreferences("process-death-test", 0).edit()
            .putInt("pid", android.os.Process.myPid()).putLong("channel", channel).commit()
        delay(500) // Allow the selected-channel DataStore write to finish before kill.
    }

    @Test fun recoverAfterProcessDeath() = runBlocking {
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val fixture = app.getSharedPreferences("process-death-test", 0)
        assertNotEquals("Fixture must force-stop between phases", fixture.getInt("pid", -1), android.os.Process.myPid())
        val session = app.session
        val restored = withTimeout(10000) { session.ui.first { it.state == ConnectionState.Offline && it.endpoint != null } }
        val account = restored.endpoint!!.key
        val channel = fixture.getLong("channel", 0)
        assertEquals(channel, restored.selected)
        assertEquals("draft survives real force-stop", session.dao.draft(account, channel)?.text)
        val pending = session.dao.pending(account, operation.toString())!!
        assertEquals("Uncertain", pending.state)
        assertArrayEquals(payload(channel), pending.wire)
        try {
            session.foreground(true)
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertNotNull(session.dao.pending(account, operation.toString()))
            session.retry(operation.toString())
            withTimeout(10000) { session.dao.messages(account, channel).first { rows ->
                rows.any { ChatMessage.parseFrom(it.wire).content == "process-death persisted operation" }
            } }
            assertNull(session.dao.pending(account, operation.toString()))
        } finally {
            session.signOut()
            withTimeout(10000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
            session.foreground(false)
            fixture.edit().clear().commit()
        }
    }
}
