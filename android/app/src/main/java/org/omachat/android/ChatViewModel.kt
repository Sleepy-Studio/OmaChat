package org.omachat.android

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import androidx.paging.*
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.*
import omachat.proto.Network.ChatMessage

@OptIn(ExperimentalCoroutinesApi::class)
class ChatViewModel(application: Application) : AndroidViewModel(application) {
    val session = (application as OmaChatApp).session
    val ui = session.ui
    val messages = ui.map { it.endpoint?.key to it.selected }.distinctUntilChanged().flatMapLatest { (account, channel) ->
        if (account == null || channel == 0L) flowOf(PagingData.empty<ChatMessage>())
        else Pager(PagingConfig(pageSize = 50, initialLoadSize = 100, maxSize = 300, enablePlaceholders = true)) {
            session.dao.pagedMessages(account, channel)
        }.flow.map { page -> page.map { ChatMessage.parseFrom(it.wire) } }
    }.cachedIn(viewModelScope)
    val pending = ui.map { it.endpoint?.key }.distinctUntilChanged().flatMapLatest { account ->
        if (account == null) flowOf(emptyList()) else session.dao.outbox(account)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())
}
