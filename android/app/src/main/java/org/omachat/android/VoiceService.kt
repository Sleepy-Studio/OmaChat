package org.omachat.android

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.*
import kotlinx.coroutines.*

/** Started only from a visible user action. No sticky restart or credentials in intents. */
class VoiceService : Service() {
    private val session get() = (application as OmaChatApp).session
    private val audioManager by lazy { getSystemService(AudioManager::class.java) }
    private var focus: AudioFocusRequest? = null
    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        if (!finishing && change != AudioManager.AUDIOFOCUS_GAIN) session.leaveVoice("Audio focus interrupted voice. Join again when ready.")
    }
    private var previousMode: Int? = null
    private var finishing = false
    private var token = 0L
    override fun onBind(intent: Intent?) = null
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == LEAVE) {
            session.leaveVoice()
            if (token == 0L) stopSelf(startId)
            return START_NOT_STICKY
        }
        val requested = intent?.getLongExtra("request", 0) ?: 0
        try {
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(NotificationChannel(CHANNEL, "Voice calls", NotificationManager.IMPORTANCE_LOW))
            val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
            val leave = PendingIntent.getService(this, 1, Intent(this, VoiceService::class.java).setAction(LEAVE), PendingIntent.FLAG_IMMUTABLE)
            val notification = Notification.Builder(this, CHANNEL).setSmallIcon(org.omachat.android.R.drawable.ic_omachat)
                .setContentTitle("OmaChat voice").setContentText("Microphone active during voice · tap Leave to end")
                .setContentIntent(open).setOngoing(true).setCategory(Notification.CATEGORY_CALL)
                .addAction(Notification.Action.Builder(null, "Leave", leave).build()).build()
            if (Build.VERSION.SDK_INT >= 30) startForeground(41, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
            else startForeground(41, notification)
            // Every startForegroundService command must acknowledge promotion,
            // including stale commands arriving while an old instance is closing.
            if (requested == 0L || !session.voiceRequestValid(requested)) {
                finishOnMain(); return START_NOT_STICKY
            }
            if (token != 0L) {
                if (requested != token || finishing) {
                    session.leaveVoice("Previous voice service is closing. Try joining again.")
                    finishOnMain()
                }
                return START_NOT_STICKY
            }
            token = requested
            session.voiceServiceStarted(token, this)
        } catch (_: Exception) {
            session.leaveVoice("Microphone service could not start. Open OmaChat and join again.")
            finishOnMain()
        }
        return START_NOT_STICKY
    }
    internal suspend fun acquireFocus() = withContext(Dispatchers.Main.immediate) {
        check(!finishing)
        val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
            .setWillPauseWhenDucked(true)
            .setOnAudioFocusChangeListener(focusListener, Handler(Looper.getMainLooper())).build()
        focus = request
        check(audioManager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) { "Audio focus unavailable" }
        previousMode = audioManager.mode
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
    }
    internal suspend fun finish() = withContext(Dispatchers.Main.immediate) { finishOnMain() }
    private fun finishOnMain() {
        if (!finishing) {
            finishing = true
            focus?.let { audioManager.abandonAudioFocusRequest(it) }; focus = null
            previousMode?.let { if (audioManager.mode == AudioManager.MODE_IN_COMMUNICATION) audioManager.mode = it }; previousMode = null
        }
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }
    override fun onDestroy() {
        val unexpected = !finishing
        finishOnMain()
        if (unexpected) session.voiceServiceLost(token)
        super.onDestroy()
    }
    companion object {
        private const val CHANNEL = "voice"
        private const val LEAVE = "org.omachat.android.LEAVE_VOICE"
    }
}
