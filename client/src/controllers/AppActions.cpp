// User intent -> omachatd IPC. Every action here maps onto a documented
// daemon method; nothing is executed through a shell.

#include "controllers/AppController.hpp"

#include "text/CommandParser.hpp"
#include "platform/ThemeProvider.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace omachat::client {

namespace {
// The daemon waits up to five minutes for the browser callback, then needs
// time to exchange the code and fetch the provider profile.
constexpr int kOAuthFlowTimeoutMs = 6 * 60 * 1000;
}

// ------------------------------------------------------------ connection

void AppController::retryDaemon()
{
    m_link.retry();
}

void AppController::switchAccount(const QString& accountId)
{
    m_addingAccount = false;
    emit authChanged();
    call(
        QStringLiteral("account.switch"), {{"account", accountId.toLongLong()}},
        [this](const QJsonObject& r) {
            if (r.value(QStringLiteral("left_voice")).toBool())
                showNotice(tr("Left voice: it belongs to the account you switched from."));
        },
        tr("Cannot switch account"));
}

void AppController::removeAccount(const QString& accountId)
{
    call(
        QStringLiteral("account.remove"), {{"account", accountId.toLongLong()}}, nullptr, tr("Could not remove account"));
}

void AppController::login(const QString& host, int port, const QString& username, const QString& password,
    bool registerAccount, const QString& displayName)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || password.isEmpty()) {
        m_authError = tr("Server, username and password are required.");
        emit authChanged();
        return;
    }
    m_authBusy = true;
    m_authError.clear();
    // The daemon makes this account the active one right away; from here the
    // pages follow its connection state like any other account's.
    m_addingAccount = false;
    emit authChanged();
    QJsonObject params{
        {"host", host.trimmed()}, {"port", port}, {"username", username.trimmed()}, {"password", password}};
    if (registerAccount && !displayName.trimmed().isEmpty())
        params.insert(QStringLiteral("display_name"), displayName.trimmed());
    m_link.request(registerAccount ? QStringLiteral("account.register") : QStringLiteral("account.login"), params,
        [this](const ipc::Reply& r) {
            m_authBusy = false;
            // Certificate problems are shown by the dedicated trust
            // dialog driven by the connection state, not here.
            m_authError = r.ok || r.errorCode == u"CertificateError" ? QString() : r.errorMessage;
            emit authChanged();
        });
}

void AppController::loginWithOAuth(const QString& host, int port, const QString& provider)
{
    if (host.trimmed().isEmpty()) {
        m_authError = tr("Server is required.");
        emit authChanged();
        return;
    }
    m_authBusy = true;
    m_authError.clear();
    emit authChanged();
    // This opens the user's browser and can take a while; the daemon reports
    // back once the whole flow (or a timeout) finishes.
    m_link.request(QStringLiteral("account.oauthLogin"), {{"host", host.trimmed()}, {"port", port}, {"provider", provider}},
        [this](const ipc::Reply& r) {
            m_authBusy = false;
            if (r.ok || r.errorCode == u"CertificateError")
                m_addingAccount = false;
            m_authError = r.ok || r.errorCode == u"CertificateError" ? QString() : r.errorMessage;
            emit authChanged();
        }, kOAuthFlowTimeoutMs);
}

void AppController::refreshOAuthIdentities()
{
    m_link.request(QStringLiteral("account.oauthIdentities"), {}, [this](const ipc::Reply& r) {
        if (!r.ok) {
            m_oauthLinkMessage = r.errorMessage.isEmpty() ? tr("Could not refresh linked accounts") : r.errorMessage;
            m_oauthLinkError = true;
            emit oauthIdentitiesChanged();
            return;
        }
        QVariantList list;
        for (const auto& v : r.result.value(QStringLiteral("identities")).toArray())
            list.append(v.toObject().toVariantMap());
        m_oauthIdentities = list;
        emit oauthIdentitiesChanged();
    });
}

void AppController::linkOAuthProvider(const QString& provider)
{
    if (m_oauthLinkBusy)
        return;
    m_oauthLinkBusy = true;
    m_oauthLinkMessage.clear();
    m_oauthLinkError = false;
    emit oauthIdentitiesChanged();
    // Opens the user's browser; the daemon reports back once the flow (or a
    // timeout) finishes.
    m_link.request(QStringLiteral("account.oauthLink"), {{"provider", provider}}, [this](const ipc::Reply& r) {
        m_oauthLinkBusy = false;
        if (r.ok) {
            m_oauthLinkMessage = tr("Account linked. Your profile was updated from this provider.");
            refreshOAuthIdentities();
        } else {
            m_oauthLinkMessage = r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage;
            m_oauthLinkError = true;
        }
        emit oauthIdentitiesChanged();
    }, kOAuthFlowTimeoutMs);
}

void AppController::unlinkOAuthProvider(const QString& provider)
{
    call(
        QStringLiteral("account.oauthUnlink"), {{"provider", provider}},
        [this](const QJsonObject&) { refreshOAuthIdentities(); }, tr("Could not unlink"));
}

void AppController::trustCertificate()
{
    call(QStringLiteral("certificate.trust"), {{"fingerprint", certificateFingerprint()}},
        [this](const QJsonObject&) { showNotice(tr("Certificate trusted. Log in to continue.")); });
}

void AppController::reconnect()
{
    call(QStringLiteral("connect"));
}

void AppController::logout()
{
    call(QStringLiteral("account.logout"), {}, [this](const QJsonObject&) {
        m_serversById.clear();
        m_channelsById.clear();
        m_selectedServer = QStringLiteral("home");
        m_selectedChannel.clear();
        m_messages.setChannel(QString());
        rebuildServers();
        rebuildChannels();
        rebuildMembers();
        emit selectionChanged();
        emit attachmentsChanged();
        emit sendOperationsChanged();
    });
}

// ------------------------------------------------------------ navigation

void AppController::persistSelection()
{
    if (!m_selectedChannel.isEmpty())
        m_lastChannelForServer.insert(m_selectedServer, m_selectedChannel);
    m_link.request(QStringLiteral("ui.focus"),
        {{"channel", m_selectedChannel}, {"server", homeSelected() ? QString() : m_selectedServer},
            {"focused", m_windowFocused}});
}

void AppController::selectHome()
{
    if (homeSelected())
        return;
    m_selectedServer = QStringLiteral("home");
    m_selectedChannel.clear();
    ensureSelection();
    cancelReply();
    rebuildServers();
    rebuildChannels();
    rebuildMembers();
    updateTyping();
    persistSelection();
    emit selectionChanged();
    emit attachmentsChanged();
    emit sendOperationsChanged();
}

void AppController::selectServer(const QString& id)
{
    if (id == u"home") {
        selectHome();
        return;
    }
    if (!m_serversById.contains(id) || id == m_selectedServer)
        return;
    m_selectedServer = id;
    m_selectedChannel.clear();
    ensureSelection();
    cancelReply();
    rebuildServers();
    rebuildChannels();
    rebuildMembers();
    updateTyping();
    persistSelection();
    emit selectionChanged();
    emit attachmentsChanged();
    emit sendOperationsChanged();
}

void AppController::selectChannel(const QString& id)
{
    const QJsonObject c = channel(id);
    if (c.isEmpty())
        return;
    const QString type = c.value(QStringLiteral("type")).toString();
    if (type == u"voice") {
        joinVoice(id);
        return;
    }
    if (type == u"category") {
        toggleCategory(id);
        return;
    }
    const QString server = c.value(QStringLiteral("server_id")).toString();
    if (server == u"0" && !homeSelected())
        m_selectedServer = QStringLiteral("home");
    else if (server != u"0" && server != m_selectedServer)
        m_selectedServer = server;
    const bool sameChannel = id == m_selectedChannel;
    if (!sameChannel)
        cancelReply();
    m_selectedChannel = id;
    if (sameChannel && !m_messages.anchorMessageId().isEmpty())
        m_messages.reload();
    else
        m_messages.setChannel(id);
    markRead(id);
    rebuildServers();
    rebuildChannels();
    if (homeSelected())
        rebuildMembers();
    updateTyping();
    persistSelection();
    emit selectionChanged();
    emit attachmentsChanged();
    emit sendOperationsChanged();
}

void AppController::selectRelativeChannel(int delta)
{
    QStringList ids;
    for (const auto& row : m_channels.rows()) {
        const QString type = row.value(QStringLiteral("rowType")).toString();
        if (type == u"text" || type == u"dm")
            ids << row.value(QStringLiteral("itemId")).toString();
    }
    if (ids.isEmpty())
        return;
    const qsizetype at = ids.indexOf(m_selectedChannel);
    const qsizetype next = at < 0 ? 0 : (at + delta + ids.size()) % ids.size();
    selectChannel(ids.at(next));
}

void AppController::toggleCategory(const QString& id)
{
    if (!m_collapsed.remove(id))
        m_collapsed.insert(id);
    rebuildChannels();
}

// -------------------------------------------------------------- messages

void AppController::sendMessage(const QString& channelId, const QString& content, const QString& replyTo, bool action,
    const QString& original, const QVariantList& files)
{
    if (m_sendOperations.size() >= 20) {
        showNotice(tr("Resolve or dismiss earlier sends before sending more."), true);
        emit composerRestore(original);
        return;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sendOperations.insert(id, {accountId(), channelId, content, replyTo, original, {}, files, action});
    dispatchSend(id);
}

QVariantList AppController::sendOperations() const
{
    QVariantList out;
    for (auto it = m_sendOperations.cbegin(); it != m_sendOperations.cend(); ++it) {
        const auto& op = it.value();
        if (op.account == accountId() && op.channel == m_selectedChannel)
            out.append(QVariantMap{{"id", it.key()}, {"text", op.original}, {"error", op.error},
                {"pending", op.pending}, {"retryable", op.retryable}});
    }
    return out;
}

int AppController::failedSendCount() const
{
    return static_cast<int>(std::ranges::count_if(m_sendOperations, [this](const auto& op) {
        return op.account == accountId() && !op.pending;
    }));
}

void AppController::reviewFailedSend()
{
    for (const auto& op : m_sendOperations) {
        if (op.account == accountId() && !op.pending) {
            selectChannel(op.channel);
            return;
        }
    }
}

void AppController::dispatchSend(const QString& id)
{
    auto& op = m_sendOperations[id];
    op.pending = true;
    op.error.clear();
    emit sendOperationsChanged();
    QJsonObject params{{"channel", op.channel}, {"content", op.content}, {"action", op.action}};
    if (!op.replyTo.isEmpty())
        params.insert(QStringLiteral("reply_to"), op.replyTo);
    QJsonArray paths;
    for (const auto& f : op.files)
        paths.append(f.toMap().value(QStringLiteral("path")).toString());
    if (!paths.isEmpty())
        params.insert(QStringLiteral("files"), paths);
    m_link.request(QStringLiteral("message.send"), params, [this, id](const ipc::Reply& r) {
        if (r.ok) {
            m_sendOperations.remove(id);
        } else {
            auto& failed = m_sendOperations[id];
            failed.pending = false;
            failed.error = r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage;
            // Only explicit rejection is safe to repeat. A transport failure or
            // timeout may have lost the acknowledgement after the server saved it.
            failed.retryable = QStringList{"BadRequest", "PermissionDenied", "NotFound", "RateLimited",
                "TooLarge", "StorageError", "AuthenticationError", "Conflict"}.contains(r.errorCode);
        }
        emit sendOperationsChanged();
    }, op.files.isEmpty() ? 20000 : 0);
}

void AppController::retrySend(const QString& id)
{
    const auto it = m_sendOperations.constFind(id);
    if (it == m_sendOperations.cend() || it->pending || !it->retryable
        || it->account != accountId() || it->channel != m_selectedChannel || !canSend()
        || state() != u"connected" || daemonState() != u"connected")
        return;
    dispatchSend(id);
}

void AppController::dismissSend(const QString& id)
{
    const auto it = m_sendOperations.constFind(id);
    if (it == m_sendOperations.cend() || it->pending || it->account != accountId())
        return;
    m_sendOperations.remove(id);
    emit sendOperationsChanged();
}

bool AppController::sendComposer(const QString& text)
{
    if (m_sendOperations.size() >= 20) {
        showNotice(tr("Resolve or dismiss earlier sends before sending more."), true);
        return false;
    }
    if (text.trimmed().isEmpty() && m_pendingFiles.isEmpty())
        return false;
    if (auto cmd = !text.trimmed().isEmpty() ? CommandParser::parse(text) : std::nullopt) {
        runCommand(*cmd, text);
        return true;
    }
    if (m_selectedChannel.isEmpty()) {
        showNotice(tr("Select a channel first."), true);
        return false;
    }
    if (!canSend()) {
        showNotice(tr("You do not have permission to send messages here."), true);
        return false;
    }
    const QVariantList files = std::exchange(m_pendingFiles, {});
    if (!files.isEmpty())
        emit attachmentsChanged();
    sendMessage(m_selectedChannel, CommandParser::unescape(text), m_replyTo, false, text, files);
    cancelReply();
    m_lastTypingSent = 0;
    return true;
}

void AppController::runCommand(const Command& c, const QString& original)
{
    const QString& n = c.name;
    if (n == u"help") {
        QStringList lines;
        for (const auto& info : CommandParser::commands())
            lines << info.usage;
        showNotice(lines.join(QStringLiteral("   ")));
    } else if (n == u"me") {
        if (!c.rest.isEmpty() && !m_selectedChannel.isEmpty())
            sendMessage(m_selectedChannel, c.rest, QString(), true, original);
    } else if (n == u"join") {
        call(QStringLiteral("channel.join"), {{"channel", c.rest}}, {}, tr("Cannot join"));
    } else if (n == u"leave") {
        leaveVoice();
    } else if (n == u"msg") {
        if (c.argument.isEmpty() || c.restAfterArgument.isEmpty()) {
            showNotice(tr("Usage: /msg <user> <text>"), true);
            emit composerRestore(original);
            return;
        }
        const QString body = c.restAfterArgument;
        call(
            QStringLiteral("dm.open"), {{"user", c.argument}},
            [this, body, original](const QJsonObject& dm) {
                m_channelsById.insert(dm.value(QStringLiteral("id")).toString(), dm);
                sendMessage(dm.value(QStringLiteral("id")).toString(), body, QString(), false, original);
            },
            tr("Cannot message %1").arg(c.argument));
    } else if (n == u"reply") {
        const QString target = m_replyTo.isEmpty() ? m_messages.newestIdFrom(selfId()) : m_replyTo;
        if (target.isEmpty() || c.rest.isEmpty()) {
            showNotice(tr("Nothing to reply to."), true);
            return;
        }
        sendMessage(m_selectedChannel, c.rest, target, false, original);
        cancelReply();
    } else if (n == u"mute") {
        call(QStringLiteral("voice.mute"));
    } else if (n == u"unmute") {
        call(QStringLiteral("voice.unmute"));
    } else if (n == u"deafen") {
        call(QStringLiteral("voice.deafen"));
    } else if (n == u"undeafen") {
        call(QStringLiteral("voice.undeafen"));
    } else if (n == u"topic") {
        if (c.rest.isEmpty())
            showNotice(selectedChannelTopic().isEmpty() ? tr("No topic set.") : selectedChannelTopic());
        else
            setTopic(c.rest);
    } else if (n == u"invite") {
        createInvite();
    } else if (n == u"kick" || n == u"ban") {
        if (c.argument.isEmpty() || homeSelected()) {
            showNotice(tr("Usage: /%1 <user> [reason]").arg(n), true);
            return;
        }
        call(QStringLiteral("moderation.") + n,
            {{"server", m_selectedServer}, {"user", c.argument}, {"reason", c.restAfterArgument}},
            [this, n, c](const QJsonObject&) {
                showNotice(n == u"kick" ? tr("%1 was kicked").arg(c.argument) : tr("%1 was banned").arg(c.argument));
            });
    } else if (n == u"status") {
        setPresence(c.argument);
    } else {
        showNotice(tr("Unknown command /%1 — type /help").arg(n), true);
        emit composerRestore(original);
    }
}

void AppController::editMessage(const QString& id, const QString& text)
{
    if (text.trimmed().isEmpty())
        return;
    m_link.request(
        QStringLiteral("message.edit"), {{"message", id}, {"content", text}}, [this, text](const ipc::Reply& r) {
            if (!r.ok) {
                showNotice(r.errorMessage, true);
                emit composerRestore(text);
            }
        });
}

void AppController::deleteMessage(const QString& id)
{
    call(QStringLiteral("message.delete"), {{"message", id}});
}

void AppController::toggleReaction(const QString& messageId, const QString& emoji)
{
    const int row = m_messages.rowOf(messageId);
    bool mine = false;
    if (row >= 0) {
        for (const auto& r : m_messages.get(row).value(QStringLiteral("reactions")).toList()) {
            const QVariantMap m = r.toMap();
            if (m.value(QStringLiteral("emoji")).toString() == emoji)
                mine = m.value(QStringLiteral("me")).toBool();
        }
    }
    call(QStringLiteral("message.react"), {{"message", messageId}, {"emoji", emoji}, {"add", !mine}});
}

void AppController::startReply(const QString& messageId)
{
    const int row = m_messages.rowOf(messageId);
    if (row < 0)
        return;
    const QVariantMap m = m_messages.get(row);
    m_replyTo = messageId;
    m_replyPreview = m.value(QStringLiteral("author_name")).toString() + QStringLiteral(": ")
        + MarkdownRenderer::plainPreview(m.value(QStringLiteral("content")).toString(), 80);
    emit replyChanged();
    emit focusComposer();
}

void AppController::cancelReply()
{
    if (m_replyTo.isEmpty())
        return;
    m_replyTo.clear();
    m_replyPreview.clear();
    emit replyChanged();
}

void AppController::notifyTyping()
{
    // Throttled: at most one typing notice per channel every 3 seconds.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_selectedChannel.isEmpty() || (m_lastTypingChannel == m_selectedChannel && now - m_lastTypingSent < 3000))
        return;
    m_lastTypingSent = now;
    m_lastTypingChannel = m_selectedChannel;
    m_link.request(QStringLiteral("typing"), {{"channel", m_selectedChannel}});
}

QVariantMap AppController::complete(const QString& textBeforeCursor) const
{
    // Finds the token being typed and offers matches for @user, #channel
    // or /command (commands only at the start of the message).
    qsizetype start = textBeforeCursor.size();
    while (start > 0 && !textBeforeCursor.at(start - 1).isSpace())
        --start;
    const QString token = textBeforeCursor.mid(start);
    QStringList candidates;
    if (token.startsWith(u'@')) {
        const QString prefix = token.mid(1).toLower();
        QStringList ids = homeSelected() ? QStringList() : m_membersByServer.value(m_selectedServer).keys();
        if (homeSelected()) {
            for (const auto& r : channel(m_selectedChannel).value(QStringLiteral("recipients")).toArray())
                ids << r.toString();
        }
        for (const auto& uid : ids) {
            const QString username = m_usersById.value(uid).value(QStringLiteral("username")).toString();
            if (!username.isEmpty() && (username.startsWith(prefix) || userName(uid).toLower().startsWith(prefix)))
                candidates << u'@' + username;
        }
    } else if (token.startsWith(u'#')) {
        const QString prefix = token.mid(1).toLower();
        for (const auto& c : m_channelsById) {
            if (c.value(QStringLiteral("server_id")).toString() == m_selectedServer
                && c.value(QStringLiteral("type")).toString() == u"text"
                && c.value(QStringLiteral("name")).toString().toLower().startsWith(prefix))
                candidates << u'#' + c.value(QStringLiteral("name")).toString();
        }
    } else if (token.startsWith(u'/') && start == 0) {
        for (const auto& info : CommandParser::commands()) {
            if (info.name.startsWith(token.mid(1).toLower()))
                candidates << u'/' + info.name;
        }
    } else if (token.startsWith(u':') && token.size() > 1) {
        const QString prefix = token.mid(1).toLower();
        for (const auto& v : emojiCatalog()) {
            const QVariantMap entry = v.toMap();
            const QString shortcode = entry.value(QStringLiteral("shortcode")).toString();
            if (shortcode.startsWith(prefix))
                candidates << entry.value(QStringLiteral("glyph")).toString();
        }
    }
    candidates.sort(Qt::CaseInsensitive);
    candidates.removeDuplicates();
    return {{"start", static_cast<int>(start)}, {"candidates", candidates}};
}

void AppController::copyText(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
    showNotice(tr("Copied to clipboard"));
}

void AppController::openLink(const QString& url)
{
    const QUrl u(url);
    const QString scheme = u.scheme().toLower();
    if (scheme == u"omachat") {
        if (u.host() == u"invite") {
            emit requestInviteJoin(url);
            return;
        }
        showNotice(tr("Unsupported OmaChat link"), true);
        return;
    }
    if (scheme == u"http" || scheme == u"https") {
        // Opened by the desktop's default browser; never executed here.
        QDesktopServices::openUrl(u);
        return;
    }
    showNotice(tr("Blocked a link with an unsupported scheme (%1)").arg(scheme), true);
}

void AppController::search(const QString& query, bool wholeServer)
{
    wholeServer = wholeServer && !homeSelected();
    if (query.trimmed().isEmpty() || (!wholeServer && m_selectedChannel.isEmpty())) {
        clearSearch();
        return;
    }
    const QJsonObject params = wholeServer ? QJsonObject{{"server", m_selectedServer}, {"query", query}, {"limit", 50}}
                                           : QJsonObject{{"channel", m_selectedChannel}, {"query", query}};
    const auto generation = ++m_searchGeneration;
    m_searchBusy = true;
    m_searchError.clear();
    m_searchResults.setRows({});
    m_searchMessages.clear();
    emit searchStateChanged();
    m_link.request(QStringLiteral("message.search"), params, [this, generation](const ipc::Reply& reply) {
        if (generation != m_searchGeneration)
            return;
        m_searchBusy = false;
        if (!reply.ok) {
            m_searchError = reply.errorMessage.isEmpty() ? reply.errorCode : reply.errorMessage;
            emit searchStateChanged();
            return;
        }
        QList<QVariantMap> rows;
        for (const auto& v : reply.result.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject m = v.toObject();
            const QString messageId = m.value(QStringLiteral("id")).toString();
            m_searchMessages.insert(messageId, m);
            const auto when
                = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m.value(QStringLiteral("timestamp")).toDouble()));
            const QString channelId = m.value(QStringLiteral("channel_id")).toString();
            rows.append({{"key", m.value(QStringLiteral("id")).toString()},
                {"itemId", m.value(QStringLiteral("id")).toString()}, {"channelId", channelId},
                {"channel", m_channelsById.value(channelId).value(QStringLiteral("name")).toString()},
                {"author", userName(m.value(QStringLiteral("author_id")).toString())},
                {"preview", MarkdownRenderer::plainPreview(m.value(QStringLiteral("content")).toString(), 160)},
                {"time", QLocale().toString(when, QLocale::ShortFormat)}});
        }
        m_searchResults.setRows(std::move(rows));
        emit searchStateChanged();
    });
}

void AppController::clearSearch()
{
    ++m_searchGeneration;
    m_searchBusy = false;
    m_searchError.clear();
    m_searchResults.setRows({});
    m_searchMessages.clear();
    emit searchStateChanged();
}

bool AppController::openSearchResult(const QString& messageId)
{
    const QJsonObject message = m_searchMessages.value(messageId);
    const QString channelId = message.value(QStringLiteral("channel_id")).toString();
    if (message.isEmpty() || channelId.isEmpty() || !m_channelsById.contains(channelId))
        return false;
    selectChannel(channelId);
    m_messages.openAt(channelId, message);
    return true;
}

// ----------------------------------------------------------------- voice

void AppController::joinVoice(const QString& channelId)
{
    if (voiceJoined() && voiceChannelId() == channelId)
        return;
    call(QStringLiteral("voice.join"), {{"channel", channelId}}, {}, tr("Cannot join voice"));
}

void AppController::leaveVoice()
{
    call(QStringLiteral("voice.leave"));
}
void AppController::toggleMute()
{
    call(QStringLiteral("voice.toggle_mute"));
}
void AppController::toggleDeafen()
{
    call(QStringLiteral("voice.toggle_deafen"));
}

void AppController::pushToTalk(bool pressed)
{
    m_link.request(pressed ? QStringLiteral("ptt.begin") : QStringLiteral("ptt.end"));
}

void AppController::setInputMode(const QString& mode)
{
    call(QStringLiteral("voice.mode"), {{"mode", mode}});
}

// ---------------------------------------------------------- screen sharing

bool AppController::canShareScreen() const
{
    return voiceJoined() && capabilities().contains(QStringLiteral("video.h264"))
        && channel(voiceChannelId()).value(QStringLiteral("can_stream")).toBool(true);
}

bool AppController::isStreaming(const QString& userId) const
{
    return m_voiceByUser.value(userId).value(QStringLiteral("streaming")).toBool();
}

QVariantList AppController::watchedStreams() const
{
    QVariantList out;
    for (const auto& v : voice().value(QStringLiteral("watching")).toArray()) {
        const QJsonObject w = v.toObject();
        const QString uid = w.value(QStringLiteral("user_id")).toString();
        // last_packet_ms/last_decoded_ms are epoch ms from the daemon; 0 means
        // "never happened yet" rather than "just now", so leave them raw and
        // let the QML side compare against Date.now().
        out.append(QVariantMap{{"userId", uid}, {"name", userName(uid)}, {"path", w.value(QStringLiteral("path"))},
            {"quality", w.value(QStringLiteral("quality")).toString(QStringLiteral("connecting"))},
            {"lastPacketMs", w.value(QStringLiteral("last_packet_ms")).toDouble()},
            {"lastDecodedMs", w.value(QStringLiteral("last_decoded_ms")).toDouble()},
            {"pointerActive", w.value(QStringLiteral("pointer_active")).toBool()},
            {"pointerX", w.value(QStringLiteral("pointer_x")).toDouble()},
            {"pointerY", w.value(QStringLiteral("pointer_y")).toDouble()},
            {"pointerAtMs", w.value(QStringLiteral("pointer_at_ms")).toDouble()}});
    }
    return out;
}

void AppController::toggleScreenShare()
{
    if (sharingScreen()) {
        call(QStringLiteral("stream.stop"));
        return;
    }
    if (m_shareStarting)
        return;
    if (m_videoSettings.value(QStringLiteral("audio"), false).toBool()) {
        emit requestScreenAudioConsent();
        return;
    }
    startScreenShare(false);
}

void AppController::startScreenShare(bool audio)
{
    if (sharingScreen() || m_shareStarting)
        return;
    m_shareStarting = true;
    emit voiceChanged();
    // The desktop shows its own screen/window picker; no timeout.
    m_link.request(
        QStringLiteral("stream.start"), {{"audio", audio}},
        [this](const ipc::Reply& r) {
            m_shareStarting = false;
            emit voiceChanged();
            if (!r.ok && r.errorMessage != u"cancelled")
                showNotice(tr("Cannot share your screen: %1").arg(r.errorMessage), true);
        },
        0);
}

void AppController::watchStream(const QString& userId)
{
    call(QStringLiteral("stream.watch"), {{"user", userId}}, {}, tr("Cannot watch %1").arg(userName(userId)));
}

void AppController::unwatchStream(const QString& userId)
{
    call(QStringLiteral("stream.unwatch"), {{"user", userId}});
}

void AppController::sendPointer(bool active, double x, double y)
{
    m_link.request(QStringLiteral("stream.pointer"), {{"active", active}, {"x", x}, {"y", y}});
}

void AppController::setUserVolume(const QString& userId, int percent)
{
    const double gain = std::clamp(percent, 0, 200) / 100.0;
    m_userVolumes.insert(userId, gain);
    call(QStringLiteral("audio.user_volume"), {{"user", userId}, {"volume", gain}});
}

int AppController::userVolume(const QString& userId) const
{
    return static_cast<int>(std::lround(m_userVolumes.value(userId, 1.0) * 100.0));
}

// --------------------------------------------------------------- servers

void AppController::createServer(const QString& name, const QString& description)
{
    call(
        QStringLiteral("server.create"), {{"name", name}, {"description", description}},
        [this](const QJsonObject& s) {
            const QString id = s.value(QStringLiteral("id")).toString();
            m_serversById.insert(id, s);
            loadSnapshot();
            QTimer::singleShot(200, this, [this, id] { selectServer(id); });
        },
        tr("Cannot create server"));
}

void AppController::createServerFromDiscord(const QString& name, const QVariantList& files)
{
    if (m_discordImportBusy || files.isEmpty())
        return;
    QJsonArray paths;
    for (const auto& item : files)
        paths.append(item.toUrl().toString());
    m_discordImportBusy = true;
    m_discordImportStatus = tr("Checking Discord exports…");
    emit discordImportChanged();
    m_link.request(QStringLiteral("server.create_from_discord"), {{"name", name}, {"files", paths}},
        [this](const ipc::Reply& r) {
            m_discordImportBusy = false;
            if (!r.ok) {
                m_discordImportStatus = r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage;
                emit discordImportChanged();
                emit discordImportFinished(false);
                return;
            }
            const QJsonObject s = r.result;
            const QString id = s.value(QStringLiteral("id")).toString();
            m_discordImportStatus = tr("Imported %1 messages").arg(s.value(QStringLiteral("imported")).toInt());
            emit discordImportChanged();
            m_serversById.insert(id, s);
            loadSnapshot();
            QTimer::singleShot(200, this, [this, id] { selectServer(id); });
            showNotice(tr("Created %1 with %2 imported messages")
                .arg(s.value(QStringLiteral("name")).toString())
                .arg(s.value(QStringLiteral("imported")).toInt()));
            emit discordImportFinished(true);
        }, 10 * 60 * 1000);
}

void AppController::joinServer(const QString& invite)
{
    call(
        QStringLiteral("server.join"), {{"invite", invite}},
        [this](const QJsonObject& s) {
            const QString id = s.value(QStringLiteral("id")).toString();
            showNotice(tr("Joined %1").arg(s.value(QStringLiteral("name")).toString()));
            m_serversById.insert(id, s);
            loadSnapshot();
            QTimer::singleShot(200, this, [this, id] { selectServer(id); });
        },
        tr("Cannot join"));
}

void AppController::createInvite()
{
    if (homeSelected())
        return;
    call(
        QStringLiteral("invite.create"), {{"server", m_selectedServer}},
        [this](const QJsonObject& r) {
            QGuiApplication::clipboard()->setText(r.value(QStringLiteral("uri")).toString());
            showNotice(tr("Invite link copied: %1").arg(r.value(QStringLiteral("uri")).toString()));
        },
        tr("Cannot create invite"));
}

void AppController::leaveServer(const QString& id)
{
    call(QStringLiteral("server.leave"), {{"server", id}});
}
void AppController::deleteServer(const QString& id)
{
    call(QStringLiteral("server.delete"), {{"server", id}});
}

void AppController::updateServerDetails(const QString& id, const QString& name, const QString& description)
{
    call(QStringLiteral("server.update"), {{"server", id}, {"name", name.trimmed()}, {"description", description}},
        [this, id](const QJsonObject& saved) {
            emit serverDetailsSaved(id);
            emit administrationFinished(QStringLiteral("server.update"), id, {}, saved.toVariantMap());
        }, tr("Cannot update server"), [this, id](const QString& error) {
            emit administrationFinished(QStringLiteral("server.update"), id, error);
        });
}

void AppController::setServerArtwork(const QString& id, const QString& kind, const QUrl& fileUrl)
{
    if (!fileUrl.isEmpty() && !fileUrl.isLocalFile()) {
        showNotice(tr("Choose a local image file."), true);
        return;
    }
    call(QStringLiteral("server.artwork.set"),
        {{"server", id}, {"kind", kind}, {"file", fileUrl.isEmpty() ? QString() : fileUrl.toLocalFile()}}, {},
        tr("Cannot update server image"));
}

void AppController::createChannel(const QString& name, const QString& type, const QString& parentId,
    const QString& topic, const QString& description, const QUrl& iconFile, const QUrl& bannerFile)
{
    QJsonObject params{{"server", m_selectedServer}, {"name", name}, {"type", type},
        {"topic", topic}, {"description", description}};
    if (!parentId.isEmpty() && parentId != u"0")
        params.insert(QStringLiteral("parent"), parentId);
    call(
        QStringLiteral("channel.create"), params,
        [this, iconFile, bannerFile](const QJsonObject& c) {
            m_channelsById.insert(c.value(QStringLiteral("id")).toString(), c);
            rebuildChannels();
            if (c.value(QStringLiteral("type")).toString() == u"text")
                selectChannel(c.value(QStringLiteral("id")).toString());
            const QString id = c.value(QStringLiteral("id")).toString();
            if (!iconFile.isEmpty())
                setChannelArtwork(id, QStringLiteral("icon"), iconFile);
            if (!bannerFile.isEmpty())
                setChannelArtwork(id, QStringLiteral("banner"), bannerFile);
            emit administrationFinished(QStringLiteral("channel.create"), id, {});
        },
        tr("Cannot create channel"), [this](const QString& error) {
            emit administrationFinished(QStringLiteral("channel.create"), {}, error);
        });
}

void AppController::deleteChannel(const QString& id)
{
    call(QStringLiteral("channel.delete"), {{"channel", id}});
}

void AppController::setTopic(const QString& topic)
{
    call(QStringLiteral("channel.update"), {{"channel", m_selectedChannel}, {"topic", topic}}, {},
        tr("Cannot set topic"));
}

void AppController::updateChannelDetails(const QString& id, const QString& name, const QString& topic,
    const QString& description)
{
    call(QStringLiteral("channel.update"),
        {{"channel", id}, {"name", name.trimmed()}, {"topic", topic}, {"description", description}},
        [this, id](const QJsonObject& saved) {
            emit channelDetailsSaved(id);
            emit administrationFinished(QStringLiteral("channel.update"), id, {}, saved.toVariantMap());
        },
        tr("Cannot update channel"), [this, id](const QString& error) {
            emit administrationFinished(QStringLiteral("channel.update"), id, error);
        });
}

void AppController::setChannelArtwork(const QString& id, const QString& kind, const QUrl& fileUrl)
{
    if (!fileUrl.isEmpty() && !fileUrl.isLocalFile()) {
        showNotice(tr("Choose a local image file."), true);
        return;
    }
    call(QStringLiteral("channel.artwork.set"),
        {{"channel", id}, {"kind", kind}, {"file", fileUrl.isEmpty() ? QString() : fileUrl.toLocalFile()}}, {},
        tr("Cannot update channel image"));
}

QVariantList AppController::channelCategories() const
{
    QVariantList out{{QVariantMap{{"id", "0"}, {"name", tr("Top level")}}}};
    for (const auto& c : m_channelsById) {
        if (c.value(QStringLiteral("server_id")).toString() == m_selectedServer
            && c.value(QStringLiteral("type")).toString() == u"category")
            out.append(QVariantMap{{"id", c.value(QStringLiteral("id")).toString()},
                {"name", c.value(QStringLiteral("name")).toString()}});
    }
    return out;
}

void AppController::moveChannelToCategory(const QString& id, const QString& parentId)
{
    call(QStringLiteral("channel.update"), {{"channel", id}, {"parent", parentId}}, {},
        tr("Cannot move channel"));
}

void AppController::moveChannelRelative(const QString& id, int delta)
{
    const QJsonObject current = channel(id);
    if (current.isEmpty() || !delta)
        return;
    const QString parent = current.value(QStringLiteral("parent_id")).toString();
    const bool category = current.value(QStringLiteral("type")).toString() == u"category";
    QList<QJsonObject> siblings;
    for (const auto& candidate : m_channelsById) {
        if (candidate.value(QStringLiteral("server_id")).toString() != m_selectedServer
            || candidate.value(QStringLiteral("parent_id")).toString() != parent
            || (candidate.value(QStringLiteral("type")).toString() == u"category") != category)
            continue;
        siblings.append(candidate);
    }
    std::sort(siblings.begin(), siblings.end(), [](const auto& a, const auto& b) {
        const int pa = a.value(QStringLiteral("position")).toInt();
        const int pb = b.value(QStringLiteral("position")).toInt();
        return pa == pb ? a.value(QStringLiteral("id")).toString().toULongLong()
                < b.value(QStringLiteral("id")).toString().toULongLong()
                        : pa < pb;
    });
    for (int i = 0; i < siblings.size(); ++i) {
        if (siblings.at(i).value(QStringLiteral("id")).toString() != id)
            continue;
        const int next = std::clamp(i + delta, 0, static_cast<int>(siblings.size()) - 1);
        if (next != i)
            call(QStringLiteral("channel.update"), {{"channel", id}, {"position", next}}, {},
                tr("Cannot reorder channel"));
        return;
    }
}

void AppController::setChannelMuted(const QString& id, bool muted)
{
    call(QStringLiteral("channel.mute"), {{"channel", id}, {"muted", muted}});
}

void AppController::openDm(const QString& userId)
{
    call(
        QStringLiteral("dm.open"), {{"user", userId}},
        [this](const QJsonObject& c) {
            const QString id = c.value(QStringLiteral("id")).toString();
            m_channelsById.insert(id, c);
            selectHome();
            selectChannel(id);
            emit focusComposer();
        },
        tr("Cannot open conversation"));
}

bool AppController::selectedEncrypted() const
{
    const QString type = selectedChannelType();
    return (type == u"dm" || type == u"group_dm") && capabilities().contains(QStringLiteral("e2e.v1"));
}

void AppController::loadSafetyNumbers()
{
    m_safetyNumbers.clear();
    emit safetyChanged();
    const QString channelId = m_selectedChannel;
    for (const QString& uid : channelRecipients(channelId)) {
        if (uid == selfId())
            continue;
        call(QStringLiteral("e2e.safety"), {{"user", uid}}, [this, channelId, uid](const QJsonObject& r) {
            if (channelId != m_selectedChannel)
                return;
            QVariantMap row = r.toVariantMap();
            row.insert(QStringLiteral("userId"), uid);
            row.insert(QStringLiteral("name"), userName(uid));
            // Replace or append, keeping a stable order.
            for (auto& v : m_safetyNumbers) {
                if (v.toMap().value(QStringLiteral("userId")) == uid) {
                    v = row;
                    emit safetyChanged();
                    return;
                }
            }
            m_safetyNumbers.append(row);
            emit safetyChanged();
        });
    }
}

void AppController::setVerified(const QString& userId, bool verified)
{
    call(
        QStringLiteral("e2e.verify"), {{"user", userId}, {"verified", verified}},
        [this](const QJsonObject&) { loadSafetyNumbers(); }, tr("Cannot change verification"));
}

QStringList AppController::channelRecipients(const QString& channelId) const
{
    QStringList out;
    for (const auto& r : channel(channelId).value(QStringLiteral("recipients")).toArray())
        out << r.toString();
    return out;
}

QVariantList AppController::knownUsers() const
{
    QVariantList out;
    for (auto it = m_usersById.cbegin(); it != m_usersById.cend(); ++it) {
        if (it.key() == selfId())
            continue;
        out.append(QVariantMap{{"userId", it.key()}, {"name", userName(it.key())},
            {"username", it->value(QStringLiteral("username")).toString()}, {"status", userStatus(it.key())}});
    }
    std::ranges::sort(out, [](const QVariant& a, const QVariant& b) {
        return a.toMap()
                   .value(QStringLiteral("name"))
                   .toString()
                   .localeAwareCompare(b.toMap().value(QStringLiteral("name")).toString())
            < 0;
    });
    return out;
}

void AppController::createGroup(const QStringList& userIds, const QString& name)
{
    call(
        QStringLiteral("dm.create"), {{"users", QJsonArray::fromStringList(userIds)}, {"name", name.trimmed()}},
        [this](const QJsonObject& c) {
            m_channelsById.insert(c.value(QStringLiteral("id")).toString(), c);
            selectHome();
            selectChannel(c.value(QStringLiteral("id")).toString());
        },
        tr("Cannot start the conversation"));
}

void AppController::addToGroup(const QString& channelId, const QStringList& userIds)
{
    for (const QString& uid : userIds)
        call(QStringLiteral("dm.add"), {{"channel", channelId}, {"user", uid}}, {},
            tr("Cannot add %1").arg(userName(uid)));
}

void AppController::renameGroup(const QString& channelId, const QString& name)
{
    call(QStringLiteral("channel.update"), {{"channel", channelId}, {"name", name.trimmed()}}, {},
        tr("Cannot rename the conversation"));
}

void AppController::leaveGroup(const QString& channelId)
{
    call(QStringLiteral("dm.leave"), {{"channel", channelId}}, {}, tr("Cannot leave the conversation"));
}

void AppController::kick(const QString& userId, const QString& reason)
{
    call(QStringLiteral("moderation.kick"), {{"server", m_selectedServer}, {"user", userId}, {"reason", reason}});
}

void AppController::ban(const QString& userId, const QString& reason)
{
    call(QStringLiteral("moderation.ban"), {{"server", m_selectedServer}, {"user", userId}, {"reason", reason}});
}

void AppController::setPresence(const QString& status)
{
    call(QStringLiteral("presence.set"), {{"status", status}});
}

void AppController::updateProfile(const QString& displayName, const QString& avatarUrl, const QString& bio)
{
    call(QStringLiteral("profile.update"),
        {{"display_name", displayName}, {"avatar_url", avatarUrl}, {"bio", bio}},
        [this](const QJsonObject&) { showNotice(tr("Profile saved")); }, tr("Could not save profile"));
}

QVariantMap AppController::userProfile(const QString& userId) const
{
    const QJsonObject user = userId == selfId() ? m_self : m_usersById.value(userId);
    return user.toVariantMap();
}

// -------------------------------------------------------- quick switcher

void AppController::switcherQuery(const QString& text)
{
    const QString q = text.trimmed().toLower();
    struct Hit {
        int score;
        QVariantMap row;
    };
    QList<Hit> hits;
    auto score = [&](const QString& label) -> int {
        const QString l = label.toLower();
        if (q.isEmpty())
            return 1;
        if (l == q)
            return 100;
        if (l.startsWith(q))
            return 80;
        if (l.contains(q))
            return 50;
        // Subsequence match ("dvlp" -> "development").
        qsizetype i = 0;
        for (QChar ch : l) {
            if (i < q.size() && ch == q.at(i))
                ++i;
        }
        return i == q.size() ? 20 : 0;
    };
    for (const auto& c : m_channelsById) {
        const QString type = c.value(QStringLiteral("type")).toString();
        if (type == u"category")
            continue;
        QString label = c.value(QStringLiteral("name")).toString();
        QString detail = m_serversById.value(c.value(QStringLiteral("server_id")).toString())
                             .value(QStringLiteral("name"))
                             .toString();
        if (type == u"dm") {
            for (const auto& r : c.value(QStringLiteral("recipients")).toArray())
                if (r.toString() != selfId())
                    label = userName(r.toString());
            detail = tr("Direct message");
        }
        const int s = score(label) + (c.value(QStringLiteral("id")).toString() == m_selectedChannel ? -5 : 0)
            + (m_unread.contains(c.value(QStringLiteral("id")).toString()) ? 5 : 0);
        if (s > 0)
            hits.append({s,
                {{"key", c.value(QStringLiteral("id")).toString()}, {"kind", type},
                    {"itemId", c.value(QStringLiteral("id")).toString()}, {"label", label}, {"detail", detail}}});
    }
    for (const auto& s : m_serversById) {
        const int sc = score(s.value(QStringLiteral("name")).toString());
        if (sc > 0)
            hits.append({sc - 1,
                {{"key", QStringLiteral("server:") + s.value(QStringLiteral("id")).toString()}, {"kind", "server"},
                    {"itemId", s.value(QStringLiteral("id")).toString()},
                    {"label", s.value(QStringLiteral("name")).toString()}, {"detail", tr("Server")}}});
    }
    for (auto it = m_usersById.cbegin(); it != m_usersById.cend(); ++it) {
        if (it.key() == selfId())
            continue;
        const int sc = std::max(score(userName(it.key())), score(it->value(QStringLiteral("username")).toString()));
        if (sc > 0 && !q.isEmpty())
            hits.append({sc - 2,
                {{"key", QStringLiteral("user:") + it.key()}, {"kind", "user"}, {"itemId", it.key()},
                    {"label", userName(it.key())},
                    {"detail", u'@' + it->value(QStringLiteral("username")).toString()}}});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    QList<QVariantMap> rows;
    for (const auto& h : hits) {
        rows.append(h.row);
        if (rows.size() >= 12)
            break;
    }
    m_switcher.setRows(std::move(rows));
}

void AppController::switcherActivate(int row)
{
    const QVariantMap r = m_switcher.get(row);
    const QString kind = r.value(QStringLiteral("kind")).toString();
    const QString id = r.value(QStringLiteral("itemId")).toString();
    if (kind == u"server")
        selectServer(id);
    else if (kind == u"user")
        openDm(id);
    else if (kind == u"voice")
        joinVoice(id);
    else
        selectChannel(id);
    emit focusComposer();
}

// ----------------------------------------------------------------- audio

void AppController::refreshAudio()
{
    m_link.request(QStringLiteral("audio.devices"), {}, [this](const ipc::Reply& r) {
        if (!r.ok)
            return;
        auto list = [](const QJsonArray& arr) {
            QVariantList out{QVariantMap{{"id", "default"}, {"name", tr("System default")}}};
            for (const auto& d : arr)
                out.append(d.toObject().toVariantMap());
            return out;
        };
        m_inputDevices = list(r.result.value(QStringLiteral("inputs")).toArray());
        m_outputDevices = list(r.result.value(QStringLiteral("outputs")).toArray());
        emit audioChanged();
    });
    m_link.request(QStringLiteral("audio.settings"), {}, [this](const ipc::Reply& r) {
        if (!r.ok)
            return;
        m_audioSettings = r.result.toVariantMap();
        emit audioChanged();
    });
    m_link.request(QStringLiteral("video.settings"), {}, [this](const ipc::Reply& r) {
        if (!r.ok)
            return;
        m_videoSettings = r.result.toVariantMap();
        emit audioChanged();
    });
}

void AppController::setVideo(const QString& key, const QVariant& value)
{
    m_link.request(QStringLiteral("video.set"), {{key, QJsonValue::fromVariant(value)}}, [this](const ipc::Reply& r) {
        if (!r.ok) {
            showNotice(r.errorMessage, true);
            return;
        }
        m_videoSettings = r.result.toVariantMap();
        emit audioChanged();
    });
}

void AppController::setAudio(const QString& key, const QVariant& value)
{
    m_link.request(QStringLiteral("audio.set"), {{key, QJsonValue::fromVariant(value)}}, [this](const ipc::Reply& r) {
        if (!r.ok) {
            showNotice(r.errorMessage, true);
            return;
        }
        m_audioSettings = r.result.toVariantMap();
        emit audioChanged();
    });
}

void AppController::setUi(double scale, bool reducedMotion)
{
    if (m_uiSaving)
        return;
    m_uiSaving = true;
    m_uiSaveFailed = false;
    m_uiSaveStatus = tr("Saving appearance…");
    emit configChanged();
    m_link.request(QStringLiteral("config.set_ui"),
        {{"scale", scale}, {"reduced_motion", reducedMotion}}, [this](const ipc::Reply& r) {
            m_uiSaving = false;
            m_uiSaveFailed = !r.ok;
            if (r.ok) {
                m_config.ui.scale = r.result.value(QStringLiteral("scale")).toDouble(1.0);
                m_config.ui.reducedMotion = r.result.value(QStringLiteral("reduced_motion")).toBool();
                ThemeProvider::instance()->setScale(m_config.ui.scale);
                ThemeProvider::instance()->setReducedMotion(m_config.ui.reducedMotion);
                m_uiSaveStatus = tr("Appearance saved");
            } else {
                m_uiSaveStatus = tr("Could not save appearance: %1").arg(r.errorMessage);
            }
            emit configChanged();
        });
}

void AppController::setNotification(const QString& key, bool enabled)
{
    if (key == u"messages")
        m_config.notifications.messages = enabled;
    else if (key == u"mentions")
        m_config.notifications.mentions = enabled;
    else if (key == u"voice_join")
        m_config.notifications.voiceJoin = enabled;
    emit configChanged();
    call(QStringLiteral("config.set_notifications"), {{key, enabled}});
}

void AppController::setWindowFocused(bool focused)
{
    if (focused == m_windowFocused)
        return;
    m_windowFocused = focused;
    if (focused)
        markRead(m_selectedChannel);
    persistSelection();
}

} // namespace omachat::client
