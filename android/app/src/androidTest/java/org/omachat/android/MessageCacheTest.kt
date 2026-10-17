package org.omachat.android

import android.content.Context
import androidx.room.Room
import androidx.room.withTransaction
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.runBlocking
import omachat.proto.Network.ChatMessage
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class MessageCacheTest {
    private fun message(id: Long, content: String) = ChatMessage.newBuilder()
        .setId(id).setChannelId(7).setTimestamp(id).setContent(content).build()

    @Test fun delayedRepliesCannotUndoEventsAndDeletedRowsStayDeletedAfterRestart() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val name = "ordering-${UUID.randomUUID()}.db"
        fun open() = Room.databaseBuilder(context, ChatDatabase::class.java, name).build()
        var db = open()
        try {
            val cache = MessageCache(db)
            val fence = cache.fence("a")
            db.withTransaction {
                cache.event("a", message(1, "edited"))
                cache.delete("a", 2) // deletion arrives before the old page
            }
            cache.history("a", listOf(message(1, "original"), message(2, "deleted"), message(3, "older")), fence)
            db.withTransaction {
                cache.acknowledge("a", message(1, "original"), fence)
                cache.acknowledge("a", message(2, "deleted"), fence)
                cache.acknowledge("a", message(3, "original receipt"), fence)
            }
            assertEquals("edited", ChatMessage.parseFrom(db.chat().message("a", 1)!!.wire).content)
            assertNull(db.chat().message("a", 2))
            assertEquals("older", ChatMessage.parseFrom(db.chat().message("a", 3)!!.wire).content)
            db.close(); db = open()
            val restored = MessageCache(db)
            restored.history("a", listOf(message(2, "deleted")), restored.fence("a"))
            db.withTransaction { restored.acknowledge("a", message(2, "deleted"), restored.fence("a")) }
            assertNull(db.chat().message("a", 2))
            // Same IDs on another account remain independent.
            restored.history("b", listOf(message(2, "other account")), restored.fence("b"))
            assertNotNull(db.chat().message("b", 2))
        } finally { db.close(); context.deleteDatabase(name) }
    }

    @Test fun syncInvalidatesInflightHistoryAndEventsAfterRepliesStillWin() = runBlocking {
        val db = Room.inMemoryDatabaseBuilder(ApplicationProvider.getApplicationContext<Context>(), ChatDatabase::class.java).build()
        try {
            val cache = MessageCache(db)
            val oldFence = cache.fence("a")
            db.withTransaction { cache.clear("a") }
            cache.history("a", listOf(message(1, "old access")), oldFence)
            db.withTransaction { cache.acknowledge("a", message(1, "old receipt"), oldFence) }
            assertNull(db.chat().message("a", 1))
            cache.history("a", listOf(message(1, "original")), cache.fence("a"))
            db.withTransaction { cache.event("a", message(1, "edited")) }
            assertEquals("edited", ChatMessage.parseFrom(db.chat().message("a", 1)!!.wire).content)
            db.withTransaction { cache.delete("a", 1); cache.event("a", message(1, "late create")) }
            assertNull(db.chat().message("a", 1))
            db.withTransaction { cache.acknowledge("a", message(4, "receipt first"), cache.fence("a")) }
            db.withTransaction { cache.event("a", message(4, "edited later")) }
            assertEquals("edited later", ChatMessage.parseFrom(db.chat().message("a", 4)!!.wire).content)
        } finally { db.close() }
    }
}
