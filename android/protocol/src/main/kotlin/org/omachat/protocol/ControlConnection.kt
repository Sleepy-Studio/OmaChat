package org.omachat.protocol

import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import omachat.proto.Network.Envelope
import omachat.proto.Network.Event
import java.io.Closeable
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetSocketAddress
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.net.ssl.*

object Framing {
    const val MAX_BYTES = 8 * 1024 * 1024
    fun read(input: DataInputStream): Envelope {
        val size = input.readInt()
        require(size in 1..MAX_BYTES) { "Invalid control frame length" }
        val bytes = ByteArray(size)
        input.readFully(bytes)
        return Envelope.parseFrom(bytes)
    }
    fun write(output: DataOutputStream, envelope: Envelope) {
        val size = envelope.serializedSize
        require(size in 1..MAX_BYTES)
        output.writeInt(size)
        envelope.writeTo(output)
        output.flush()
    }
}

fun fingerprint(cert: X509Certificate): String = MessageDigest.getInstance("SHA-256")
    .digest(cert.encoded).joinToString(":") { "%02X".format(it) }

class CertificateConfirmation(val fingerprint: String, val changed: Boolean) :
    CertificateException("Certificate confirmation required")
class ServerError(val code: Int, message: String) : Exception(message)

/** Scoped to this socket only. A saved pin must match even when a new certificate
 * would pass system trust. Only a valid, self-signed leaf can start initial trust.
 * Explicit pin acceptance authorizes the exact certificate for the chosen endpoint.
 */
class EndpointTrust(private val pin: String?) : X509TrustManager {
    private val system = (TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply {
        init(null as KeyStore?)
    }.trustManagers.filterIsInstance<X509TrustManager>().single())
    var confirmation: CertificateConfirmation? = null
        private set
    override fun getAcceptedIssuers(): Array<X509Certificate> = system.acceptedIssuers
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) =
        system.checkClientTrusted(chain, authType)
    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {
        if (chain.isEmpty()) throw CertificateException("Empty certificate chain")
        chain[0].checkValidity()
        val actual = fingerprint(chain[0])
        if (pin != null) {
            if (pin != actual) {
                confirmation = CertificateConfirmation(actual, true)
                throw confirmation!!
            }
            return
        }
        try { system.checkServerTrusted(chain, authType) }
        catch (e: CertificateException) {
            if (chain.size != 1 || chain[0].subjectX500Principal != chain[0].issuerX500Principal) throw e
            chain[0].verify(chain[0].publicKey)
            confirmation = CertificateConfirmation(actual, false)
            throw confirmation!!
        }
    }
}

/** One reader per socket, bounded concurrent requests, serialized writes. Close
 * the socket before cancelling its owner so blocking I/O is released immediately. */
class ControlConnection private constructor(private val socket: SSLSocket, scope: CoroutineScope) : Closeable {
    /** Address selected by the authenticated TLS connection; never resolve media independently. */
    val peerAddress: java.net.InetAddress get() = socket.inetAddress
    private val input = DataInputStream(socket.inputStream)
    private val output = DataOutputStream(socket.outputStream)
    private val ids = AtomicLong(1)
    private val pending = ConcurrentHashMap<Long, CompletableDeferred<Envelope>>()
    private val writeLock = Mutex()
    val events = Channel<Event>(64)
    @Volatile private var acceptingEvents = false
    /** Full sync is a wire-order barrier. Events before its reply are represented
     * by the snapshot; events following it must be applied after that snapshot. */
    suspend fun synchronize(): Envelope {
        acceptingEvents = false
        while (events.tryReceive().isSuccess) { /* discard pre-snapshot events */ }
        return call(Envelope.newBuilder().setSync(omachat.proto.Network.SyncRequest.getDefaultInstance()))
    }
    private val reader = scope.launch(Dispatchers.IO) {
        try {
            while (isActive) {
                val env = Framing.read(input)
                if (env.requestId != 0L) {
                    if (env.hasSyncState()) acceptingEvents = true
                    if (env.hasResumeResult()) acceptingEvents = env.resumeResult.replayedEvents in 0..32
                    pending.remove(env.requestId)?.complete(env)
                } else if (env.hasEvent() && acceptingEvents) {
                    // Blocking the sole reader would also block request replies.
                    // Overflow ends this connection and forces authoritative sync.
                    check(events.trySend(env.event).isSuccess) { "Event queue overflow; full synchronization required" }
                }
            }
        } catch (e: Exception) {
            pending.values.forEach { it.completeExceptionally(e) }
            events.close(e)
        } finally { socket.close() }
    }
    suspend fun call(builder: Envelope.Builder): Envelope {
        val id = ids.getAndIncrement()
        val reply = CompletableDeferred<Envelope>()
        writeLock.withLock {
            check(!socket.isClosed) { "Connection is closed" }
            check(pending.size < 32) { "Too many pending requests" }
            pending[id] = reply
            try { withContext(Dispatchers.IO) { Framing.write(output, builder.setRequestId(id).build()) } }
            catch (e: Exception) { pending.remove(id); throw e }
        }
        return try {
            withTimeout(20_000) { reply.await() }.also {
                if (it.hasError()) throw ServerError(it.error.codeValue, it.error.message)
            }
        } finally { pending.remove(id) }
    }
    override fun close() {
        socket.close()
        reader.cancel()
        pending.values.forEach { it.cancel() }
        pending.clear()
        events.close()
    }
    companion object {
        suspend fun connect(host: String, port: Int, pin: String?, scope: CoroutineScope): ControlConnection =
            withContext(Dispatchers.IO) { suspendCancellableCoroutine { continuation ->
                require(host.isNotBlank() && port in 1..65535)
                val trust = EndpointTrust(pin)
                val context = SSLContext.getInstance("TLSv1.3").apply { init(null, arrayOf(trust), null) }
                val socket = context.socketFactory.createSocket() as SSLSocket
                continuation.invokeOnCancellation { socket.close() }
                try {
                    socket.enabledProtocols = arrayOf("TLSv1.3")
                    socket.sslParameters = socket.sslParameters.apply {
                        if (pin == null) endpointIdentificationAlgorithm = "HTTPS"
                    }
                    socket.soTimeout = 15_000
                    socket.tcpNoDelay = true
                    socket.keepAlive = true
                    socket.connect(InetSocketAddress(host, port), 10_000)
                    socket.startHandshake()
                    socket.soTimeout = 0
                    continuation.resume(ControlConnection(socket, scope)) { _, value, _ -> value.close() }
                } catch (e: Exception) {
                    socket.close()
                    continuation.resumeWith(Result.failure(trust.confirmation ?: e))
                }
            } }

    }
}
