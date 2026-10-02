package org.omachat.android

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import androidx.room.Room
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.flow.first
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class RoomPersistenceTest {
    @Test fun migrationsPreserveDraftsAndSeparateAccounts() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        for (version in 1..5) {
            val name = "migration-${UUID.randomUUID()}.db"
            val path = context.getDatabasePath(name).apply { parentFile!!.mkdirs() }
            val schema = InstrumentationRegistry.getInstrumentation().context.assets
                .open("org.omachat.android.ChatDatabase/$version.json").bufferedReader().use { JSONObject(it.readText()).getJSONObject("database") }
            SQLiteDatabase.openOrCreateDatabase(path, null).use { old ->
                val entities = schema.getJSONArray("entities")
                for (index in 0 until entities.length()) {
                    val entity = entities.getJSONObject(index)
                    old.execSQL(entity.getString("createSql").replace("\${TABLE_NAME}", entity.getString("tableName")))
                    val indices = entity.optJSONArray("indices")
                    for (i in 0 until (indices?.length() ?: 0)) old.execSQL(indices!!.getJSONObject(i).getString("createSql")
                        .replace("\${TABLE_NAME}", entity.getString("tableName")))
                }
                val setup = schema.getJSONArray("setupQueries")
                for (index in 0 until setup.length()) old.execSQL(setup.getString(index))
                old.execSQL("INSERT INTO drafts VALUES ('account-a', 7, 'survives upgrade')")
                old.version = version
            }
            fun open() = Room.databaseBuilder(context, ChatDatabase::class.java, name)
                .addMigrations(ChatDatabase.MIGRATION_1_2, ChatDatabase.MIGRATION_2_3, ChatDatabase.MIGRATION_3_4, ChatDatabase.MIGRATION_4_5, ChatDatabase.MIGRATION_5_6).build()
            try {
                open().withDatabase { db ->
                    val dao = db.chat()
                    assertEquals("survives upgrade", dao.draft("account-a", 7)?.text)
                    dao.put(Draft("account-b", 7, "other server/account"))
                    dao.put(PendingSend("account-a", "stable-operation", 7, byteArrayOf(1, 2), "Sending"))
                    dao.put(PendingSend("account-b", "stable-operation", 7, byteArrayOf(3, 4), "Sending"))
                    dao.interruptSends("account-a")
                    assertEquals("Uncertain", dao.pending("account-a", "stable-operation")!!.state)
                    assertEquals("Sending", dao.pending("account-b", "stable-operation")!!.state)
                    dao.clearDrafts("account-a")
                    assertEquals("other server/account", dao.draft("account-b", 7)?.text)
                }
                open().withDatabase { db ->
                    assertArrayEquals(byteArrayOf(1, 2), db.chat().pending("account-a", "stable-operation")!!.wire)
                    assertEquals("Uncertain", db.chat().pending("account-a", "stable-operation")!!.state)
                    assertNull(db.chat().snapshot("account-a"))
                }
            } finally { context.deleteDatabase(name) }
        }
    }

    @Test fun roomPagingLoadsBeyondFiveHundredWithoutMaterializingWholeHistory() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val db = Room.inMemoryDatabaseBuilder(context, ChatDatabase::class.java).build()
        try {
            db.chat().put((1L..620L).map { CachedMessage("paging-a", it, 7, it, byteArrayOf(1)) })
            db.chat().put(listOf(CachedMessage("paging-b", 620, 7, 620, byteArrayOf(2))))
            val source = db.chat().pagedMessages("paging-a", 7)
            val page = source.load(androidx.paging.PagingSource.LoadParams.Refresh(550, 50, true))
                as androidx.paging.PagingSource.LoadResult.Page
            assertEquals(50, page.data.size)
            assertEquals(70L, page.data.first().id)
            assertEquals(21L, page.data.last().id)
            assertTrue(page.data.all { it.account == "paging-a" })
            assertEquals(550, page.itemsBefore)
            assertEquals(20, page.itemsAfter)
        } finally { db.close() }
    }

    @Test fun keystoreWrappingRejectsTamperingAndRemovesLocalSecrets() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val account = UUID.randomUUID().toString().replace("-", "").repeat(2)
        val secrets = SecretStore(context)
        val plaintext = "test-only secret ${UUID.randomUUID()}".toByteArray()
        val file = java.io.File(context.noBackupFilesDir, "accounts/$account")
        try {
            secrets.write(account, plaintext)
            assertArrayEquals(plaintext, secrets.read(account))
            assertFalse(file.readBytes().toString(Charsets.ISO_8859_1).contains(plaintext.toString(Charsets.UTF_8)))
            val damaged = file.readBytes().apply { this[lastIndex] = (this[lastIndex].toInt() xor 1).toByte() }
            file.writeBytes(damaged)
            assertTrue(runCatching { secrets.read(account) }.isFailure)
        } finally { secrets.remove(account) }
        assertFalse(file.exists())
        assertNull(secrets.read(account))
    }
}

private inline fun <T> ChatDatabase.withDatabase(block: (ChatDatabase) -> T): T = try { block(this) } finally { close() }
