// Role editing, member role assignment and channel permission overrides.
// The server enforces every rule (rank, "cannot grant what you lack"); the
// UI only hides what would certainly be refused.

#include "controllers/AppController.hpp"

#include <QJsonArray>

#include <algorithm>
#include <climits>

namespace omachat::client {

namespace {

QStringList toStringList(const QJsonValue& v)
{
    QStringList out;
    for (const auto& item : v.toArray())
        out << item.toString();
    return out;
}

QJsonArray toJsonArray(const QStringList& list)
{
    return QJsonArray::fromStringList(list);
}

} // namespace

QVariantList AppController::permissionCatalog() const
{
    struct Entry {
        const char* name;
        const char* group;
        const char* label;
        const char* description;
    };
    static const Entry kEntries[] = {
        {"VIEW_CHANNEL", "General", QT_TR_NOOP("View channels"), QT_TR_NOOP("See channels and their members")},
        {"CREATE_INVITES", "General", QT_TR_NOOP("Create invites"), QT_TR_NOOP("Invite people to the server")},
        {"SEND_MESSAGES", "Text", QT_TR_NOOP("Send messages"), ""},
        {"READ_HISTORY", "Text", QT_TR_NOOP("Read history"), QT_TR_NOOP("See messages sent before joining")},
        {"ATTACH_FILES", "Text", QT_TR_NOOP("Attach files"), ""},
        {"ADD_REACTIONS", "Text", QT_TR_NOOP("Add reactions"), ""},
        {"MANAGE_MESSAGES", "Text", QT_TR_NOOP("Manage messages"), QT_TR_NOOP("Delete other people's messages")},
        {"CONNECT_VOICE", "Voice", QT_TR_NOOP("Connect"), QT_TR_NOOP("Join voice channels")},
        {"SPEAK", "Voice", QT_TR_NOOP("Speak"), ""},
        {"STREAM", "Voice", QT_TR_NOOP("Share screen"), ""},
        {"PRIORITY_SPEAKER", "Voice", QT_TR_NOOP("Priority speaker"), ""},
        {"MUTE_MEMBERS", "Voice", QT_TR_NOOP("Mute members"), QT_TR_NOOP("Server-mute or deafen others")},
        {"MOVE_MEMBERS", "Voice", QT_TR_NOOP("Move members"), ""},
        {"KICK_MEMBERS", "Members", QT_TR_NOOP("Kick members"), ""},
        {"BAN_MEMBERS", "Members", QT_TR_NOOP("Ban members"), ""},
        {"CREATE_CHANNEL", "Administration", QT_TR_NOOP("Create channels"), ""},
        {"MANAGE_CHANNEL", "Administration", QT_TR_NOOP("Manage channels"), QT_TR_NOOP("Rename, delete, set topics")},
        {"MANAGE_ROLES", "Administration", QT_TR_NOOP("Manage roles"),
            QT_TR_NOOP("Edit roles below your own and channel permissions")},
        {"MANAGE_SERVER", "Administration", QT_TR_NOOP("Manage server"), ""},
        {"ADMINISTRATOR", "Administration", QT_TR_NOOP("Administrator"),
            QT_TR_NOOP("Every permission; channel overrides do not apply")},
    };
    QVariantList out;
    for (const auto& e : kEntries)
        out.append(QVariantMap{{"name", QString::fromLatin1(e.name)}, {"group", tr(e.group)}, {"label", tr(e.label)},
            {"description", tr(e.description)}});
    return out;
}

bool AppController::canManageRoles() const
{
    return !homeSelected() && serverPermission("MANAGE_ROLES");
}

int AppController::selfRank() const
{
    if (homeSelected())
        return 0;
    if (isServerOwner())
        return INT_MAX;
    int rank = 0;
    const QJsonObject me = m_membersByServer.value(m_selectedServer).value(selfId());
    for (const auto& r : me.value(QStringLiteral("roles")).toArray())
        rank = std::max(rank, m_rolesById.value(r.toString()).value(QStringLiteral("position")).toInt());
    return rank;
}

QVariantList AppController::serverRoles() const
{
    if (homeSelected())
        return {};
    std::vector<QJsonObject> roles;
    for (const auto& r : m_rolesById) {
        if (r.value(QStringLiteral("server_id")).toString() == m_selectedServer)
            roles.push_back(r);
    }
    std::ranges::sort(roles, [](const QJsonObject& a, const QJsonObject& b) {
        const int pa = a.value(QStringLiteral("position")).toInt(), pb = b.value(QStringLiteral("position")).toInt();
        return pa != pb ? pa > pb : a.value(QStringLiteral("id")).toString() > b.value(QStringLiteral("id")).toString();
    });
    QHash<QString, int> holders;
    for (const auto& m : m_membersByServer.value(m_selectedServer))
        for (const auto& r : m.value(QStringLiteral("roles")).toArray())
            ++holders[r.toString()];
    const int rank = selfRank();
    const bool manage = canManageRoles();
    QVariantList out;
    for (const auto& r : roles) {
        const QString id = r.value(QStringLiteral("id")).toString();
        const bool isDefault = r.value(QStringLiteral("is_default")).toBool();
        out.append(QVariantMap{{"id", id}, {"name", r.value(QStringLiteral("name")).toString()},
            {"color", r.value(QStringLiteral("color")).toString()},
            {"hasColor", r.value(QStringLiteral("has_color")).toBool()},
            {"position", r.value(QStringLiteral("position")).toInt()}, {"isDefault", isDefault},
            {"permissions", toStringList(r.value(QStringLiteral("permissions")))},
            {"members", isDefault ? m_membersByServer.value(m_selectedServer).size() : holders.value(id)},
            {"editable", manage && r.value(QStringLiteral("position")).toInt() < rank}});
    }
    return out;
}

QVariantList AppController::serverMembers() const
{
    if (homeSelected())
        return {};
    const int rank = selfRank();
    const bool manage = canManageRoles();
    const QString ownerId = m_serversById.value(m_selectedServer).value(QStringLiteral("owner_id")).toString();
    QVariantList out;
    for (const auto& m : m_membersByServer.value(m_selectedServer)) {
        const QString uid = m.value(QStringLiteral("user_id")).toString();
        int theirRank = uid == ownerId ? INT_MAX : 0;
        for (const auto& r : m.value(QStringLiteral("roles")).toArray())
            theirRank = std::max(theirRank, m_rolesById.value(r.toString()).value(QStringLiteral("position")).toInt());
        out.append(QVariantMap{{"userId", uid}, {"name", userName(uid)},
            {"roles", toStringList(m.value(QStringLiteral("roles")))}, {"isOwner", uid == ownerId},
            {"editable", manage && (uid == selfId() || theirRank < rank)}});
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

void AppController::createRole(const QString& name)
{
    if (name.trimmed().isEmpty() || homeSelected())
        return;
    call(QStringLiteral("role.create"),
        {{"server", m_selectedServer}, {"name", name.trimmed()}, {"permissions", QJsonArray()}}, {},
        tr("Cannot create role"));
}

void AppController::updateRole(
    const QString& roleId, const QString& name, const QString& color, const QStringList& permissions)
{
    const QJsonObject role = m_rolesById.value(roleId);
    if (role.isEmpty()) {
        emit administrationFinished(QStringLiteral("role.update"), roleId, tr("This role is no longer available."));
        return;
    }
    call(QStringLiteral("role.update"),
        {{"role", roleId}, {"name", name.trimmed()}, {"color", color.isEmpty() ? QStringLiteral("000000") : color},
            {"permissions", toJsonArray(permissions)},
            {"position", std::max(1, role.value(QStringLiteral("position")).toInt())}},
        [this, roleId](const QJsonObject&) {
            emit administrationFinished(QStringLiteral("role.update"), roleId, {});
        }, tr("Cannot update role"), [this, roleId](const QString& error) {
            emit administrationFinished(QStringLiteral("role.update"), roleId, error);
        });
}

void AppController::moveRole(const QString& roleId, int delta)
{
    // Renumber the non-default roles top to bottom (n … 1) in their new
    // order and send only the positions that actually change.
    QVariantList roles = serverRoles();
    roles.erase(std::remove_if(roles.begin(), roles.end(),
                    [](const QVariant& r) { return r.toMap().value(QStringLiteral("isDefault")).toBool(); }),
        roles.end());
    const auto it = std::ranges::find_if(
        roles, [&](const QVariant& r) { return r.toMap().value(QStringLiteral("id")).toString() == roleId; });
    if (it == roles.end())
        return;
    const auto from = static_cast<int>(it - roles.begin());
    const int to = from - delta; // list is highest first, so "up" is a lower index
    if (to < 0 || to >= roles.size())
        return;
    roles.move(from, to);
    const auto count = static_cast<int>(roles.size());
    for (int i = 0; i < count; ++i) {
        const QVariantMap r = roles.at(i).toMap();
        const int position = count - i;
        if (r.value(QStringLiteral("position")).toInt() == position)
            continue;
        call(QStringLiteral("role.update"),
            {{"role", r.value(QStringLiteral("id")).toString()}, {"name", r.value(QStringLiteral("name")).toString()},
                {"color",
                    r.value(QStringLiteral("hasColor")).toBool() ? r.value(QStringLiteral("color")).toString()
                                                                 : QStringLiteral("000000")},
                {"permissions", toJsonArray(r.value(QStringLiteral("permissions")).toStringList())},
                {"position", position}},
            {}, tr("Cannot reorder roles"));
    }
}

void AppController::deleteRole(const QString& roleId)
{
    call(QStringLiteral("role.delete"), {{"role", roleId}}, {}, tr("Cannot delete role"));
}

void AppController::setMemberRole(const QString& userId, const QString& roleId, bool add)
{
    if (homeSelected())
        return;
    call(QStringLiteral("role.assign"),
        {{"server", m_selectedServer}, {"user", userId}, {"role", roleId}, {"add", add}}, {},
        add ? tr("Cannot assign role") : tr("Cannot remove role"));
}

void AppController::loadOverrides(const QString& channelId)
{
    m_overridesChannel = channelId;
    m_channelOverrides.clear();
    emit overridesChanged();
    call(
        QStringLiteral("override.list"), {{"channel", channelId}},
        [this, channelId](const QJsonObject& r) {
            if (channelId != m_overridesChannel)
                return;
            QVariantList list;
            for (const auto& v : r.value(QStringLiteral("overrides")).toArray()) {
                const QJsonObject o = v.toObject();
                const QString type = o.value(QStringLiteral("target_type")).toString();
                const QString target = o.value(QStringLiteral("target_id")).toString();
                const QString name = type == u"user"
                    ? userName(target)
                    : m_rolesById.value(target).value(QStringLiteral("name")).toString(tr("Unknown role"));
                list.append(QVariantMap{{"targetType", type}, {"targetId", target}, {"name", name},
                    {"allow", toStringList(o.value(QStringLiteral("allow")))},
                    {"deny", toStringList(o.value(QStringLiteral("deny")))}});
            }
            m_channelOverrides = list;
            emit overridesChanged();
        },
        tr("Cannot load channel permissions"));
}

void AppController::setOverride(const QString& channelId, const QString& targetType, const QString& targetId,
    const QStringList& allow, const QStringList& deny)
{
    const bool remove = allow.isEmpty() && deny.isEmpty();
    QJsonObject params{
        {"channel", channelId}, {"allow", toJsonArray(allow)}, {"deny", toJsonArray(deny)}, {"remove", remove}};
    params.insert(targetType == u"user" ? QStringLiteral("user") : QStringLiteral("role"), targetId);
    call(
        QStringLiteral("override.set"), params, [this, channelId](const QJsonObject&) { loadOverrides(channelId); },
        tr("Cannot change channel permissions"));
}

} // namespace omachat::client
