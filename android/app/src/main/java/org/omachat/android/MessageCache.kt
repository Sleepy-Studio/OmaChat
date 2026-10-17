package org.omachat.android

import androidx.room.withTransaction
import omachat.proto.Network.ChatMessage

/** Events are authoritative; historical replies and original send receipts are
 * snapshots. All callers participate in Room transactions, including the cursor. */
internal class MessageCache(
    private val database: ChatDatabase,
    private val observe: suspend (String, List<ChatMessage>) -> Unit = { _, _ -> }
) {
    private val dao = database.chat()
    private var generation = 0L // protected by Room's transaction serialization
    data class Fence(val generation: Long, val revision: Long)

    suspend fun fence(account: String): Fence = database.withTransaction {
        Fence(generation, dao.revision(account))
    }

    // Called inside the same transaction that replaces the sync snapshot.
    suspend fun clear(account: String) {
        generation++
        dao.clearMessages(account)
        dao.clearMutations(account)
    }

    suspend fun event(account: String, message: ChatMessage) {
        val previous = dao.mutation(account, message.id)
        // Message IDs are never reused. A deletion cannot be resurrected.
        if (previous?.deleted == true) return
        observe(account, listOf(message))
        dao.put(MessageMutation(account, message.id, dao.revision(account) + 1, false))
        dao.put(listOf(row(account, message)))
    }

    suspend fun delete(account: String, id: Long) {
        dao.put(MessageMutation(account, id, dao.revision(account) + 1, true))
        dao.delete(account, id)
    }

    suspend fun history(account: String, messages: List<ChatMessage>, fence: Fence) = database.withTransaction {
        if (generation != fence.generation) return@withTransaction
        val accepted = messages.filter { message ->
            val mutation = dao.mutation(account, message.id)
            mutation?.deleted != true && (mutation?.revision ?: 0) <= fence.revision
        }
        observe(account, accepted)
        dao.put(accepted.map { row(account, it) })
    }

    suspend fun acknowledge(account: String, message: ChatMessage, fence: Fence) {
        // Idempotent replies contain the originally accepted bytes, even after
        // edits. They confirm the outbox but never replace an existing message.
        if (generation == fence.generation && dao.mutation(account, message.id) == null && dao.message(account, message.id) == null) {
            observe(account, listOf(message))
            dao.put(listOf(row(account, message)))
        }
    }

    private fun row(account: String, message: ChatMessage) =
        CachedMessage(account, message.id, message.channelId, message.timestamp, message.toByteArray())
}
