package org.omachat.android

import android.Manifest
import android.content.Intent
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import omachat.proto.Network.*
import org.omachat.protocol.ControlConnection
import org.junit.Assert.*
import org.junit.Test

/** Actual application/service/control integration on a disposable server. */
class VoiceOwnershipTest {
    private suspend fun fixture(block: suspend (SessionCoordinator, ControlConnection, Long, ActivityScenario<MainActivity>) -> Unit) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val session = app.session
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val control = ControlConnection.connect(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(), args.getString("serverFingerprint")!!, scope)
        control.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
        // Distinct disposable users keep the five-per-name login limiter intact.
        val username = "owner${System.nanoTime()}"
        control.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder().setUsername(username).setPassword("fixture-password-123")))
        val server = control.call(Envelope.newBuilder().setCreateServer(CreateServerRequest.newBuilder().setName("Ownership fixture"))).server
        val channel = control.call(Envelope.newBuilder().setCreateChannel(CreateChannelRequest.newBuilder()
            .setServerId(server.id).setName("ownership-${System.nanoTime()}").setType(ChannelType.CHANNEL_TYPE_VOICE))).channel.id
        instrumentation.uiAutomation.executeShellCommand("pm grant ${app.packageName} ${Manifest.permission.RECORD_AUDIO}").close()
        instrumentation.uiAutomation.executeShellCommand("pm grant ${app.packageName} ${Manifest.permission.POST_NOTIFICATIONS}").close()
        ActivityScenario.launch(MainActivity::class.java).use { activity ->
            try {
                session.foreground(true)
                fun login() = session.login(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(), username, "fixture-password-123")
                login()
                val first = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected || it.state == ConnectionState.CertificateConfirmationRequired } }
                if (first.state == ConnectionState.CertificateConfirmationRequired) {
                    assertEquals(args.getString("serverFingerprint"), first.certificate)
                    session.acceptCertificate()
                    withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
                    login()
                }
                withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
                assertTrue(session.ui.value.snapshot.channelsList.any { it.id == channel })
                block(session, control, channel, activity)
            } finally {
                session.leaveVoice()
                withTimeout(15000) { session.voice.first { !it.active } }
                session.signOut()
                withTimeout(15000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
                session.foreground(false)
                control.close(); scope.cancel()
            }
        }
    }
    private fun join(session: SessionCoordinator, channel: Long, transfer: Boolean = false) =
        InstrumentationRegistry.getInstrumentation().runOnMainSync { session.joinVoice(channel, transfer) }
    private suspend fun connected(session: SessionCoordinator) {
        val state = withTimeout(25000) { session.voice.first { it.connected || (!it.starting && it.error.isNotEmpty()) } }
        assertTrue(state.error, state.connected)
    }
    @Test fun explicitTransferStopsFormerOwnerAndNeverAutomaticallyRejoins(): Unit = runBlocking {
        fixture { session, other, channel, _ ->
            other.call(Envelope.newBuilder().setJoinVoice(JoinVoiceRequest.newBuilder().setChannelId(channel)))
            join(session, channel)
            withTimeout(25000) { session.voice.first { it.transferChannel == channel } }
            assertFalse(session.voice.value.active)
            val previous = other.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState.voiceStatesList.single { it.userId == session.ui.value.snapshot.self.id }
            session.cancelVoiceTransfer()
            delay(300)
            assertFalse(session.voice.value.active)
            // Explicit transfer is a separate user action with a fresh lease.
            join(session, channel, true); connected(session)
            val current = other.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState.voiceStatesList.single { it.userId == previous.userId }
            assertNotEquals(previous.streamId, current.streamId)
            session.muteVoice()
            withTimeout(5000) { session.voice.first { it.muted } }
            session.muteVoice()
            withTimeout(5000) { session.voice.first { !it.muted } }
            other.call(Envelope.newBuilder().setJoinVoice(JoinVoiceRequest.newBuilder().setChannelId(channel).setTransfer(true)))
            withTimeout(10000) { session.voice.first { !it.active && it.error.contains("moved") } }
            delay(500)
            assertFalse(session.voice.value.active)
            val moved = other.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState.voiceStatesList.single { it.userId == previous.userId }
            assertNotEquals(current.ownerSessionId, moved.ownerSessionId)
            // Reconnect does not start capture or steal the other device's voice.
            session.networkChanged()
            withTimeout(10000) { session.ui.first { it.state != ConnectionState.Connected } }
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertFalse(session.voice.value.active)
            other.call(Envelope.newBuilder().setLeaveVoice(LeaveVoiceRequest.getDefaultInstance()))
        }
    }
    @Test fun foregroundCallSurvivesBackgroundAndStopsOnNetworkAndNotificationLeave(): Unit = runBlocking {
        fixture { session, other, channel, activity ->
            join(session, channel); connected(session)
            activity.moveToState(androidx.lifecycle.Lifecycle.State.CREATED)
            session.foreground(false)
            delay(1400)
            assertTrue(session.voice.value.connected)
            assertEquals(ConnectionState.Connected, session.ui.value.state)
            val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
            // Exercise the notification's service command while backgrounded.
            app.startService(Intent(app, VoiceService::class.java).setAction("org.omachat.android.LEAVE_VOICE"))
            withTimeout(12000) { session.voice.first { !it.active } }
            withTimeout(12000) { session.ui.first { it.state == ConnectionState.Offline } }
            val snapshot = other.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState
            assertTrue(snapshot.voiceStatesList.none { it.userId == session.ui.value.snapshot.self.id && it.channelId != 0L })
            activity.moveToState(androidx.lifecycle.Lifecycle.State.RESUMED)
            session.foreground(true)
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertFalse(session.voice.value.active)
            join(session, channel); connected(session)
            session.networkChanged()
            withTimeout(10000) { session.ui.first { it.state != ConnectionState.Connected } }
            withTimeout(12000) { session.voice.first { !it.active } }
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertFalse(session.voice.value.active)
            // Network loss while backgrounded also ends capture and idle control.
            join(session, channel); connected(session)
            activity.moveToState(androidx.lifecycle.Lifecycle.State.CREATED)
            session.foreground(false)
            delay(1000)
            session.networkChanged()
            withTimeout(12000) { session.voice.first { !it.active } }
            withTimeout(12000) { session.ui.first { it.state == ConnectionState.Offline } }
            activity.moveToState(androidx.lifecycle.Lifecycle.State.RESUMED)
            session.foreground(true)
            withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
            assertFalse(session.voice.value.active)
            // Sign-out ends the owned lease before secrets are removed.
            join(session, channel); connected(session)
            session.signOut()
            withTimeout(15000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
            assertFalse(session.voice.value.active)
        }
    }
    private suspend fun clickText(text: String) = withTimeout(10000) {
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        while (true) {
            fun find(node: android.view.accessibility.AccessibilityNodeInfo?): android.view.accessibility.AccessibilityNodeInfo? {
                node ?: return null
                if (node.text?.toString()?.contains(text) == true) return node
                for (index in 0 until node.childCount) find(node.getChild(index))?.let { return it }
                return null
            }
            var node = find(automation.rootInActiveWindow)
            if (node != null) {
                while (node != null && !node.isClickable) node = node.parent
                // StateFlow may lead the Compose accessibility tree by a frame.
                if (node?.isEnabled == true && node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK)) break
            }
            delay(100)
        }
    }
    @Test fun composeTransferConfirmationAndFocusLossReleaseService(): Unit = runBlocking {
        fixture { session, other, channel, _ ->
            val name = session.ui.value.snapshot.channelsList.single { it.id == channel }.name
            other.call(Envelope.newBuilder().setJoinVoice(JoinVoiceRequest.newBuilder().setChannelId(channel)))
            session.select(0)
            clickText("Voice · $name")
            withTimeout(25000) { session.voice.first { it.transferChannel == channel } }
            clickText("Cancel")
            assertFalse(session.voice.value.active)
            clickText("Voice · $name")
            withTimeout(25000) { session.voice.first { it.transferChannel == channel } }
            clickText("Move voice here")
            connected(session)
            clickText("Mute")
            withTimeout(5000) { session.voice.first { it.muted } }
            clickText("Unmute")
            withTimeout(5000) { session.voice.first { !it.muted } }
            // Inject the platform focus callback. This verifies policy/cleanup,
            // not real telephony or Bluetooth interruption behavior.
            val call = SessionCoordinator::class.java.getDeclaredField("voiceCall").apply { isAccessible = true }.get(session)
            val service = VoiceCall::class.java.getDeclaredField("service").apply { isAccessible = true }.get(call) as VoiceService
            val listener = VoiceService::class.java.getDeclaredField("focusListener").apply { isAccessible = true }.get(service) as android.media.AudioManager.OnAudioFocusChangeListener
            InstrumentationRegistry.getInstrumentation().runOnMainSync {
                listener.onAudioFocusChange(android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
            }
            withTimeout(10000) { session.voice.first { !it.active && it.error.contains("focus") } }
            delay(300)
            assertFalse(session.voice.value.active)
            join(session, channel); connected(session)
            val app = ApplicationProvider.getApplicationContext<OmaChatApp>()
            assertTrue(app.stopService(Intent(app, VoiceService::class.java)))
            withTimeout(10000) { session.voice.first { !it.active && it.error.contains("service stopped") } }
            assertFalse(session.voice.value.active)
        }
    }

    @Test fun cancelledQueuedServiceStartsNeverJoinOrRestartCapture(): Unit = runBlocking {
        fixture { session, other, channel, _ ->
            repeat(3) {
                InstrumentationRegistry.getInstrumentation().runOnMainSync {
                    session.joinVoice(channel)
                    session.leaveVoice()
                }
                withTimeout(5000) { session.voice.first { !it.active } }
                delay(350)
                val snapshot = other.call(Envelope.newBuilder().setSync(SyncRequest.getDefaultInstance())).syncState
                assertTrue(snapshot.voiceStatesList.none { it.userId == session.ui.value.snapshot.self.id && it.channelId != 0L })
            }
            // Let Android's foreground-promotion watchdog fire if a queued
            // cancelled command incorrectly returned without promotion/stop.
            delay(11000)
            assertFalse(session.voice.value.active)
            join(session, channel); connected(session)
            val call = SessionCoordinator::class.java.getDeclaredField("voiceCall").apply { isAccessible = true }.get(session) as VoiceCall
            val fence = VoiceCall::class.java.getDeclaredField("observedSequence").apply { isAccessible = true }.getLong(call)
            assertTrue(fence > 0)
            // A pre-join leave or structural reply represented by the observed
            // snapshot cannot tear down the new resource owner.
            call.observe(VoiceState.newBuilder().setUserId(session.ui.value.snapshot.self.id).build(), fence)
            call.reconcile(SyncState.newBuilder().setLastSequence(fence).build())
            assertTrue(session.voice.value.connected)
            session.leaveVoice()
            withTimeout(10000) { session.voice.first { !it.active } }
        }
    }

}
