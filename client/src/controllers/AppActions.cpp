// User intent -> omachatd IPC. Every action here maps onto a documented
// daemon method; nothing is executed through a shell.

#include "controllers/AppController.hpp"

#include "text/CommandParser.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QUrl>

namespace omachat::client {

// ------------------------------------------------------------ connection

void AppController::retryDaemon()
{
    m_link.retry();
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
    if (id != m_selectedChannel)
        cancelReply();
    m_selectedChannel = id;
    m_messages.setChannel(id);
    markRead(id);
    rebuildServers();
    rebuildChannels();
    if (homeSelected())
        rebuildMembers();
    updateTyping();
    persistSelection();
    emit selectionChanged();
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
    QJsonObject params{{"channel", channelId}, {"content", content}, {"action", action}};
    if (!replyTo.isEmpty())
        params.insert(QStringLiteral("reply_to"), replyTo);
    QJsonArray paths;
    for (const auto& f : files)
        paths.append(f.toMap().value(QStringLiteral("path")).toString());
    if (!paths.isEmpty())
        params.insert(QStringLiteral("files"), paths);
    m_link.request(
        QStringLiteral("message.send"), params,
        [this, original, files](const ipc::Reply& r) {
            if (!r.ok) {
                showNotice(r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage, true);
                emit composerRestore(original); // never lose what the user typed
                if (!files.isEmpty() && m_pendingFiles.isEmpty()) {
                    m_pendingFiles = files; // nor what they attached
                    emit attachmentsChanged();
                }
            }
        },
        files.isEmpty() ? 20000 : 0); // uploads report progress through events
}

bool AppController::sendComposer(const QString& text)
{
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

void AppController::search(const QString& query)
{
    if (query.trimmed().isEmpty() || m_selectedChannel.isEmpty()) {
        clearSearch();
        return;
    }
    call(QStringLiteral("message.search"), {{"channel", m_selectedChannel}, {"query", query}},
        [this](const QJsonObject& r) {
            QList<QVariantMap> rows;
            for (const auto& v : r.value(QStringLiteral("messages")).toArray()) {
                const QJsonObject m = v.toObject();
                const auto when = QDateTime::fromMSecsSinceEpoch(
                    static_cast<qint64>(m.value(QStringLiteral("timestamp")).toDouble()));
                rows.append({{"key", m.value(QStringLiteral("id")).toString()},
                    {"itemId", m.value(QStringLiteral("id")).toString()},
                    {"author", userName(m.value(QStringLiteral("author_id")).toString())},
                    {"preview", MarkdownRenderer::plainPreview(m.value(QStringLiteral("content")).toString(), 160)},
                    {"time", QLocale().toString(when, QLocale::ShortFormat)}});
            }
            m_searchResults.setRows(std::move(rows));
            if (m_searchResults.count() == 0)
                showNotice(tr("No messages found"));
        });
}

void AppController::clearSearch()
{
    m_searchResults.setRows({});
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

void AppController::createServer(const QString& name)
{
    call(
        QStringLiteral("server.create"), {{"name", name}},
        [this](const QJsonObject& s) {
            const QString id = s.value(QStringLiteral("id")).toString();
            m_serversById.insert(id, s);
            loadSnapshot();
            QTimer::singleShot(200, this, [this, id] { selectServer(id); });
        },
        tr("Cannot create server"));
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

void AppController::createChannel(const QString& name, const QString& type, const QString& parentId)
{
    QJsonObject params{{"server", m_selectedServer}, {"name", name}, {"type", type}};
    if (!parentId.isEmpty() && parentId != u"0")
        params.insert(QStringLiteral("parent"), parentId);
    call(
        QStringLiteral("channel.create"), params,
        [this](const QJsonObject& c) {
            m_channelsById.insert(c.value(QStringLiteral("id")).toString(), c);
            rebuildChannels();
            if (c.value(QStringLiteral("type")).toString() == u"text")
                selectChannel(c.value(QStringLiteral("id")).toString());
        },
        tr("Cannot create channel"));
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
