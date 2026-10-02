package org.omachat.android

import android.app.Application
import android.net.ConnectivityManager
import android.net.Network
import androidx.lifecycle.*
import androidx.room.Room

class OmaChatApp : Application(), DefaultLifecycleObserver {
    lateinit var session: SessionCoordinator
        private set
    override fun onCreate() {
        super<Application>.onCreate()
        session = SessionCoordinator(this, Room.databaseBuilder(this, ChatDatabase::class.java, "omachat.db").addMigrations(ChatDatabase.MIGRATION_1_2, ChatDatabase.MIGRATION_2_3, ChatDatabase.MIGRATION_3_4, ChatDatabase.MIGRATION_4_5, ChatDatabase.MIGRATION_5_6).build())
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
        getSystemService(ConnectivityManager::class.java).registerDefaultNetworkCallback(object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = session.networkChanged()
            override fun onLost(network: Network) = session.networkChanged()
        })
    }
    override fun onStart(owner: LifecycleOwner) = session.foreground(true)
    override fun onStop(owner: LifecycleOwner) = session.foreground(false)
}
