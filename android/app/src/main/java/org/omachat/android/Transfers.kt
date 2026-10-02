package org.omachat.android

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import java.io.InputStream
import java.io.OutputStream

/** One foreground transfer at a time; never an unbounded queue of file workers. */
data class TransferProgress(
    val channel: Long,
    val direction: String,
    val stage: String,
    val bytes: Long = 0,
    val total: Long? = null,
    val active: Boolean = true
)
internal class Transfers {
    private val lock = Mutex()
    private val mutable = MutableStateFlow<TransferProgress?>(null)
    val state = mutable.asStateFlow()
    private var owner: Job? = null
    private val guard = Any()

    suspend fun <T> run(channel: Long, direction: String, block: suspend ((String, Long, Long?) -> Unit) -> T): T {
        check(lock.tryLock()) { "Finish or cancel the current file transfer first." }
        val job = currentCoroutineContext().job
        synchronized(guard) {
            owner = job
            mutable.value = TransferProgress(channel, direction, "Preparing")
        }
        var saving = false
        try {
            val result = block { stage, bytes, total ->
                saving = stage == "Saving file"
                mutable.value = TransferProgress(channel, direction, stage, bytes, total)
            }
            currentCoroutineContext().ensureActive()
            mutable.value = TransferProgress(channel, direction, "Complete", active = false)
            return result
        } catch (e: CancellationException) {
            mutable.value = TransferProgress(channel, direction, if (saving) "Cancelled · destination may contain a partial file" else "Cancelled", active = false)
            throw e
        } catch (e: Exception) {
            mutable.value = TransferProgress(channel, direction, "Failed: ${e.message ?: "transfer error"}" + if (saving) " · destination may contain a partial file" else "", active = false)
            throw e
        } finally {
            synchronized(guard) { owner = null }
            lock.unlock()
        }
    }
    fun cancel() { synchronized(guard) { owner?.cancel() } }
    suspend fun cancelAndJoin() {
        val job = synchronized(guard) { owner?.also { it.cancel() } }
        job?.join()
    }
    fun dismiss() { synchronized(guard) { if (owner == null) mutable.value = null } }
}

/** Checks cancellation between bounded reads/writes, including local provider I/O. */
internal suspend fun copyTransfer(input: InputStream, output: OutputStream, limit: Long, progress: (Long) -> Unit): Long {
    val buffer = ByteArray(65536)
    var total = 0L
    while (true) {
        currentCoroutineContext().ensureActive()
        val count = input.read(buffer)
        if (count < 0) break
        if (count == 0) continue
        check(count.toLong() <= limit - total) { "Attachment exceeds mobile limit" }
        output.write(buffer, 0, count)
        total += count
        progress(total)
    }
    currentCoroutineContext().ensureActive()
    return total
}
