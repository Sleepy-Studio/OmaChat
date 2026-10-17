package org.omachat.android

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.room.*
import kotlinx.coroutines.flow.Flow
import java.io.File
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

@Entity(tableName = "messages", primaryKeys = ["account", "id"], indices = [Index(value = ["account", "channel", "timestamp", "id"])])
data class CachedMessage(val account: String, val id: Long, val channel: Long, val timestamp: Long, val wire: ByteArray)
@Entity(tableName = "message_mutations", primaryKeys = ["account", "id"], indices = [Index(value = ["account", "revision"])])
data class MessageMutation(val account: String, val id: Long, val revision: Long, val deleted: Boolean)
@Entity(tableName = "outbox", primaryKeys = ["account", "operation"])
data class PendingSend(val account: String, val operation: String, val channel: Long,
    val wire: ByteArray, val state: String = "Queued", val error: String = "", val created: Long = System.currentTimeMillis())
@Entity(tableName = "drafts", primaryKeys = ["account", "channel"])
data class Draft(val account: String, val channel: Long, val text: String)
@Entity(tableName = "read_markers", primaryKeys = ["account", "channel"])
data class LocalReadMarker(val account: String, val channel: Long, val message: Long, val timestamp: Long)

@Entity(tableName = "snapshots")
data class CachedSnapshot(@PrimaryKey val account: String, val wire: ByteArray, @ColumnInfo(defaultValue = "''") val instanceId: String = "")

@Dao
interface ChatDao {
    @Query("SELECT * FROM snapshots WHERE account=:account") suspend fun snapshot(account: String): CachedSnapshot?
    @Upsert suspend fun put(snapshot: CachedSnapshot)
    @Query("DELETE FROM snapshots WHERE account=:account") suspend fun clearSnapshot(account: String)

    @Query("SELECT * FROM messages WHERE account=:account AND channel=:channel ORDER BY timestamp DESC,id DESC LIMIT 500")
    fun messages(account: String, channel: Long): Flow<List<CachedMessage>>
    @Query("SELECT * FROM messages WHERE account=:account AND channel=:channel ORDER BY timestamp DESC,id DESC")
    fun pagedMessages(account: String, channel: Long): androidx.paging.PagingSource<Int, CachedMessage>
    @Query("SELECT * FROM messages WHERE account=:account AND channel=:channel ORDER BY timestamp ASC,id ASC LIMIT 1")
    suspend fun oldest(account: String, channel: Long): CachedMessage?
    @Query("SELECT * FROM messages WHERE account=:account AND id=:id") suspend fun message(account: String, id: Long): CachedMessage?
    @Query("SELECT * FROM message_mutations WHERE account=:account AND id=:id") suspend fun mutation(account: String, id: Long): MessageMutation?
    @Query("SELECT COALESCE(MAX(revision),0) FROM message_mutations WHERE account=:account") suspend fun revision(account: String): Long
    @Upsert suspend fun put(mutation: MessageMutation)
    @Query("DELETE FROM message_mutations WHERE account=:account") suspend fun clearMutations(account: String)
    @Upsert suspend fun put(messages: List<CachedMessage>)
    @Query("DELETE FROM messages WHERE account=:account AND id=:id") suspend fun delete(account: String, id: Long)
    @Query("DELETE FROM messages WHERE account=:account AND channel=:channel") suspend fun clearChannel(account: String, channel: Long)
    @Query("DELETE FROM messages WHERE account=:account") suspend fun clearMessages(account: String)
    @Query("SELECT * FROM outbox WHERE account=:account ORDER BY created") fun outbox(account: String): Flow<List<PendingSend>>
    @Query("SELECT * FROM outbox WHERE account=:account AND state='Queued' ORDER BY created LIMIT 100")
    suspend fun queued(account: String): List<PendingSend>
    @Query("SELECT * FROM outbox WHERE account=:account AND operation=:operation") suspend fun pending(account: String, operation: String): PendingSend?
    @Upsert suspend fun put(send: PendingSend)
    @Query("DELETE FROM outbox WHERE account=:account AND operation=:operation") suspend fun cancel(account: String, operation: String)
    @Query("UPDATE outbox SET state='Uncertain', error='Connection ended before confirmation' WHERE account=:account AND state='Sending'")
    suspend fun interruptSends(account: String)
    @Query("SELECT * FROM drafts WHERE account=:account AND channel=:channel") suspend fun draft(account: String, channel: Long): Draft?
    @Upsert suspend fun put(draft: Draft)
    @Upsert suspend fun put(marker: LocalReadMarker)
    @Query("DELETE FROM outbox WHERE account=:account") suspend fun clearOutbox(account: String)
    @Query("DELETE FROM drafts WHERE account=:account") suspend fun clearDrafts(account: String)
    @Query("DELETE FROM read_markers WHERE account=:account") suspend fun clearMarkers(account: String)
}
@Database(entities = [CachedMessage::class, PendingSend::class, Draft::class, LocalReadMarker::class, CachedSnapshot::class, MessageMutation::class], version = 6, exportSchema = true)
abstract class ChatDatabase : RoomDatabase() {
    abstract fun chat(): ChatDao
    companion object {
        val MIGRATION_5_6 = object : androidx.room.migration.Migration(5, 6) {
            override fun migrate(db: androidx.sqlite.db.SupportSQLiteDatabase) {
                db.execSQL("CREATE INDEX IF NOT EXISTS index_message_mutations_account_revision ON message_mutations (account, revision)")
            }
        }
        val MIGRATION_4_5 = object : androidx.room.migration.Migration(4, 5) {
            override fun migrate(db: androidx.sqlite.db.SupportSQLiteDatabase) {
                db.execSQL("CREATE TABLE IF NOT EXISTS message_mutations (account TEXT NOT NULL, id INTEGER NOT NULL, revision INTEGER NOT NULL, deleted INTEGER NOT NULL, PRIMARY KEY(account, id))")
            }
        }
        val MIGRATION_3_4 = object : androidx.room.migration.Migration(3, 4) {
            override fun migrate(db: androidx.sqlite.db.SupportSQLiteDatabase) {
                db.execSQL("CREATE INDEX IF NOT EXISTS index_messages_account_channel_timestamp_id ON messages (account, channel, timestamp, id)")
            }
        }
        val MIGRATION_2_3 = object : androidx.room.migration.Migration(2, 3) {
            override fun migrate(db: androidx.sqlite.db.SupportSQLiteDatabase) {
                db.execSQL("ALTER TABLE snapshots ADD COLUMN instanceId TEXT NOT NULL DEFAULT ''")
            }
        }
        val MIGRATION_1_2 = object : androidx.room.migration.Migration(1, 2) {
            override fun migrate(db: androidx.sqlite.db.SupportSQLiteDatabase) {
                db.execSQL("CREATE TABLE IF NOT EXISTS snapshots (account TEXT NOT NULL PRIMARY KEY, wire BLOB NOT NULL)")
            }
        }
    }
}

/** The Keystore AES key wraps arbitrary protocol secrets; it is never substituted
 * for an X25519 identity. Files and key are excluded from backup. No secret logging. */
class SecretStore(context: Context) {
    private val directory = File(context.noBackupFilesDir, "accounts").apply { mkdirs() }
    private val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    private fun key(alias: String): SecretKey {
        (keyStore.getKey(alias, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        }.generateKey()
    }
    @Synchronized fun write(account: String, value: ByteArray) {
        require(account.matches(Regex("[a-f0-9]{64}")))
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key("omachat/$account"))
        val file = android.util.AtomicFile(File(directory, account))
        val stream = file.startWrite()
        try { stream.write(cipher.iv + cipher.doFinal(value)); file.finishWrite(stream) }
        catch (e: Exception) { file.failWrite(stream); throw e }
    }
    @Synchronized fun read(account: String): ByteArray? {
        require(account.matches(Regex("[a-f0-9]{64}")))
        val file = File(directory, account)
        if (!file.exists()) return null
        val bytes = file.readBytes()
        require(bytes.size in 28..65536)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key("omachat/$account"), GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
        return cipher.doFinal(bytes.copyOfRange(12, bytes.size))
    }
    @Synchronized fun remove(account: String) {
        File(directory, account).delete()
        keyStore.deleteEntry("omachat/$account")
    }
}
