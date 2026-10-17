package org.omachat.android

import android.os.StrictMode
import android.os.Looper
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSession
import javax.net.ssl.HandshakeCompletedListener
import org.omachat.protocol.ControlConnection
import androidx.test.core.app.ApplicationProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executor

/** A TLS close can write close-notify; lifecycle callbacks must never do network I/O. */
class MainThreadLifecycleTest {
    @Test fun backgroundCallbackDoesNotCloseTlsOnMainThread(): Unit = runBlocking {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        val session = ApplicationProvider.getApplicationContext<OmaChatApp>().session
        val violations = CopyOnWriteArrayList<android.os.strictmode.Violation>()
        ActivityScenario.launch(MainActivity::class.java).use {
            session.foreground(true)
            try {
                fun login() = session.login(args.getString("serverHost")!!, args.getString("serverPort")!!.toInt(), "androidfixture", "fixture-password-123")
                login()
                val first = withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected || it.state == ConnectionState.CertificateConfirmationRequired } }
                if (first.state == ConnectionState.CertificateConfirmationRequired) {
                    assertEquals(args.getString("serverFingerprint"), first.certificate)
                    session.acceptCertificate()
                    withTimeout(5000) { session.ui.first { it.state == ConnectionState.LoginRequired } }
                    login()
                    withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
                }
                repeat(3) {
                    // Observe the actual close-call thread as well as StrictMode:
                    // Conscrypt implementations differ in whether close writes TLS bytes.
                    val connectionField = SessionCoordinator::class.java.getDeclaredField("connection").apply { isAccessible = true }
                    val connection = connectionField.get(session) as ControlConnection
                    val socketField = ControlConnection::class.java.getDeclaredField("socket").apply { isAccessible = true }
                    val original = socketField.get(connection) as SSLSocket
                    val closedOnMain = java.util.concurrent.atomic.AtomicBoolean(false)
                    socketField.set(connection, ObservedSocket(original) {
                        if (Looper.myLooper() == Looper.getMainLooper()) closedOnMain.set(true)
                    })
                    instrumentation.runOnMainSync {
                        val previous = StrictMode.getThreadPolicy()
                        try {
                            StrictMode.setThreadPolicy(StrictMode.ThreadPolicy.Builder().detectNetwork()
                                .penaltyListener(Executor { command -> command.run() }) { violations.add(it) }.build())
                            session.foreground(false)
                        } finally { StrictMode.setThreadPolicy(previous) }
                    }
                    withTimeout(10000) { session.ui.first { it.state == ConnectionState.Offline } }
                    instrumentation.waitForIdleSync()
                    assertFalse("TLS socket close ran on the main thread", closedOnMain.get())
                    assertTrue("TLS lifecycle performed network I/O on main: $violations", violations.isEmpty())
                    instrumentation.runOnMainSync { session.foreground(true) }
                    withTimeout(20000) { session.ui.first { it.state == ConnectionState.Connected } }
                }
            } finally {
                session.signOut()
                withTimeout(10000) { session.ui.first { it.state == ConnectionState.NotConfigured } }
                session.foreground(false)
            }
        }
    }
}

/** Test-only observer around an established socket; original stream/framing stay intact. */
private class ObservedSocket(private val original: SSLSocket, private val onClose: () -> Unit) : SSLSocket() {
    override fun close() { onClose(); original.close() }
    override fun isClosed() = original.isClosed
    override fun getSupportedCipherSuites() = original.supportedCipherSuites
    override fun getEnabledCipherSuites() = original.enabledCipherSuites
    override fun setEnabledCipherSuites(value: Array<String>) { original.enabledCipherSuites = value }
    override fun getSupportedProtocols() = original.supportedProtocols
    override fun getEnabledProtocols() = original.enabledProtocols
    override fun setEnabledProtocols(value: Array<String>) { original.enabledProtocols = value }
    override fun getSession(): SSLSession = original.session
    override fun addHandshakeCompletedListener(value: HandshakeCompletedListener) = original.addHandshakeCompletedListener(value)
    override fun removeHandshakeCompletedListener(value: HandshakeCompletedListener) = original.removeHandshakeCompletedListener(value)
    override fun startHandshake() = original.startHandshake()
    override fun setUseClientMode(value: Boolean) { original.useClientMode = value }
    override fun getUseClientMode() = original.useClientMode
    override fun setNeedClientAuth(value: Boolean) { original.needClientAuth = value }
    override fun getNeedClientAuth() = original.needClientAuth
    override fun setWantClientAuth(value: Boolean) { original.wantClientAuth = value }
    override fun getWantClientAuth() = original.wantClientAuth
    override fun setEnableSessionCreation(value: Boolean) { original.enableSessionCreation = value }
    override fun getEnableSessionCreation() = original.enableSessionCreation
}
