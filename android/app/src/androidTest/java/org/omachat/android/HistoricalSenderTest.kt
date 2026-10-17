package org.omachat.android

import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.google.protobuf.ByteString
import kotlinx.coroutines.*
import omachat.proto.Network.*
import org.junit.Assert.*
import org.junit.Test
import org.omachat.protocol.ControlConnection

/** Exact-byte local provenance, with actual live directory/revocation responses. */
class HistoricalSenderTest {
    @Test fun observedBytesSurviveRevocationButUnseenEditsAndContextsDoNot(): Unit = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        val context = ApplicationProvider.getApplicationContext<OmaChatApp>()
        val worker = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val socket = ControlConnection.connect(args.getString("serverHost")!!,
            args.getString("serverPort")!!.toInt(), args.getString("serverFingerprint")!!, worker)
        val sender = NativeCrypto.generate()
        val public = ByteString.copyFrom(NativeCrypto.publicKey(sender)!!)
        val account = java.util.UUID.randomUUID().toString()
        val crypto = EncryptedConversations(context)
        var user = 0L
        fun message(id: Long, text: String): ChatMessage {
            val body = E2EBody.newBuilder().setContent(text).build().toByteArray()
            val parts = NativeCrypto.seal(body, 7, user, sender, arrayOf(crypto.localDeviceKey()!!.toByteArray()))!!
            val payload = E2EPayload.newBuilder().setVersion(1).setSenderKey(ByteString.copyFrom(parts[0]))
                .setNonce(ByteString.copyFrom(parts[1])).setCiphertext(ByteString.copyFrom(parts[2]))
            for (i in 3 until parts.size step 3) payload.addWraps(E2EKeyWrap.newBuilder()
                .setRecipientKey(ByteString.copyFrom(parts[i])).setNonce(ByteString.copyFrom(parts[i + 1])).setBox(ByteString.copyFrom(parts[i + 2])))
            return ChatMessage.newBuilder().setId(id).setChannelId(7).setAuthorId(user)
                .setEncrypted(payload.build().toByteString()).build()
        }
        try {
            socket.call(Envelope.newBuilder().setHello(Hello.newBuilder().setProtocolMajor(1).setProtocolMinor(2)))
            user = socket.call(Envelope.newBuilder().setRegister(RegisterRequest.newBuilder()
                .setUsername("historyfixture").setPassword("fixture-history-password-123"))).authResult.user.id
            socket.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(public)))
            crypto.initialize(account, "history-instance", user, socket)
            val observed = message(1, "already authenticated")
            val unseen = message(2, "never observed")
            val edited = message(1, "new encrypted edit")
            assertEquals("already authenticated", crypto.open(observed)?.content)
            assertNull(crypto.historicalObservation(observed))
            crypto.remember("wrong-account", listOf(unseen))
            crypto.remember(account, listOf(observed))
            socket.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder().setPublicKey(public)))
            crypto.refresh(listOf(user), socket)
            assertEquals("already authenticated", crypto.open(observed)?.content)
            val time = crypto.historicalObservation(observed)
            assertNotNull(time)
            assertNull(crypto.open(unseen))
            assertNull(crypto.open(edited))
            assertNull(crypto.open(observed.toBuilder().setId(9).build()))
            assertNull(crypto.open(observed.toBuilder().setChannelId(8).build()))
            assertNull(crypto.open(observed.toBuilder().setAuthorId(user + 1).build()))
            val tampered = observed.encrypted.toByteArray().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
            assertNull(crypto.open(observed.toBuilder().setEncrypted(ByteString.copyFrom(tampered)).build()))
            crypto.remember(account, listOf(unseen, edited))
            assertNull(crypto.open(unseen))
            // Restore from disk in a fresh coordinator without a network directory.
            crypto.close()
            val restored = EncryptedConversations(context)
            try {
                restored.restoreOffline(account, "history-instance", user, listOf(user))
                assertEquals("already authenticated", restored.open(observed)?.content)
                assertEquals(time, restored.historicalObservation(observed))
                assertNull(restored.open(edited))
                // Offline cached directories cannot mint new receipts.
                socket.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(public)))
                restored.refresh(listOf(user), socket)
                restored.endDirectoryObservation()
                restored.remember(account, listOf(unseen))
                socket.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder().setPublicKey(public)))
                restored.refresh(listOf(user), socket)
                assertNull(restored.open(unseen))
                // Bounded journal evicts old exact-byte observations, fail closed.
                socket.call(Envelope.newBuilder().setPublishDeviceKey(PublishDeviceKeyRequest.newBuilder().setPublicKey(public)))
                restored.refresh(listOf(user), socket)
                val batch = (100L..1124L).map { observed.toBuilder().setId(it).build() }
                restored.remember(account, batch)
                socket.call(Envelope.newBuilder().setRevokeDeviceKey(RevokeDeviceKeyRequest.newBuilder().setPublicKey(public)))
                restored.refresh(listOf(user), socket)
                assertNull(restored.open(observed))
                assertNull(restored.open(batch.first()))
                assertEquals("already authenticated", restored.open(batch.last())?.content)
                restored.close()
                restored.restoreOffline(account, "another-instance", user, listOf(user))
                assertNull(restored.open(batch.last()))
                restored.restoreOffline("another-account", "history-instance", user, listOf(user))
                assertNull(restored.open(batch.last()))
                restored.restoreOffline(account, "history-instance", user, listOf(user))
                assertEquals("already authenticated", restored.open(batch.last())?.content)
                // Invalid wrapped provenance cannot authorize history, nor rotate keys.
                val binding = "$account/history-instance/$user"
                val address = java.security.MessageDigest.getInstance("SHA-256")
                    .digest("$binding/history".toByteArray()).joinToString("") { "%02x".format(it) }
                val localKey = restored.localDeviceKey()
                SecretStore(context).write(address, byteArrayOf(0))
                restored.restoreOffline(account, "history-instance", user, listOf(user))
                assertEquals(localKey, restored.localDeviceKey())
                assertNull(restored.open(batch.last()))
                restored.forget()
                restored.restoreOffline(account, "history-instance", user, listOf(user))
                assertNull(restored.open(observed))
            } finally { restored.forget() }
        } finally { crypto.forget(); sender.fill(0); socket.close(); worker.cancel() }
    }
}
