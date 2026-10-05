#include "controllers/AppController.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"
#include "platform/ThemeProvider.hpp"
#include "text/CommandParser.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QKeySequence>
#include <QQmlEngine>

#include <algorithm>

namespace omachat::client {

namespace {
AppController* g_app = nullptr;

QString initials(const QString& name)
{
    const QStringList words = name.split(u' ', Qt::SkipEmptyParts);
    QString out;
    for (const auto& w : words) {
        out += w.front().toUpper();
        if (out.size() == 2)
            break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

} // namespace

AppController::AppController(const config::ClientConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_link(config.startup.launchDaemon)
    , m_servers({"key", "itemId", "name", "initials", "isHome", "unread", "mentions", "selected", "inVoice",
          "description", "iconAttachmentId", "bannerAttachmentId"})
    , m_channels({"key", "itemId", "rowType", "name", "depth", "unread", "mentions", "muted", "locked", "selected",
          "collapsed", "speaking", "userMuted", "userDeafened", "userId", "presence", "voiceCount", "topic",
          "streaming"})
    , m_members(
          {"key", "userId", "name", "username", "avatarUrl", "status", "nameColor", "section", "isOwner", "inVoice", "speaking"})
    , m_switcher({"key", "kind", "itemId", "label", "detail"})
    , m_searchResults({"key", "itemId", "channelId", "channel", "author", "preview", "time"})
{
    g_app = this;
    prunePastedImages();
    connect(&m_link, &DaemonLink::stateChanged, this, &AppController::daemonChanged);
    connect(&m_link, &DaemonLink::connected, this, &AppController::onDaemonConnected);
    connect(&m_link, &DaemonLink::eventReceived, this, &AppController::onEvent);

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(5000);
    connect(&m_noticeTimer, &QTimer::timeout, this, &AppController::dismissNotice);
    m_typingTimer.setSingleShot(true);
    connect(&m_typingTimer, &QTimer::timeout, this, &AppController::updateTyping);

    MessageListModel::Hooks hooks;
    hooks.displayName = [this](const QString& id) { return userName(id); };
    hooks.nameColor = [this](const QString& id) { return userColor(id); };
    hooks.render = [this](const QString& c) { return renderMarkdown(c); };
    hooks.selfId = [this] { return selfId(); };
    hooks.fetchPage = [this](const QString& channel, const QString& before, auto done) {
        QJsonObject params{{"channel", channel}, {"limit", 50}};
        if (!before.isEmpty())
            params.insert(QStringLiteral("before"), before);
        m_link.request(QStringLiteral("message.history"), params, [done](const ipc::Reply& r) {
            QList<QJsonObject> page;
            for (const auto& v : r.result.value(QStringLiteral("messages")).toArray())
                page.append(v.toObject());
            done(page, r.result.value(QStringLiteral("has_more")).toBool(), r.ok ? QString() : r.errorMessage);
        });
    };
    m_messages.setHooks(hooks);

    if (auto* theme = ThemeProvider::instance()) {
        auto applyPalette = [this, theme] {
            MarkdownRenderer::Palette p;
            p.codeBackground = theme->codeBackground();
            p.codeText = theme->text();
            p.link = theme->accent();
            p.quoteBar = theme->border();
            p.mention = theme->mention();
            // Rich text ignores alpha: pre-blend the highlight with the background.
            const QColor bg = theme->background();
            const QColor m = theme->mention();
            p.mentionBackground = QColor::fromRgbF(bg.redF() * 0.8f + m.redF() * 0.2f,
                bg.greenF() * 0.8f + m.greenF() * 0.2f, bg.blueF() * 0.8f + m.blueF() * 0.2f);
            m_markdown.setPalette(p);
            m_messages.refreshRendering();
        };
        connect(theme, &ThemeProvider::changed, this, applyPalette);
        applyPalette();
    }
}

AppController* AppController::instance()
{
    return g_app;
}

AppController* AppController::create(QQmlEngine*, QJSEngine*)
{
    QJSEngine::setObjectOwnership(g_app, QJSEngine::CppOwnership);
    return g_app;
}

void AppController::start()
{
    m_link.start();
}

QString AppController::version() const
{
    return QString::fromLatin1(kVersion);
}

// --------------------------------------------------------------- plumbing

void AppController::call(const QString& method, const QJsonObject& params, std::function<void(const QJsonObject&)> onOk,
    const QString& failurePrefix, std::function<void(const QString&)> onError)
{
    m_link.request(method, params, [this, onOk = std::move(onOk), failurePrefix, onError = std::move(onError)](const ipc::Reply& r) {
        if (!r.ok) {
            const QString msg = r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage;
            showNotice(failurePrefix.isEmpty() ? msg : failurePrefix + QStringLiteral(": ") + msg, true);
            if (onError)
                onError(msg);
            return;
        }
        if (onOk)
            onOk(r.result);
    });
}

void AppController::showNotice(const QString& text, bool error)
{
    m_notice = text;
    m_noticeError = error;
    m_noticeTimer.start(error ? 8000 : 4000);
    emit noticeChanged();
}

void AppController::dismissNotice()
{
    m_notice.clear();
    m_noticeTimer.stop();
    emit noticeChanged();
}

void AppController::onDaemonConnected()
{
    m_link.request(QStringLiteral("events.subscribe"), {});
    m_link.request(QStringLiteral("daemon.status"), {}, [this](const ipc::Reply& r) {
        if (r.ok)
            applyStatus(r.result);
    });
    m_link.request(QStringLiteral("config.get"), {}, [this](const ipc::Reply& r) {
        if (!r.ok)
            return;
        const QJsonObject n = r.result.value(QStringLiteral("notifications")).toObject();
        m_config.notifications.messages = n.value(QStringLiteral("messages")).toBool(true);
        m_config.notifications.mentions = n.value(QStringLiteral("mentions")).toBool(true);
        m_config.notifications.voiceJoin = n.value(QStringLiteral("voice_join")).toBool(false);
        const QJsonObject ui = r.result.value(QStringLiteral("ui")).toObject();
        m_config.ui.scale = ui.value(QStringLiteral("scale")).toDouble(m_config.ui.scale);
        m_config.ui.reducedMotion = ui.value(QStringLiteral("reduced_motion")).toBool(m_config.ui.reducedMotion);
        ThemeProvider::instance()->setScale(m_config.ui.scale);
        ThemeProvider::instance()->setReducedMotion(m_config.ui.reducedMotion);
        const QJsonObject sc = r.result.value(QStringLiteral("shortcuts")).toObject();
        for (auto it = sc.begin(); it != sc.end(); ++it)
            m_config.shortcuts.insert(it.key(), it.value().toString());
        emit configChanged();
    });
    loadSnapshot();
    refreshAudio();
    refreshTransfers();
}

void AppController::loadSnapshot()
{
    m_link.request(QStringLiteral("state.snapshot"), {}, [this](const ipc::Reply& r) {
        if (r.ok)
            applySnapshot(r.result);
    });
}

void AppController::applyStatus(const QJsonObject& status)
{
    const QString previousState = state();
    const QString previousAccount = accountId();
    const bool voiceBefore = voiceJoined();
    const QString voiceChannelBefore = voiceChannelId();
    m_status = status;
    if (state() != u"connected" || previousAccount != accountId()) {
        m_instanceOperator = false;
        m_instanceStatus = {};
        emit instanceStatusChanged();
    }
    const QJsonObject user = status.value(QStringLiteral("user")).toObject();
    if (!user.isEmpty())
        m_self = user;
    refreshArtworkCache();
    emit statusChanged();
    emit attachmentsChanged();
    emit sendOperationsChanged();
    emit voiceChanged();
    if (voiceBefore != voiceJoined() || voiceChannelBefore != voiceChannelId())
        rebuildServers();
    if (previousAccount != accountId())
        refreshTransfers();
    if (previousState != state() && state() == u"connected")
        loadSnapshot();
}

void AppController::applySnapshot(const QJsonObject& snap)
{
    if (!snap.value(QStringLiteral("valid")).toBool()) {
        m_serversById.clear();
        m_channelsById.clear();
        m_usersById.clear();
        m_membersByServer.clear();
        m_rolesById.clear();
        m_emojiById.clear();
        m_voiceByUser.clear();
        rebuildServers();
        rebuildChannels();
        rebuildMembers();
        m_messages.setChannel(QString());
        applyStatus(snap.value(QStringLiteral("status")).toObject());
        return;
    }
    m_self = snap.value(QStringLiteral("self")).toObject();
    auto index = [&](const char* key, QHash<QString, QJsonObject>& into) {
        into.clear();
        for (const auto& v : snap.value(QLatin1StringView(key)).toArray()) {
            const QJsonObject o = v.toObject();
            into.insert(o.value(QStringLiteral("id")).toString(), o);
        }
    };
    index("servers", m_serversById);
    index("channels", m_channelsById);
    index("users", m_usersById);
    ++m_profilesRevision;
    emit profilesChanged();
    index("roles", m_rolesById);
    index("emoji", m_emojiById);
    emit emojiListChanged();
    m_membersByServer.clear();
    for (const auto& v : snap.value(QStringLiteral("members")).toArray()) {
        const QJsonObject m = v.toObject();
        m_membersByServer[m.value(QStringLiteral("server_id")).toString()].insert(
            m.value(QStringLiteral("user_id")).toString(), m);
    }
    m_voiceByUser.clear();
    for (const auto& v : snap.value(QStringLiteral("voice_states")).toArray()) {
        const QJsonObject vs = v.toObject();
        m_voiceByUser.insert(vs.value(QStringLiteral("user_id")).toString(), vs);
    }
    m_mutedChannels.clear();
    for (const auto& v : snap.value(QStringLiteral("muted_channels")).toArray())
        m_mutedChannels.insert(v.toString());
    const QJsonObject volumes = snap.value(QStringLiteral("user_volumes")).toObject();
    m_userVolumes.clear();
    for (auto it = volumes.begin(); it != volumes.end(); ++it)
        m_userVolumes.insert(it.key(), it.value().toDouble());
    if (snap.contains(QStringLiteral("status")))
        applyStatus(snap.value(QStringLiteral("status")).toObject());

    if (!m_initialSelectionDone && !m_serversById.isEmpty()) {
        // First load: open the first server rather than an empty DM list.
        m_initialSelectionDone = true;
        if (homeSelected() && m_selectedChannel.isEmpty())
            m_selectedServer.clear();
    }
    ensureSelection();
    refreshArtworkCache(true);
    rebuildServers();
    rebuildChannels();
    rebuildMembers();
    m_messages.refreshRendering();
    emit selectionChanged();
    emit attachmentsChanged();
    emit sendOperationsChanged();
    refreshInstanceStatus();
}

void AppController::refreshInstanceStatus()
{
    if (state() != u"connected")
        return;
    const QString requestedAccount = accountId();
    m_link.request(QStringLiteral("instance.status"), {}, [this, requestedAccount](const ipc::Reply& r) {
        if (requestedAccount != accountId())
            return;
        m_instanceOperator = r.ok;
        m_instanceStatus = r.ok ? r.result : QJsonObject{};
        emit instanceStatusChanged();
    });
}

void AppController::setInstanceRegistration(bool open)
{
    call(
        QStringLiteral("instance.registration"), {{"open", open}},
        [this](const QJsonObject&) { refreshInstanceStatus(); }, tr("Cannot change registration"));
}

void AppController::setInstanceSuspension(const QString& userId, bool suspended)
{
    call(
        QStringLiteral("instance.suspend"), {{"user_id", userId}, {"suspended", suspended}},
        [this](const QJsonObject&) { refreshInstanceStatus(); }, tr("Cannot update account"));
}

void AppController::deleteInstanceCommunity(const QString& serverId)
{
    call(
        QStringLiteral("instance.community_delete"), {{"server_id", serverId}},
        [this](const QJsonObject&) { refreshInstanceStatus(); }, tr("Cannot delete community"));
}

void AppController::moderateInstanceCommunity(const QString& serverId, const QString& userId, const QString& action)
{
    call(
        QStringLiteral("instance.moderate"), {{"server_id", serverId}, {"user_id", userId}, {"action", action}},
        [this](const QJsonObject&) { refreshInstanceStatus(); }, tr("Cannot moderate community"));
}

void AppController::restartInstance()
{
    call(QStringLiteral("instance.restart"), {}, {}, tr("Cannot restart instance"));
}

// ----------------------------------------------------------------- events

void AppController::onEvent(const QString& name, const QJsonObject& data)
{
    auto id = [&](const char* key) { return data.value(QLatin1StringView(key)).toString(); };

    if (name == u"status" || name == u"connection") {
        applyStatus(data);
        return;
    }
    if (name == u"transfer.progress") {
        onTransferProgress(data);
        return;
    }
    if (name == u"discord.import_progress") {
        m_discordImportStatus = tr("Importing Discord history: %1 of %2 batches, %3 messages")
                                    .arg(data.value(QStringLiteral("completed")).toInt())
                                    .arg(data.value(QStringLiteral("total")).toInt())
                                    .arg(data.value(QStringLiteral("messages")).toInt());
        emit discordImportChanged();
        return;
    }
    if (name == u"state.reset") {
        loadSnapshot();
        return;
    }
    if (name == u"read.marker") {
        const QString channelId = id("channel_id");
        const qint64 at = static_cast<qint64>(data.value(QStringLiteral("timestamp")).toDouble());
        const quint64 messageId = id("message_id").toULongLong();
        auto& unread = m_unreadMessages[channelId];
        unread.removeIf([&](const QJsonObject& msg) {
            const qint64 timestamp = static_cast<qint64>(msg.value(QStringLiteral("timestamp")).toDouble());
            return timestamp < at
                || (timestamp == at && msg.value(QStringLiteral("id")).toString().toULongLong() <= messageId);
        });
        m_unread[channelId] = static_cast<int>(unread.size());
        m_mentions[channelId] = static_cast<int>(std::ranges::count_if(unread, [&](const QJsonObject& msg) {
            return msg.value(QStringLiteral("mentions_me")).toBool()
                || channel(channelId).value(QStringLiteral("type")).toString() == u"dm";
        }));
        rebuildServers();
        rebuildChannels();
        return;
    }
    if (name == u"message.created") {
        const QString channelId = id("channel_id");
        if (channelId == m_selectedChannel) {
            m_messages.addMessage(data);
        } else if (id("author_id") != selfId()) {
            m_unreadMessages[channelId].append(data);
            m_unread[channelId] += 1;
            if (data.value(QStringLiteral("mentions_me")).toBool()
                || channel(channelId).value(QStringLiteral("type")).toString() == u"dm")
                m_mentions[channelId] += 1;
            rebuildServers();
            rebuildChannels();
        }
        // A message ends that author's typing indicator.
        if (m_typing.contains(channelId) && m_typing[channelId].remove(id("author_id")))
            updateTyping();
        return;
    }
    if (name == u"message.updated") {
        m_messages.updateMessage(data);
        return;
    }
    if (name == u"message.deleted") {
        m_messages.removeMessage(id("message_id"));
        return;
    }
    if (name == u"reaction") {
        m_messages.applyReaction(
            id("message_id"), id("emoji"), data.value(QStringLiteral("add")).toBool(), id("user_id") == selfId());
        return;
    }
    if (name == u"typing") {
        if (id("user_id") == selfId())
            return;
        m_typing[id("channel_id")][id("user_id")] = QDateTime::currentMSecsSinceEpoch() + 5000;
        updateTyping();
        return;
    }
    if (name == u"channel.created" || name == u"channel.updated") {
        m_channelsById.insert(id("id"), data);
        rebuildChannels();
        emit channelDataChanged(id("id"));
        if (id("id") == m_selectedChannel)
            emit selectionChanged();
        return;
    }
    if (name == u"channel.deleted") {
        m_channelsById.remove(id("channel_id"));
        ensureSelection();
        rebuildChannels();
        return;
    }
    if (name == u"server.updated") {
        m_serversById.insert(id("id"), data);
        rebuildServers();
        emit serverDataChanged(id("id"));
        emit selectionChanged();
        return;
    }
    if (name == u"server.removed") {
        const QString sid = id("server_id");
        m_serversById.remove(sid);
        for (auto it = m_channelsById.begin(); it != m_channelsById.end();)
            it = it->value(QStringLiteral("server_id")).toString() == sid ? m_channelsById.erase(it) : std::next(it);
        m_membersByServer.remove(sid);
        if (id("reason") == u"kicked" || id("reason") == u"banned")
            showNotice(tr("You were %1 from a server").arg(id("reason")), true);
        ensureSelection();
        rebuildServers();
        rebuildChannels();
        rebuildMembers();
        return;
    }
    if (name == u"member.joined" || name == u"member.updated") {
        m_membersByServer[id("server_id")].insert(id("user_id"), data);
        rebuildMembers();
        return;
    }
    if (name == u"member.left") {
        m_membersByServer[id("server_id")].remove(id("user_id"));
        rebuildMembers();
        return;
    }
    if (name == u"user.updated") {
        m_usersById.insert(id("id"), data);
        ++m_profilesRevision;
        emit profilesChanged();
        if (id("id") == selfId()) {
            m_self = data;
            emit statusChanged();
        }
        rebuildMembers();
        rebuildChannels();
        m_messages.refreshRendering();
        return;
    }
    if (name == u"presence") {
        auto it = m_usersById.find(id("user_id"));
        if (it != m_usersById.end())
            it->insert(QStringLiteral("status"), id("status"));
        if (id("user_id") == selfId()) {
            m_self.insert(QStringLiteral("status"), id("status"));
            emit statusChanged();
        }
        rebuildMembers();
        rebuildChannels();
        return;
    }
    if (name == u"role.updated") {
        m_rolesById.insert(id("id"), data);
        rebuildMembers();
        return;
    }
    if (name == u"role.deleted") {
        m_rolesById.remove(id("role_id"));
        rebuildMembers();
        return;
    }
    if (name == u"emoji.added") {
        m_emojiById.insert(id("id"), data);
        emit emojiListChanged();
        return;
    }
    if (name == u"emoji.removed") {
        m_emojiById.remove(id("emoji_id"));
        emit emojiListChanged();
        return;
    }
    if (name == u"voice.state") {
        if (id("channel_id") == u"0")
            m_voiceByUser.remove(id("user_id"));
        else
            m_voiceByUser.insert(id("user_id"), data);
        rebuildChannels();
        rebuildMembers();
        return;
    }
    if (name == u"voice.speaking") {
        if (data.value(QStringLiteral("speaking")).toBool())
            m_speaking.insert(id("user_id"));
        else
            m_speaking.remove(id("user_id"));
        rebuildChannels();
        if (id("user_id") == selfId())
            emit voiceChanged();
        return;
    }
    if (name == u"e2e.keys_changed") {
        if (data.value(QStringLiteral("self")).toBool())
            showNotice(tr("A new device was added to your account. If it was not you, change your password."), true);
        else
            showNotice(tr("%1's security keys changed (a new or removed device). Compare safety numbers from the lock "
                          "icon in your conversation.")
                           .arg(userName(id("user_id"))),
                true);
        return;
    }
    if (name == u"stream.ended") {
        const QString uid = id("user_id");
        if (uid.isEmpty() || uid == selfId())
            showNotice(tr("Screen sharing stopped"));
        else
            showNotice(tr("%1 stopped sharing").arg(userName(uid)));
        return;
    }
    if (name == u"voice.error") {
        showNotice(data.value(QStringLiteral("message")).toString(), true);
        return;
    }
    if (name == u"channel.muted") {
        if (data.value(QStringLiteral("muted")).toBool())
            m_mutedChannels.insert(id("channel_id"));
        else
            m_mutedChannels.remove(id("channel_id"));
        rebuildChannels();
        rebuildServers();
        return;
    }
    if (name == u"audio.devices") {
        refreshAudio();
        return;
    }
    if (name == u"ui.navigate") {
        const QString sid = id("server_id");
        if (sid == u"0")
            selectHome();
        else if (m_serversById.contains(sid))
            selectServer(sid);
        selectChannel(id("channel_id"));
        emit focusComposer();
        return;
    }
}

void AppController::updateTyping()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 next = 0;
    for (auto cit = m_typing.begin(); cit != m_typing.end();) {
        for (auto uit = cit->begin(); uit != cit->end();) {
            if (uit.value() <= now) {
                uit = cit->erase(uit);
            } else {
                next = next ? std::min(next, uit.value()) : uit.value();
                ++uit;
            }
        }
        cit = cit->isEmpty() ? m_typing.erase(cit) : std::next(cit);
    }
    QStringList names;
    for (auto it = m_typing.value(m_selectedChannel).cbegin(); it != m_typing.value(m_selectedChannel).cend(); ++it)
        names << userName(it.key());
    QString text;
    if (names.size() == 1)
        text = tr("%1 is typing…").arg(names.first());
    else if (names.size() == 2)
        text = tr("%1 and %2 are typing…").arg(names.at(0), names.at(1));
    else if (names.size() > 2)
        text = tr("Several people are typing…");
    if (text != m_typingText) {
        m_typingText = text;
        emit typingChanged();
    }
    // Wake only when the next indicator expires; nothing runs while idle.
    if (next)
        m_typingTimer.start(static_cast<int>(std::max<qint64>(50, next - now)));
}

// ------------------------------------------------------------ derivations

bool AppController::ready() const
{
    return state() == u"connected"
        || ((state() == u"reconnecting" || state() == u"offline") && !m_serversById.isEmpty());
}

QString AppController::errorCode() const
{
    return m_status.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

QString AppController::errorMessage() const
{
    return m_status.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
}

QString AppController::certificateFingerprint() const
{
    return m_status.value(QStringLiteral("error")).toObject().value(QStringLiteral("fingerprint")).toString();
}

QString AppController::voiceServerName() const
{
    const QJsonObject c = channel(voiceChannelId());
    return m_serversById.value(c.value(QStringLiteral("server_id")).toString())
        .value(QStringLiteral("name"))
        .toString();
}

QString AppController::selectedServerName() const
{
    if (homeSelected())
        return tr("Direct Messages");
    return m_serversById.value(m_selectedServer).value(QStringLiteral("name")).toString();
}

QString AppController::selectedServerDescription() const
{
    if (homeSelected())
        return {};
    return m_serversById.value(m_selectedServer).value(QStringLiteral("description")).toString();
}

QString AppController::selectedServerBannerId() const
{
    if (homeSelected())
        return {};
    return m_serversById.value(m_selectedServer).value(QStringLiteral("banner_attachment_id")).toString();
}

QVariantMap AppController::serverDetails(const QString& id) const
{
    return m_serversById.value(id).toVariantMap();
}

QString AppController::selectedChannelName() const
{
    return channel(m_selectedChannel).value(QStringLiteral("name")).toString();
}

QString AppController::selectedChannelTopic() const
{
    return channel(m_selectedChannel).value(QStringLiteral("topic")).toString();
}

QString AppController::selectedChannelDescription() const
{
    return channel(m_selectedChannel).value(QStringLiteral("description")).toString();
}

QString AppController::selectedChannelBannerId() const
{
    return channel(m_selectedChannel).value(QStringLiteral("banner_attachment_id")).toString();
}

QString AppController::selectedChannelType() const
{
    return channel(m_selectedChannel).value(QStringLiteral("type")).toString();
}

bool AppController::channelFlag(const QString& flag) const
{
    return channel(m_selectedChannel).value(flag).toBool();
}

bool AppController::canSend() const
{
    return channelFlag(QStringLiteral("can_send"));
}
bool AppController::canManageMessages() const
{
    return channelFlag(QStringLiteral("can_manage_messages"));
}

bool AppController::serverPermission(const char* name) const
{
    const QJsonArray perms = m_serversById.value(m_selectedServer).value(QStringLiteral("permissions")).toArray();
    return perms.contains(QString::fromLatin1(name)) || perms.contains(QStringLiteral("ADMINISTRATOR"))
        || isServerOwner();
}

bool AppController::canManageChannels() const
{
    return !homeSelected() && serverPermission("MANAGE_CHANNEL");
}
bool AppController::canManageServer() const
{
    return !homeSelected() && serverPermission("MANAGE_SERVER");
}
bool AppController::canCreateInvites() const
{
    return !homeSelected() && serverPermission("CREATE_INVITES");
}
bool AppController::canKick() const
{
    return !homeSelected() && serverPermission("KICK_MEMBERS");
}
bool AppController::canBan() const
{
    return !homeSelected() && serverPermission("BAN_MEMBERS");
}

bool AppController::isServerOwner() const
{
    return !homeSelected() && m_serversById.value(m_selectedServer).value(QStringLiteral("is_owner")).toBool();
}

QString AppController::userName(const QString& userId) const
{
    const QJsonObject u = m_usersById.value(userId);
    const QString n = u.value(QStringLiteral("display_name")).toString();
    if (!n.isEmpty())
        return n;
    if (userId == selfId())
        return selfName();
    return u.value(QStringLiteral("username")).toString(tr("Unknown user"));
}

QString AppController::userStatus(const QString& userId) const
{
    if (userId == selfId())
        return selfStatus();
    return m_usersById.value(userId).value(QStringLiteral("status")).toString(QStringLiteral("offline"));
}

QString AppController::roleColorFor(const QString& serverId, const QString& userId) const
{
    const QJsonObject member = m_membersByServer.value(serverId).value(userId);
    int best = -1;
    QString color;
    for (const auto& r : member.value(QStringLiteral("roles")).toArray()) {
        const QJsonObject role = m_rolesById.value(r.toString());
        const int pos = role.value(QStringLiteral("position")).toInt();
        if (role.value(QStringLiteral("has_color")).toBool() && pos > best) {
            best = pos;
            color = role.value(QStringLiteral("color")).toString();
        }
    }
    return color;
}

QString AppController::userColor(const QString& userId) const
{
    const QString role = homeSelected() ? QString() : roleColorFor(m_selectedServer, userId);
    if (!role.isEmpty())
        return role;
    if (auto* theme = ThemeProvider::instance())
        return theme->userColor(userId).name();
    return QStringLiteral("#cccccc");
}

QString AppController::renderMarkdown(const QString& content) const
{
    return m_markdown.render(content, selfUsername());
}

QVariantMap AppController::shortcuts() const
{
    QVariantMap out;
    for (auto it = m_config.shortcuts.cbegin(); it != m_config.shortcuts.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

int AppController::pushToTalkKey() const
{
    const QKeySequence seq(m_config.shortcuts.value(QStringLiteral("push_to_talk"), QStringLiteral("F8")));
    return seq.isEmpty() ? Qt::Key_F8 : seq[0].key();
}

QVariantList AppController::commandHelp() const
{
    QVariantList out;
    for (const auto& c : CommandParser::commands())
        out.append(QVariantMap{{"name", c.name}, {"usage", c.usage}, {"description", c.description}});
    return out;
}

QVariantMap AppController::notificationSettings() const
{
    return {{"messages", m_config.notifications.messages}, {"mentions", m_config.notifications.mentions},
        {"voice_join", m_config.notifications.voiceJoin}};
}

// ------------------------------------------------------------------- rows

void AppController::ensureSelection()
{
    if (m_selectedServer.isEmpty() || (!homeSelected() && !m_serversById.contains(m_selectedServer))) {
        // Prefer the first server; fall back to direct messages.
        QStringList ids = m_serversById.keys();
        std::sort(ids.begin(), ids.end(),
            [](const QString& a, const QString& b) { return a.toULongLong() < b.toULongLong(); });
        m_selectedServer = ids.isEmpty() ? QStringLiteral("home") : ids.first();
    }
    const QJsonObject current = channel(m_selectedChannel);
    const QString scope = homeSelected() ? QStringLiteral("0") : m_selectedServer;
    const bool valid = !current.isEmpty() && current.value(QStringLiteral("server_id")).toString() == scope
        && current.value(QStringLiteral("type")).toString() != u"voice"
        && current.value(QStringLiteral("type")).toString() != u"category";
    if (!valid) {
        QString pick = m_lastChannelForServer.value(m_selectedServer);
        const QJsonObject remembered = channel(pick);
        if (remembered.isEmpty() || remembered.value(QStringLiteral("server_id")).toString() != scope) {
            pick.clear();
            quint64 best = UINT64_MAX;
            int bestPos = INT_MAX;
            for (const auto& c : m_channelsById) {
                const QString type = c.value(QStringLiteral("type")).toString();
                if (c.value(QStringLiteral("server_id")).toString() != scope || (type != u"text" && type != u"dm"))
                    continue;
                const int pos = c.value(QStringLiteral("position")).toInt();
                const quint64 cid = c.value(QStringLiteral("id")).toString().toULongLong();
                if (pos < bestPos || (pos == bestPos && cid < best)) {
                    bestPos = pos;
                    best = cid;
                    pick = c.value(QStringLiteral("id")).toString();
                }
            }
        }
        m_selectedChannel = pick;
    }
    m_messages.setChannel(m_selectedChannel);
    markRead(m_selectedChannel);
}

void AppController::markConversationRead(const QString& messageId)
{
    if (!m_windowFocused || m_selectedChannel.isEmpty() || messageId.isEmpty())
        return;
    markRead(m_selectedChannel);
    m_link.request(QStringLiteral("message.read"), {{"channel", m_selectedChannel}, {"message", messageId}});
}

void AppController::markRead(const QString& channelId)
{
    m_unreadMessages.remove(channelId);
    const bool had = m_unread.remove(channelId) + m_mentions.remove(channelId) > 0;
    if (had) {
        rebuildServers();
        rebuildChannels();
    }
}

void AppController::rebuildServers()
{
    refreshArtworkCache();
    QList<QVariantMap> rows;
    int homeUnread = 0, homeMentions = 0;
    QHash<QString, int> unreadByServer, mentionsByServer;
    for (auto it = m_unread.cbegin(); it != m_unread.cend(); ++it) {
        if (m_mutedChannels.contains(it.key()))
            continue;
        const QString sid = channel(it.key()).value(QStringLiteral("server_id")).toString();
        if (sid == u"0")
            homeUnread += it.value();
        else
            unreadByServer[sid] += it.value();
    }
    for (auto it = m_mentions.cbegin(); it != m_mentions.cend(); ++it) {
        const QString sid = channel(it.key()).value(QStringLiteral("server_id")).toString();
        if (sid == u"0")
            homeMentions += it.value();
        else
            mentionsByServer[sid] += it.value();
    }
    rows.append(
        {{"key", "home"}, {"itemId", "home"}, {"name", tr("Direct Messages")}, {"initials", "DM"}, {"isHome", true},
            {"unread", homeUnread > 0}, {"mentions", homeMentions}, {"selected", homeSelected()}, {"inVoice", false}});
    QStringList ids = m_serversById.keys();
    std::sort(
        ids.begin(), ids.end(), [](const QString& a, const QString& b) { return a.toULongLong() < b.toULongLong(); });
    const QString voiceServer = channel(voiceChannelId()).value(QStringLiteral("server_id")).toString();
    for (const QString& id : ids) {
        const QJsonObject server = m_serversById.value(id);
        const QString name = server.value(QStringLiteral("name")).toString();
        rows.append({{"key", id}, {"itemId", id}, {"name", name}, {"initials", initials(name)}, {"isHome", false},
            {"description", server.value(QStringLiteral("description")).toString()},
            {"iconAttachmentId", server.value(QStringLiteral("icon_attachment_id")).toString()},
            {"bannerAttachmentId", server.value(QStringLiteral("banner_attachment_id")).toString()},
            {"unread", unreadByServer.value(id) > 0}, {"mentions", mentionsByServer.value(id)},
            {"selected", id == m_selectedServer}, {"inVoice", voiceJoined() && id == voiceServer}});
    }
    m_servers.setRows(std::move(rows));
}

void AppController::rebuildChannels()
{
    refreshArtworkCache();
    QList<QVariantMap> rows;
    auto channelRow = [&](const QJsonObject& c, int depth) {
        const QString id = c.value(QStringLiteral("id")).toString();
        const QString type = c.value(QStringLiteral("type")).toString();
        QVariantMap row{{"key", id}, {"itemId", id}, {"rowType", type},
            {"name", c.value(QStringLiteral("name")).toString()}, {"depth", depth},
            {"unread", m_unread.value(id) > 0 && !m_mutedChannels.contains(id)}, {"mentions", m_mentions.value(id)},
            {"muted", m_mutedChannels.contains(id)}, {"locked", c.value(QStringLiteral("locked")).toBool()},
            {"selected", id == m_selectedChannel}, {"collapsed", m_collapsed.contains(id)}, {"speaking", false},
            {"userMuted", false}, {"userDeafened", false}, {"userId", QString()}, {"presence", QString()},
            {"voiceCount", 0}, {"topic", c.value(QStringLiteral("topic")).toString()}, {"streaming", false}};
        row.insert(QStringLiteral("iconAttachmentId"), c.value(QStringLiteral("icon_attachment_id")).toString());
        if (type == u"dm") {
            for (const auto& r : c.value(QStringLiteral("recipients")).toArray()) {
                if (r.toString() != selfId()) {
                    row.insert(QStringLiteral("userId"), r.toString());
                    row.insert(QStringLiteral("presence"), userStatus(r.toString()));
                    row.insert(QStringLiteral("name"), userName(r.toString()));
                }
            }
        }
        rows.append(row);
        if (type == u"voice") {
            QList<QJsonObject> participants;
            for (const auto& v : m_voiceByUser) {
                if (v.value(QStringLiteral("channel_id")).toString() == id)
                    participants.append(v);
            }
            rows.last().insert(QStringLiteral("voiceCount"), participants.size());
            std::sort(participants.begin(), participants.end(), [this](const QJsonObject& a, const QJsonObject& b) {
                return userName(a.value(QStringLiteral("user_id")).toString())
                           .localeAwareCompare(userName(b.value(QStringLiteral("user_id")).toString()))
                    < 0;
            });
            for (const auto& v : participants) {
                const QString uid = v.value(QStringLiteral("user_id")).toString();
                rows.append(
                    {{"key", id + u':' + uid}, {"itemId", id}, {"rowType", "participant"}, {"name", userName(uid)},
                        {"depth", depth + 1}, {"unread", false}, {"mentions", 0}, {"muted", false}, {"locked", false},
                        {"selected", false}, {"collapsed", false}, {"speaking", m_speaking.contains(uid)},
                        {"userMuted",
                            v.value(QStringLiteral("self_mute")).toBool()
                                || v.value(QStringLiteral("server_mute")).toBool()},
                        {"userDeafened",
                            v.value(QStringLiteral("self_deaf")).toBool()
                                || v.value(QStringLiteral("server_deaf")).toBool()},
                        {"userId", uid}, {"presence", userStatus(uid)}, {"voiceCount", 0}, {"topic", QString()},
                        {"streaming", v.value(QStringLiteral("streaming")).toBool()}});
            }
        }
    };
    auto byPosition = [](const QJsonObject& a, const QJsonObject& b) {
        const int pa = a.value(QStringLiteral("position")).toInt(), pb = b.value(QStringLiteral("position")).toInt();
        return pa != pb ? pa < pb
                        : a.value(QStringLiteral("id")).toString().toULongLong()
                < b.value(QStringLiteral("id")).toString().toULongLong();
    };

    if (homeSelected()) {
        QList<QJsonObject> dms;
        for (const auto& c : m_channelsById) {
            if (c.value(QStringLiteral("server_id")).toString() == u"0")
                dms.append(c);
        }
        std::sort(dms.begin(), dms.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value(QStringLiteral("id")).toString().toULongLong()
                > b.value(QStringLiteral("id")).toString().toULongLong();
        });
        for (const auto& c : dms)
            channelRow(c, 0);
    } else {
        QList<QJsonObject> top, categories;
        QHash<QString, QList<QJsonObject>> children;
        for (const auto& c : m_channelsById) {
            if (c.value(QStringLiteral("server_id")).toString() != m_selectedServer)
                continue;
            const QString parent = c.value(QStringLiteral("parent_id")).toString();
            if (c.value(QStringLiteral("type")).toString() == u"category")
                categories.append(c);
            else if (parent.isEmpty() || parent == u"0")
                top.append(c);
            else
                children[parent].append(c);
        }
        std::sort(top.begin(), top.end(), byPosition);
        std::sort(categories.begin(), categories.end(), byPosition);
        for (const auto& c : top)
            channelRow(c, 0);
        for (const auto& cat : categories) {
            const QString cid = cat.value(QStringLiteral("id")).toString();
            auto kids = children.value(cid);
            std::sort(kids.begin(), kids.end(), byPosition);
            channelRow(cat, 0);
            for (const auto& c : kids) {
                // Collapsed categories still show the selected channel and
                // voice channels that have people in them.
                const bool keep
                    = !m_collapsed.contains(cid) || c.value(QStringLiteral("id")).toString() == m_selectedChannel;
                if (keep)
                    channelRow(c, 1);
            }
        }
    }
    m_channels.setRows(std::move(rows));
}

void AppController::rebuildMembers()
{
    QList<QVariantMap> rows;
    QStringList userIds;
    QString ownerId;
    if (homeSelected()) {
        for (const auto& r : channel(m_selectedChannel).value(QStringLiteral("recipients")).toArray())
            userIds << r.toString();
    } else {
        userIds = m_membersByServer.value(m_selectedServer).keys();
        ownerId = m_serversById.value(m_selectedServer).value(QStringLiteral("owner_id")).toString();
    }
    std::sort(userIds.begin(), userIds.end(), [this](const QString& a, const QString& b) {
        const bool onA = userStatus(a) != u"offline", onB = userStatus(b) != u"offline";
        if (onA != onB)
            return onA;
        return userName(a).localeAwareCompare(userName(b)) < 0;
    });
    int online = 0;
    for (const auto& u : userIds)
        online += userStatus(u) != u"offline" ? 1 : 0;
    for (const QString& uid : userIds) {
        const QString st = userStatus(uid);
        const bool isOnline = st != u"offline";
        rows.append({{"key", uid}, {"userId", uid}, {"name", userName(uid)},
            {"username", m_usersById.value(uid).value(QStringLiteral("username")).toString()}, {"status", st},
            {"avatarUrl", m_usersById.value(uid).value(QStringLiteral("avatar_url")).toString()},
            {"nameColor", userColor(uid)},
            {"section", isOnline ? tr("Online — %1").arg(online) : tr("Offline — %1").arg(userIds.size() - online)},
            {"isOwner", uid == ownerId}, {"inVoice", m_voiceByUser.contains(uid)},
            {"speaking", m_speaking.contains(uid)}});
    }
    m_members.setRows(std::move(rows));
    emit rolesChanged();
}

} // namespace omachat::client
