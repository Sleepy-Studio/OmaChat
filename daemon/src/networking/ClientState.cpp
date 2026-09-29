#include "networking/ClientState.hpp"

#include "omachat/core/Permissions.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace omachat::daemon {

QString idString(Id id)
{
    return QString::number(id);
}

Id idFromJson(const QJsonValue& v)
{
    if (v.isString()) {
        bool ok = false;
        const Id id = v.toString().toULongLong(&ok);
        return ok ? id : 0;
    }
    if (v.isDouble() && v.toDouble() > 0)
        return static_cast<Id>(v.toDouble());
    return 0;
}

QString statusName(proto::UserStatus s)
{
    switch (s) {
    case proto::USER_STATUS_ONLINE:
        return QStringLiteral("online");
    case proto::USER_STATUS_IDLE:
        return QStringLiteral("idle");
    case proto::USER_STATUS_DO_NOT_DISTURB:
        return QStringLiteral("dnd");
    default:
        return QStringLiteral("offline");
    }
}

proto::UserStatus statusFromName(const QString& s, bool* ok)
{
    if (ok)
        *ok = true;
    if (s == u"online")
        return proto::USER_STATUS_ONLINE;
    if (s == u"idle")
        return proto::USER_STATUS_IDLE;
    if (s == u"dnd" || s == u"do_not_disturb")
        return proto::USER_STATUS_DO_NOT_DISTURB;
    if (ok)
        *ok = false;
    return proto::USER_STATUS_OFFLINE;
}

QString channelTypeName(proto::ChannelType t)
{
    switch (t) {
    case proto::CHANNEL_TYPE_TEXT:
        return QStringLiteral("text");
    case proto::CHANNEL_TYPE_VOICE:
        return QStringLiteral("voice");
    case proto::CHANNEL_TYPE_CATEGORY:
        return QStringLiteral("category");
    case proto::CHANNEL_TYPE_DM:
        return QStringLiteral("dm");
    case proto::CHANNEL_TYPE_GROUP_DM:
        return QStringLiteral("group_dm");
    default:
        return QStringLiteral("unknown");
    }
}

void ClientState::clear()
{
    Decryptor keep = std::move(m_decryptor); // wiring, not state
    *this = ClientState{};
    m_decryptor = std::move(keep);
}

void ClientState::reset(const proto::SyncState& sync)
{
    clear();
    m_valid = true;
    m_self = sync.self();
    for (const auto& s : sync.servers())
        m_servers[s.id()] = s;
    for (const auto& c : sync.channels())
        m_channels[c.id()] = c;
    for (const auto& r : sync.roles())
        m_roles[r.id()] = r;
    for (const auto& m : sync.members())
        m_members[{m.server_id(), m.user_id()}] = m;
    for (const auto& u : sync.users())
        m_users[u.id()] = u;
    for (const auto& v : sync.voice_states())
        m_voice[v.user_id()] = v;
    for (int i = 0; i < sync.server_permissions_server_ids_size() && i < sync.server_permissions_size(); ++i)
        m_serverPermissions[sync.server_permissions_server_ids(i)] = sync.server_permissions(i);
    m_lastSequence = sync.last_sequence();
}

void ClientState::upsertChannel(const proto::Channel& c)
{
    m_channels[c.id()] = c;
}

const proto::Channel* ClientState::channel(Id id) const
{
    auto it = m_channels.find(id);
    return it == m_channels.end() ? nullptr : &it->second;
}

const proto::Server* ClientState::server(Id id) const
{
    auto it = m_servers.find(id);
    return it == m_servers.end() ? nullptr : &it->second;
}

const proto::User* ClientState::user(Id id) const
{
    auto it = m_users.find(id);
    return it == m_users.end() ? nullptr : &it->second;
}

const proto::Role* ClientState::role(Id id) const
{
    auto it = m_roles.find(id);
    return it == m_roles.end() ? nullptr : &it->second;
}

const proto::VoiceState* ClientState::voiceState(Id userId) const
{
    auto it = m_voice.find(userId);
    return it == m_voice.end() ? nullptr : &it->second;
}

std::vector<Id> ClientState::voiceParticipants(Id channelId) const
{
    std::vector<Id> out;
    for (const auto& [uid, v] : m_voice) {
        if (v.channel_id() == channelId)
            out.push_back(uid);
    }
    return out;
}

std::uint64_t ClientState::serverPermissions(Id serverId) const
{
    auto it = m_serverPermissions.find(serverId);
    return it == m_serverPermissions.end() ? 0 : it->second;
}

Id ClientState::resolveServer(const QString& ref) const
{
    bool numeric = false;
    const Id id = ref.toULongLong(&numeric);
    if (numeric && m_servers.contains(id))
        return id;
    Id found = 0;
    for (const auto& [sid, s] : m_servers) {
        if (QString::fromStdString(s.name()).compare(ref, Qt::CaseInsensitive) == 0) {
            if (found)
                return 0;
            found = sid;
        }
    }
    return found;
}

Id ClientState::resolveChannel(const QString& refIn, ChannelKind kind) const
{
    QString ref = refIn.trimmed();
    bool numeric = false;
    const Id id = ref.toULongLong(&numeric);
    if (numeric && m_channels.contains(id))
        return id;
    Id serverScope = 0;
    const qsizetype slash = ref.lastIndexOf(u'/');
    if (slash > 0) {
        serverScope = resolveServer(ref.left(slash));
        if (!serverScope)
            return 0;
        ref = ref.mid(slash + 1);
    }
    if (ref.startsWith(u'#')) {
        ref.remove(0, 1);
        if (kind == ChannelKind::Any)
            kind = ChannelKind::Messages;
    }
    auto accepts = [kind](const proto::Channel& c) {
        switch (kind) {
        case ChannelKind::Any:
            return c.type() != proto::CHANNEL_TYPE_CATEGORY;
        case ChannelKind::Messages:
            return c.type() == proto::CHANNEL_TYPE_TEXT || c.type() == proto::CHANNEL_TYPE_DM
                || c.type() == proto::CHANNEL_TYPE_GROUP_DM;
        case ChannelKind::Voice:
            return c.type() == proto::CHANNEL_TYPE_VOICE;
        }
        return false;
    };
    for (const auto sensitivity : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
        Id found = 0;
        int matches = 0;
        for (const auto& [cid, c] : m_channels) {
            if (!accepts(c) || (serverScope && c.server_id() != serverScope))
                continue;
            if (QString::fromStdString(c.name()).compare(ref, sensitivity) == 0) {
                found = cid;
                ++matches;
            }
        }
        if (matches == 1)
            return found;
        if (matches > 1)
            return 0; // ambiguous: require Server/name or an id
    }
    return 0;
}

Id ClientState::resolveUser(const QString& refIn) const
{
    QString ref = refIn.trimmed();
    if (ref.startsWith(u'@'))
        ref.remove(0, 1);
    bool numeric = false;
    const Id id = ref.toULongLong(&numeric);
    if (numeric && m_users.contains(id))
        return id;
    for (const auto& [uid, u] : m_users) {
        if (QString::fromStdString(u.username()).compare(ref, Qt::CaseInsensitive) == 0)
            return uid;
    }
    return 0;
}

// ------------------------------------------------------------------ events

void ClientState::apply(const proto::Event& e, std::vector<ModelEvent>& out, bool& needsResync)
{
    if (e.sequence())
        m_lastSequence = std::max(m_lastSequence, e.sequence());
    const Id self = m_self.id();

    switch (e.kind_case()) {
    case proto::Event::kMessageCreate:
        out.push_back({QStringLiteral("message.created"), messageJson(e.message_create())});
        break;
    case proto::Event::kMessageUpdate:
        out.push_back({QStringLiteral("message.updated"), messageJson(e.message_update())});
        break;
    case proto::Event::kMessageDelete:
        out.push_back({QStringLiteral("message.deleted"),
            {{"channel_id", idString(e.message_delete().channel_id())},
                {"message_id", idString(e.message_delete().message_id())}}});
        break;
    case proto::Event::kChannelCreate:
    case proto::Event::kChannelUpdate: {
        const auto& c = e.has_channel_create() ? e.channel_create() : e.channel_update();
        m_channels[c.id()] = c;
        out.push_back({e.has_channel_create() ? QStringLiteral("channel.created") : QStringLiteral("channel.updated"),
            channelJson(c)});
        break;
    }
    case proto::Event::kChannelDelete:
        m_channels.erase(e.channel_delete().channel_id());
        out.push_back({QStringLiteral("channel.deleted"),
            {{"channel_id", idString(e.channel_delete().channel_id())},
                {"server_id", idString(e.channel_delete().server_id())}}});
        break;
    case proto::Event::kMemberJoin:
    case proto::Event::kMemberUpdate: {
        const auto& m = e.has_member_join() ? e.member_join() : e.member_update();
        m_members[{m.server_id(), m.user_id()}] = m;
        out.push_back(
            {e.has_member_join() ? QStringLiteral("member.joined") : QStringLiteral("member.updated"), memberJson(m)});
        break;
    }
    case proto::Event::kMemberLeave: {
        const auto& l = e.member_leave();
        m_members.erase({l.server_id(), l.user_id()});
        if (l.user_id() == self) {
            // We were removed: drop the whole server locally.
            m_servers.erase(l.server_id());
            std::erase_if(m_channels, [&](const auto& kv) { return kv.second.server_id() == l.server_id(); });
            std::erase_if(m_members, [&](const auto& kv) { return kv.first.first == l.server_id(); });
            std::erase_if(m_roles, [&](const auto& kv) { return kv.second.server_id() == l.server_id(); });
            out.push_back({QStringLiteral("server.removed"),
                {{"server_id", idString(l.server_id())}, {"reason", QString::fromStdString(l.reason())}}});
        } else {
            out.push_back({QStringLiteral("member.left"),
                {{"server_id", idString(l.server_id())}, {"user_id", idString(l.user_id())},
                    {"reason", QString::fromStdString(l.reason())}}});
        }
        break;
    }
    case proto::Event::kPresenceUpdate: {
        const auto& p = e.presence_update();
        if (auto it = m_users.find(p.user_id()); it != m_users.end())
            it->second.set_status(p.status());
        if (p.user_id() == self)
            m_self.set_status(p.status());
        out.push_back(
            {QStringLiteral("presence"), {{"user_id", idString(p.user_id())}, {"status", statusName(p.status())}}});
        break;
    }
    case proto::Event::kVoiceStateUpdate: {
        const auto& v = e.voice_state_update();
        const Id previous = m_voice.contains(v.user_id()) ? m_voice[v.user_id()].channel_id() : 0;
        if (v.channel_id() == 0)
            m_voice.erase(v.user_id());
        else
            m_voice[v.user_id()] = v;
        QJsonObject data = voiceStateJson(v);
        data.insert(QStringLiteral("previous_channel_id"), idString(previous));
        out.push_back({QStringLiteral("voice.state"), data});
        break;
    }
    case proto::Event::kReaction: {
        const auto& r = e.reaction();
        out.push_back({QStringLiteral("reaction"),
            {{"channel_id", idString(r.channel_id())}, {"message_id", idString(r.message_id())},
                {"user_id", idString(r.user_id())}, {"emoji", QString::fromStdString(r.emoji())}, {"add", r.add()}}});
        break;
    }
    case proto::Event::kRoleUpdate:
        m_roles[e.role_update().id()] = e.role_update();
        out.push_back({QStringLiteral("role.updated"), roleJson(e.role_update())});
        break;
    case proto::Event::kRoleDelete:
        m_roles.erase(e.role_delete().role_id());
        out.push_back({QStringLiteral("role.deleted"),
            {{"server_id", idString(e.role_delete().server_id())}, {"role_id", idString(e.role_delete().role_id())}}});
        break;
    case proto::Event::kTyping:
        out.push_back({QStringLiteral("typing"),
            {{"channel_id", idString(e.typing().channel_id())}, {"user_id", idString(e.typing().user_id())}}});
        break;
    case proto::Event::kServerCreate:
        // A new server comes with roles, channels and members: resync.
        needsResync = true;
        break;
    case proto::Event::kServerUpdate:
        m_servers[e.server_update().id()] = e.server_update();
        out.push_back({QStringLiteral("server.updated"), serverJson(e.server_update())});
        break;
    case proto::Event::kServerDelete: {
        const Id sid = e.server_delete().server_id();
        m_servers.erase(sid);
        std::erase_if(m_channels, [&](const auto& kv) { return kv.second.server_id() == sid; });
        std::erase_if(m_members, [&](const auto& kv) { return kv.first.first == sid; });
        std::erase_if(m_roles, [&](const auto& kv) { return kv.second.server_id() == sid; });
        out.push_back({QStringLiteral("server.removed"), {{"server_id", idString(sid)}, {"reason", "deleted"}}});
        break;
    }
    case proto::Event::kUserUpdate:
        m_users[e.user_update().id()] = e.user_update();
        if (e.user_update().id() == m_self.id())
            m_self = e.user_update();
        out.push_back({QStringLiteral("user.updated"), userJson(e.user_update())});
        break;
    case proto::Event::kPermissionsChanged:
        needsResync = true;
        break;
    case proto::Event::kDeviceKeysChanged:
        out.push_back(
            {QStringLiteral("e2e.device_keys_changed"), {{"user_id", idString(e.device_keys_changed().user_id())}}});
        break;
    case proto::Event::KIND_NOT_SET:
        break;
    }
}

// -------------------------------------------------------------------- JSON

QJsonObject ClientState::userJson(const proto::User& u) const
{
    return {{"id", idString(u.id())}, {"username", QString::fromStdString(u.username())},
        {"display_name", QString::fromStdString(u.display_name())},
        {"avatar_url", QString::fromStdString(u.avatar_url())}, {"bio", QString::fromStdString(u.bio())},
        {"status", statusName(u.status())}};
}

QJsonObject ClientState::serverJson(const proto::Server& s) const
{
    QJsonArray perms;
    const auto bits = serverPermissions(s.id());
    for (auto n : permissions::names(bits))
        perms.append(QString::fromLatin1(n.data(), static_cast<qsizetype>(n.size())));
    return {{"id", idString(s.id())}, {"name", QString::fromStdString(s.name())}, {"owner_id", idString(s.owner_id())},
        {"is_owner", s.owner_id() == m_self.id()}, {"permissions", perms}};
}

QJsonObject ClientState::channelJson(const proto::Channel& c) const
{
    QJsonArray recipients;
    for (Id r : c.recipient_ids())
        recipients.append(idString(r));
    QString name = QString::fromStdString(c.name());
    if (c.type() == proto::CHANNEL_TYPE_DM) {
        for (Id r : c.recipient_ids()) {
            if (r != m_self.id())
                if (const auto* u = user(r))
                    name = QString::fromStdString(u->display_name());
        }
    } else if (c.type() == proto::CHANNEL_TYPE_GROUP_DM && name.isEmpty()) {
        // Unnamed groups are called after the other people in them.
        QStringList others;
        for (Id r : c.recipient_ids()) {
            if (r == m_self.id())
                continue;
            const auto* u = user(r);
            others << (u ? QString::fromStdString(u->display_name()) : QStringLiteral("?"));
        }
        name = others.join(QStringLiteral(", "));
    }
    const auto p = c.effective_permissions();
    return {{"id", idString(c.id())}, {"server_id", idString(c.server_id())}, {"name", name},
        {"type", channelTypeName(c.type())}, {"parent_id", idString(c.parent_id())},
        {"position", static_cast<int>(c.position())}, {"topic", QString::fromStdString(c.topic())},
        {"recipients", recipients}, {"can_send", permissions::has(p, permissions::SendMessages)},
        {"can_connect", permissions::has(p, permissions::ConnectVoice)},
        {"can_speak", permissions::has(p, permissions::Speak)},
        {"can_stream", permissions::has(p, permissions::Stream)},
        {"can_manage_messages", permissions::has(p, permissions::ManageMessages)},
        {"can_manage", permissions::has(p, permissions::ManageChannel)},
        {"locked",
            c.type() == proto::CHANNEL_TYPE_TEXT        ? !permissions::has(p, permissions::SendMessages)
                : c.type() == proto::CHANNEL_TYPE_VOICE ? !permissions::has(p, permissions::ConnectVoice)
                                                        : false}};
}

QJsonObject ClientState::roleJson(const proto::Role& r) const
{
    QJsonArray perms;
    for (auto n : permissions::names(r.permissions()))
        perms.append(QString::fromLatin1(n.data(), static_cast<qsizetype>(n.size())));
    return {{"id", idString(r.id())}, {"server_id", idString(r.server_id())},
        {"name", QString::fromStdString(r.name())}, {"permissions", perms},
        {"position", static_cast<int>(r.position())},
        {"color", QStringLiteral("#%1").arg(r.color(), 6, 16, QLatin1Char('0'))}, {"has_color", r.color() != 0},
        {"is_default", r.is_default()}};
}

QJsonObject ClientState::memberJson(const proto::Member& m) const
{
    QJsonArray roles;
    for (Id r : m.role_ids())
        roles.append(idString(r));
    return {{"server_id", idString(m.server_id())}, {"user_id", idString(m.user_id())}, {"roles", roles},
        {"joined_at", static_cast<double>(m.joined_at())}};
}

QJsonObject ClientState::voiceStateJson(const proto::VoiceState& v) const
{
    return {{"user_id", idString(v.user_id())}, {"channel_id", idString(v.channel_id())}, {"self_mute", v.self_mute()},
        {"self_deaf", v.self_deaf()}, {"server_mute", v.server_mute()}, {"server_deaf", v.server_deaf()},
        {"stream_id", static_cast<double>(v.stream_id())}, {"streaming", v.streaming()}};
}

QJsonObject ClientState::attachmentJson(const proto::Attachment& a)
{
    return {{"id", idString(a.id())}, {"filename", QString::fromStdString(a.filename())},
        {"mime_type", QString::fromStdString(a.mime_type())}, {"size", static_cast<double>(a.size())},
        {"sha256", QString::fromLatin1(QByteArray::fromStdString(a.sha256()).toHex())}};
}

QJsonObject ClientState::messageJson(const proto::ChatMessage& m) const
{
    QJsonArray mentions;
    bool mentionsMe = false;
    for (Id id : m.mention_ids()) {
        mentions.append(idString(id));
        mentionsMe = mentionsMe || id == m_self.id();
    }
    QJsonArray reactions;
    for (const auto& r : m.reactions())
        reactions.append(QJsonObject{
            {"emoji", QString::fromStdString(r.emoji())}, {"count", static_cast<int>(r.count())}, {"me", r.me()}});
    QJsonArray attachments;
    QString content = QString::fromStdString(m.content());
    QString e2eStatus;
    if (!m.encrypted().empty()) {
        const Decrypted d = m_decryptor ? m_decryptor(m) : Decrypted{{}, {}, QStringLiteral("undecryptable")};
        content = d.content;
        e2eStatus = d.status;
        // The server only knows "file.enc"; the real name, type and size
        // travel inside the encrypted body.
        for (const auto& a : m.attachments()) {
            QJsonObject json = attachmentJson(a);
            const auto f = std::ranges::find_if(d.files, [&](const auto& x) { return x.attachment_id() == a.id(); });
            if (f != d.files.end()) {
                json.insert(QStringLiteral("filename"), QString::fromStdString(f->filename()));
                json.insert(QStringLiteral("mime_type"), QString::fromStdString(f->mime_type()));
                json.insert(QStringLiteral("size"), static_cast<double>(f->size()));
                json.insert(QStringLiteral("encrypted"), true);
            }
            attachments.append(json);
        }
        // Mentions are computed here: the server cannot read the text.
        const QString me = QString::fromStdString(m_self.username());
        mentionsMe = !me.isEmpty()
            && content.contains(
                QRegularExpression(QStringLiteral("(?:^|[^\\w@])@%1\\b").arg(QRegularExpression::escape(me)),
                    QRegularExpression::CaseInsensitiveOption));
    } else {
        for (const auto& a : m.attachments())
            attachments.append(attachmentJson(a));
    }
    QJsonObject out{{"id", idString(m.id())}, {"channel_id", idString(m.channel_id())},
        {"author_id", idString(m.author_id())}, {"timestamp", static_cast<double>(m.timestamp())}, {"content", content},
        {"reply_to", idString(m.reply_to())}, {"edited_at", static_cast<double>(m.edited_at())},
        {"is_action", m.is_action()}, {"mentions", mentions}, {"mentions_me", mentionsMe}, {"reactions", reactions},
        {"attachments", attachments}};
    if (!e2eStatus.isEmpty())
        out.insert(QStringLiteral("e2e"), e2eStatus);
    return out;
}

QJsonObject ClientState::snapshotJson() const
{
    QJsonArray servers, channels, roles, members, users, voice;
    for (const auto& [id, s] : m_servers)
        servers.append(serverJson(s));
    for (const auto& [id, c] : m_channels)
        channels.append(channelJson(c));
    for (const auto& [id, r] : m_roles)
        roles.append(roleJson(r));
    for (const auto& [key, m] : m_members)
        members.append(memberJson(m));
    for (const auto& [id, u] : m_users)
        users.append(userJson(u));
    for (const auto& [id, v] : m_voice)
        voice.append(voiceStateJson(v));
    return {{"valid", m_valid}, {"self", m_valid ? userJson(m_self) : QJsonObject{}}, {"servers", servers},
        {"channels", channels}, {"roles", roles}, {"members", members}, {"users", users}, {"voice_states", voice}};
}

} // namespace omachat::daemon
