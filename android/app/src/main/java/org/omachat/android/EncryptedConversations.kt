package org.omachat.android

import android.content.Context
import com.google.protobuf.ByteString
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import omachat.proto.Network.*
import org.omachat.protocol.ControlConnection
import java.security.MessageDigest

/** Only protobuf assembly is Kotlin-owned. All cryptographic operations use the
 * bounded core also linked by the desktop; JNI never owns sockets or lifecycle. */
internal object NativeCrypto {
    init { System.loadLibrary("omachat_crypto") }
    external fun encryptFile(input: String, output: String): ByteArray?
    external fun decryptFile(input: String, output: String, key: ByteArray): Boolean
    external fun safety(userA: Long, keysA: Array<ByteArray>, userB: Long, keysB: Array<ByteArray>): String
    external fun generate(): ByteArray
    external fun publicKey(secret: ByteArray): ByteArray?
    external fun seal(body: ByteArray, channel: Long, author: Long, secret: ByteArray, recipients: Array<ByteArray>): Array<ByteArray>?
    external fun open(payload: Array<ByteArray>, channel: Long, author: Long, secret: ByteArray): ByteArray?
}

internal class EncryptedConversations(context: Context) {
    private val store = SecretStore(context)
    private val lock = Mutex()
    private var secret: ByteArray? = null
    private var scope = ""
    private var accountScope = ""
    private var self = 0L
    var revoked = false
        private set
    private val directory = java.util.concurrent.ConcurrentHashMap<Long, List<ByteString>>()
    private val liveDirectory = java.util.concurrent.ConcurrentHashMap.newKeySet<Long>()
    private var history = linkedMapOf<ByteString, Long>()
    private var ownDevices: List<DeviceKey> = emptyList()
    val changed = java.util.concurrent.ConcurrentHashMap.newKeySet<Long>()
    private fun address(suffix: String) = MessageDigest.getInstance("SHA-256")
        .digest("$scope/$suffix".toByteArray()).joinToString("") { "%02x".format(it) }

    suspend fun restoreOffline(account: String, instance: String, user: Long, users: List<Long>) = lock.withLock {
        close(); scope = "$account/$instance/$user"; accountScope = account; self = user
        secret = store.read(address("identity")) ?: return@withLock
        check(NativeCrypto.publicKey(secret!!) != null) { "Unreadable encryption identity" }
        restoreHistory()
        for (person in (users + self).distinct()) {
            val saved = store.read(address("known/$person"))?.let { DeviceKeyList.parseFrom(it) }
            saved?.let { directory[person] = it.keysList.map { device -> device.publicKey } }
            if (store.read(address("changed/$person")) != null) changed.add(person)
        }
        revoked = ByteString.copyFrom(NativeCrypto.publicKey(secret!!)!!) !in keys(self)
    }
    suspend fun initialize(account: String, instance: String, user: Long, socket: ControlConnection) = lock.withLock {
        val binding = "$account/$instance/$user"
        if (scope != binding || secret == null) {
            close()
            scope = binding; accountScope = account; self = user
            // Corrupt/invalid stored keys stop setup. Never silently rotate identity.
            secret = store.read(address("identity")) ?: NativeCrypto.generate().also { store.write(address("identity"), it) }
            check(NativeCrypto.publicKey(secret!!) != null) { "Unreadable encryption identity" }
            restoreHistory()
        }
        val public = ByteString.copyFrom(NativeCrypto.publicKey(secret!!)!!)
        if (store.read(address("published")) != null) {
            fetch(listOf(self), socket)
            revoked = public !in keys(self)
        } else {
            socket.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(public)))
            store.write(address("published"), byteArrayOf(1))
            fetch(listOf(self), socket)
        }

    }
    private suspend fun fetch(users: List<Long>, socket: ControlConnection) {
        for (batch in users.distinct().chunked(50)) {
            val reply = socket.call(Envelope.newBuilder().setGetDeviceKeys(GetDeviceKeysRequest.newBuilder().addAllUserIds(batch)))
            check(reply.hasDeviceKeyList()) { "Missing encryption directory" }
            for (user in batch) {
                val keys = reply.deviceKeyList.keysList.filter { it.userId == user }.map { it.publicKey }.distinct()
                    .sortedBy { it.toByteArray().joinToString("") { byte -> "%02x".format(byte) } }
                check(keys.size <= 16 && keys.all { it.size() == 32 }) { "Invalid encryption directory" }
                val previous = store.read(address("known/$user"))?.let { DeviceKeyList.parseFrom(it) }
                val old = previous?.keysList?.map { it.publicKey }
                if (old != null && old != keys) {
                    changed.add(user)
                    store.remove(address("verified/$user"))
                }
                // Persist the warning separately so a process restart cannot dismiss it.
                if (user in changed) store.write(address("changed/$user"), byteArrayOf(1))
                if (store.read(address("changed/$user")) != null) changed.add(user)
                directory[user] = keys
                liveDirectory.add(user)
                if (user == self) ownDevices = reply.deviceKeyList.keysList.filter { it.userId == self }.distinctBy { it.publicKey }
                if (user == self && secret != null) revoked = ByteString.copyFrom(NativeCrypto.publicKey(secret!!)!!) !in keys
                store.write(address("known/$user"), DeviceKeyList.newBuilder().addAllKeys(keys.map {
                    DeviceKey.newBuilder().setUserId(user).setPublicKey(it).build()
                }).build().toByteArray())
            }
        }
    }
    suspend fun refresh(users: List<Long>, socket: ControlConnection) = lock.withLock { if (secret != null) fetch(users, socket) }
    suspend fun seal(channel: Channel, body: E2EBody, socket: ControlConnection): ByteString = lock.withLock {
        val key = secret ?: error("Encryption key is not ready")
        check(!revoked) { "This device encryption key was revoked. Sign out and log in to create a new device identity." }
        val users = (channel.recipientIdsList + self).distinct()
        // Fresh directory on every new operation, including edits; retries retain exact bytes.
        fetch(users, socket)
        check(users.all { !directory[it].isNullOrEmpty() }) { "A participant has no encryption devices" }
        check(users.none { it in changed }) { "Device keys changed. Review the conversation's encryption keys before sending." }
        val recipients = users.flatMap { directory[it]!! }.distinct().map { it.toByteArray() }.toTypedArray()
        val plain = body.toByteArray()
        val parts = try { NativeCrypto.seal(plain, channel.id, self, key, recipients) ?: error("Encryption failed") }
            finally { plain.fill(0) }
        val payload = E2EPayload.newBuilder().setVersion(1).setSenderKey(ByteString.copyFrom(parts[0]))
            .setNonce(ByteString.copyFrom(parts[1])).setCiphertext(ByteString.copyFrom(parts[2]))
        for (index in 3 until parts.size step 3) payload.addWraps(E2EKeyWrap.newBuilder()
            .setRecipientKey(ByteString.copyFrom(parts[index])).setNonce(ByteString.copyFrom(parts[index + 1]))
            .setBox(ByteString.copyFrom(parts[index + 2])))
        ByteString.copyFrom(payload.build().toByteArray())
    }
    suspend fun validatePending(channel: Channel, payload: ByteString, socket: ControlConnection) = lock.withLock {
        val key = secret ?: error("Encryption key is not ready")
        val users = (channel.recipientIdsList + self).distinct()
        fetch(users, socket)
        check(!revoked && users.none { it in changed }) { "Review changed device keys before retrying encrypted work." }
        val p = E2EPayload.parseFrom(payload)
        val expected = users.flatMap { keys(it) }.toSet()
        check(p.senderKey == ByteString.copyFrom(NativeCrypto.publicKey(key)!!) &&
            users.all { keys(it).isNotEmpty() } && p.wrapsList.map { it.recipientKey }.toSet() == expected) {
            "Pending encryption targets old device keys. Check history, remove this pending send, then compose again."
        }
    }
    // Receipts describe a local observation, never a server timestamp or signature.
    // Include identity/context and exact ciphertext: old receipts cannot authorize
    // a new edit, another message ID, or newly arriving bytes from a removed key.
    private fun historyDigest(message: ChatMessage): ByteString {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update("omachat-android-observed-v1".toByteArray(Charsets.UTF_8))
        digest.update(java.nio.ByteBuffer.allocate(24).putLong(message.id)
            .putLong(message.channelId).putLong(message.authorId).array())
        digest.update(message.encrypted.toByteArray())
        return ByteString.copyFrom(digest.digest())
    }
    private fun restoreHistory() {
        // Corrupt provenance fails closed without replacing the installation key.
        history = runCatching {
            val bytes = store.read(address("history")) ?: return@runCatching linkedMapOf<ByteString, Long>()
            val input = java.nio.ByteBuffer.wrap(bytes)
            require(input.int == 1)
            val count = input.int
            require(count in 0..HISTORY_LIMIT && input.remaining() == count * 40)
            linkedMapOf<ByteString, Long>().apply {
                repeat(count) {
                    val hash = ByteArray(32).also(input::get)
                    val observed = input.long
                    require(observed > 0)
                    put(ByteString.copyFrom(hash), observed)
                }
            }
        }.getOrElse { linkedMapOf() }
    }
    fun endDirectoryObservation() { liveDirectory.clear() }
    suspend fun remember(account: String, messages: List<ChatMessage>) = withContext(Dispatchers.IO) { lock.withLock {
        if (secret == null || scope.isEmpty() || account != accountScope) return@withLock
        val updated = synchronized(this@EncryptedConversations) {
            LinkedHashMap(history).apply {
                for (message in messages) {
                    if (message.encrypted.isEmpty || message.id <= 0 || message.authorId !in liveDirectory) continue
                    val hash = historyDigest(message)
                    if (hash in this || openCurrent(message) == null) continue
                    put(hash, System.currentTimeMillis())
                    while (size > HISTORY_LIMIT) remove(keys.first())
                }
            }
        }
        if (updated == history) return@withLock
        val bytes = java.nio.ByteBuffer.allocate(8 + updated.size * 40).putInt(1).putInt(updated.size)
        updated.forEach { (hash, time) -> bytes.put(hash.toByteArray()).putLong(time) }
        store.write(address("history"), bytes.array())
        synchronized(this@EncryptedConversations) { history = updated }
    } }
    @Synchronized fun historicalObservation(message: ChatMessage): Long? = runCatching {
        if (message.encrypted.isEmpty) return null
        val sender = E2EPayload.parseFrom(message.encrypted).senderKey
        if (sender in keys(message.authorId)) null else history[historyDigest(message)]
    }.getOrNull()
    private fun openCurrent(message: ChatMessage) = open(message, allowHistory = false)
    @Synchronized fun open(message: ChatMessage): E2EBody? = open(message, allowHistory = true)
    private fun open(message: ChatMessage, allowHistory: Boolean): E2EBody? {
        val key = secret ?: return null
        return runCatching {
            require(message.encrypted.size() <= 1024 * 1024 + 32768)
            val p = E2EPayload.parseFrom(message.encrypted)
            require(p.version == 1 && p.wrapsCount in 1..256)
            require(p.senderKey in keys(message.authorId) ||
                (allowHistory && historyDigest(message) in history))
            val parts = listOf(p.senderKey.toByteArray(), p.nonce.toByteArray(), p.ciphertext.toByteArray()) +
                p.wrapsList.flatMap { listOf(it.recipientKey.toByteArray(), it.nonce.toByteArray(), it.box.toByteArray()) }
            val plain = NativeCrypto.open(parts.toTypedArray(), message.channelId, message.authorId, key) ?: return null
            try { E2EBody.parseFrom(plain) } finally { plain.fill(0) }
        }.getOrNull()
    }
    @Synchronized fun close() { secret?.fill(0); secret = null; directory.clear(); liveDirectory.clear(); history.clear(); ownDevices = emptyList(); changed.clear(); scope = ""; accountScope = ""; self = 0; revoked = false }
    suspend fun devices(socket: ControlConnection): List<DeviceKey> = lock.withLock {
        check(secret != null && self != 0L) { "Encryption identity is not ready" }
        fetch(listOf(self), socket)
        ownDevices.toList()
    }
    suspend fun revokeDevice(publicKey: ByteString, socket: ControlConnection): List<DeviceKey> = lock.withLock {
        check(secret != null && self != 0L) { "Encryption identity is not ready" }
        require(publicKey.size() == 32)
        fetch(listOf(self), socket)
        check(publicKey in keys(self)) { "This encryption device is no longer registered. Refresh the list." }
        socket.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder().setPublicKey(publicKey)))
        fetch(listOf(self), socket)
        check(publicKey !in keys(self)) { "Device revocation was not confirmed. Refresh before trying again." }
        // Keep this installation's secret for old recipient wraps, even when
        // revoking itself. initialize() must not silently republish it.
        ownDevices.toList()
    }
    fun localDeviceKey(): ByteString? = secret?.let { NativeCrypto.publicKey(it)?.let(ByteString::copyFrom) }
    suspend fun forget() = lock.withLock {
        // Local sign-out must erase the private identity even without a socket.
        // Offline removal cannot promise revocation of the server's public record.
        if (scope.isNotEmpty()) {
            store.remove(address("identity")); store.remove(address("published")); store.remove(address("history"))
        }
        close()
    }
    suspend fun revoke(socket: ControlConnection) = lock.withLock {
        secret?.let { key ->
            socket.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder()
                .setPublicKey(ByteString.copyFrom(NativeCrypto.publicKey(key)!!))))
            store.remove(address("identity"))
            store.remove(address("published"))
            store.remove(address("history"))
        }
        close()
    }
    fun safety(user: Long): String = NativeCrypto.safety(self, keys(self).map { it.toByteArray() }.toTypedArray(), user, keys(user).map { it.toByteArray() }.toTypedArray())
    fun verified(user: Long) = store.read(address("verified/$user"))?.toString(Charsets.UTF_8) == safety(user)
    suspend fun verify(user: Long, expectedNumber: String) = lock.withLock {
        check(keys(user).isNotEmpty() && safety(user) == expectedNumber) { "Keys changed while reviewing. Compare again." }
        store.write(address("verified/$user"), expectedNumber.toByteArray())
        acknowledge(listOf(user, self))
    }
    suspend fun accept(user: Long, expectedNumber: String) = lock.withLock {
        check(keys(user).isNotEmpty() && safety(user) == expectedNumber) { "Keys changed while reviewing. Compare again." }
        acknowledge(listOf(user, self))
    }
    fun keys(user: Long): List<ByteString> = directory[user] ?: emptyList()
    private companion object { const val HISTORY_LIMIT = 1024 }
    private fun acknowledge(users: List<Long>) {
        users.forEach { changed.remove(it); store.remove(address("changed/$it")) }
    }
}
