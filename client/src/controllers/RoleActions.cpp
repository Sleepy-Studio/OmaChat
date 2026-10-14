// Role editing, member role assignment and channel permission overrides.
// The server enforces every rule (rank, "cannot grant what you lack"); the
// UI only hides what would certainly be refused.

#include "controllers/AppController.hpp"

#include <QJsonArray>
#include <QPointer>

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

void AppController::invalidatePermissionPreview()
{
    const QVariantMap context = m_permissionPreview;
    const bool creation = context.value("newChannel").toBool();
    const QString channelId = context.value("channelId").toString();
    const QString parentId = context.value("parentId").toString();
    const bool refresh = state() == u"connected" && accountId() == m_permissionPreviewAccount
        && m_selectedServer == m_permissionPreviewServer && !homeSelected()
        && (creation ? (parentId == u"0" || !channel(parentId).isEmpty()) : !channel(channelId).isEmpty());
    const quint64 serial = ++m_permissionPreviewSerial;
    m_permissionPreview = refresh ? context : QVariantMap{};
    m_permissionPreview.insert("available", false);
    m_permissionPreview.insert("loading", false);
    m_permissionPreview.insert("rows", QVariantList{});
    m_permissionPreview.remove("error");
    emit permissionPreviewChanged();
    if (refresh) {
        QTimer::singleShot(0, this, [this, serial, context] {
            if (serial == m_permissionPreviewSerial)
                requestPermissionPreview(context.value("channelId").toString(), context.value("userId").toString(),
                    context.value("roleId").toString(), context.value("newChannel").toBool(),
                    context.value("channelType").toString(), context.value("parentId").toString());
        });
    }
}

void AppController::loadPermissionPreview(const QString& channelId, const QString& userId, const QString& roleId)
{
    requestPermissionPreview(channelId, userId, roleId, false, {}, {});
}

void AppController::loadCreationPermissionPreview(
    const QString& type, const QString& parentId, const QString& userId, const QString& roleId)
{
    requestPermissionPreview({}, userId, roleId, true, type, type == u"category" ? QStringLiteral("0") : parentId);
}

void AppController::requestPermissionPreview(const QString& channelId, const QString& userId, const QString& roleId,
    bool creation, const QString& type, const QString& requestedParent)
{
    const QString subject = roleId.isEmpty() ? (userId.isEmpty() ? selfId() : userId) : QStringLiteral("0");
    const QString parentId = requestedParent.isEmpty() ? QStringLiteral("0") : requestedParent;
    const QString server = m_selectedServer;
    const QString account = accountId();
    const auto caps = capabilities();
    const bool supported = caps.contains(QStringLiteral("permission_preview"))
        && (!creation || caps.contains(QStringLiteral("permission_preview_creation")))
        && (roleId.isEmpty() || caps.contains(QStringLiteral("permission_preview_roles")));
    m_permissionPreviewAccount = account;
    m_permissionPreviewServer = server;
    const quint64 serial = ++m_permissionPreviewSerial;
    m_permissionPreview = {{"channelId", channelId}, {"userId", subject}, {"roleId", roleId}, {"newChannel", creation},
        {"channelType", type}, {"parentId", parentId}, {"serverId", server}, {"supported", supported},
        {"available", false}, {"loading", false}, {"rows", QVariantList{}}, {"canPreviewMembers", false},
        {"canPreviewRoles", caps.contains(QStringLiteral("permission_preview_roles"))}};
    const auto validContext = [this, creation, channelId, parentId, server, type] {
        if (homeSelected())
            return false;
        if (!creation)
            return !channel(channelId).isEmpty() && channel(channelId).value("server_id").toString() == server;
        if (type != u"text" && type != u"voice" && type != u"category")
            return false;
        return parentId == u"0"
            || (channel(parentId).value("server_id").toString() == server
                && channel(parentId).value("type").toString() == u"category");
    };
    if (!supported || state() != u"connected" || !validContext()) {
        emit permissionPreviewChanged();
        return;
    }
    m_permissionPreview.insert("loading", true);
    emit permissionPreviewChanged();
    QJsonObject params = creation
        ? QJsonObject{{"server", server}, {"parent", parentId}, {"type", type}, {"new_channel", true}}
        : QJsonObject{{"channel", channelId}};
    params.insert(roleId.isEmpty() ? "user" : "role", roleId.isEmpty() ? subject : roleId);
    const QPointer<AppController> guard(this);
    m_link.request(QStringLiteral("permission.preview"), params,
        [guard, serial, account, server, channelId, subject, roleId, creation, type, parentId](
            const ipc::Reply& reply) {
            if (!guard || serial != guard->m_permissionPreviewSerial || guard->state() != u"connected"
                || guard->accountId() != account || guard->m_selectedServer != server || guard->homeSelected()
                || (!creation
                    && (guard->channel(channelId).isEmpty()
                        || guard->channel(channelId).value("server_id").toString() != server))
                || (creation && parentId != u"0"
                    && (guard->channel(parentId).value("server_id").toString() != server
                        || guard->channel(parentId).value("type").toString() != u"category")))
                return;
            guard->m_permissionPreview.insert("loading", false);
            const bool matches = reply.result.value("user_id").toString() == subject
                && (roleId.isEmpty() || reply.result.value("role_id").toString() == roleId)
                && (creation ? reply.result.value("new_channel").toBool()
                            && reply.result.value("server_id").toString() == server
                            && reply.result.value("parent_id").toString() == parentId
                             : reply.result.value("channel_id").toString() == channelId);
            if (!reply.ok || !matches) {
                guard->m_permissionPreview.insert("error",
                    reply.errorMessage.isEmpty() ? guard->tr("Current permission sources are unavailable.")
                                                 : reply.errorMessage);
                emit guard->permissionPreviewChanged();
                return;
            }
            const QHash<QString, QString> sources{{"owner", guard->tr("Server owner: bypasses overrides")},
                {"administrator", guard->tr("Administrator: bypasses overrides")},
                {"default_role", guard->tr("Server default role")}, {"member_roles", guard->tr("Server member roles")},
                {"server_roles_only", guard->tr("Server roles only; channel overrides do not apply")},
                {"category_everyone", guard->tr("Category override for everyone")},
                {"category_roles", guard->tr("Category role overrides")},
                {"category_user", guard->tr("Category member override")},
                {"channel_everyone", guard->tr("Channel override for everyone")},
                {"channel_roles", guard->tr("Channel role overrides")},
                {"channel_user", guard->tr("Channel member override")},
                {"not_granted", guard->tr("No role or override grants this permission")},
                {"view_channel_required", guard->tr("View channels is required")}};
            QHash<QString, QJsonObject> results;
            for (const auto& item : reply.result.value("permissions").toArray()) {
                const QJsonObject row = item.toObject();
                results.insert(row.value("name").toString(), row);
            }
            QVariantList rows = creation ? guard->channelCreationAccessReview(type, parentId).value("rows").toList()
                                         : guard->channelPermissionReview(channelId).value("rows").toList();
            for (QVariant& item : rows) {
                QVariantMap row = item.toMap();
                const QJsonObject result = results.value(row.value("name").toString());
                if (!result.value("allowed").isBool() || !sources.contains(result.value("source").toString())) {
                    guard->m_permissionPreview.insert("error", guard->tr("Incomplete permission sources."));
                    emit guard->permissionPreviewChanged();
                    return;
                }
                row.insert("allowed", result.value("allowed").toBool());
                QString sourceText = sources.value(result.value("source").toString());
                QStringList contributors;
                for (const auto& role : result.value("roles").toArray()) {
                    const QString name = role.toObject().value("name").toString();
                    if (!name.isEmpty())
                        contributors.append(name);
                }
                if (!contributors.isEmpty())
                    sourceText += guard->tr(" — Roles: %1").arg(contributors.join(", "));
                row.insert("source", sourceText);
                row.insert("roles", result.value("roles").toArray().toVariantList());
                item = row;
            }
            guard->m_permissionPreview.insert("rows", rows);
            guard->m_permissionPreview.insert("available", true);
            guard->m_permissionPreview.insert("canPreviewMembers", reply.result.value("can_preview_members").toBool());
            emit guard->permissionPreviewChanged();
        });
}

QVariantMap AppController::channelPermissionReview(const QString& channelId) const
{
    const QJsonObject c = channel(channelId);
    const bool available = state() == u"connected" && !c.isEmpty() && c.value(QStringLiteral("permissions")).isArray();
    QVariantMap review{{"available", available}, {"rows", QVariantList{}},
        {"source",
            available ? tr("Latest server-reported permissions for your account.")
                      : (state() == u"connected" ? tr("Current permissions for this channel are unavailable.")
                                                 : tr("Connect to load current permissions for this channel."))}};
    if (!available)
        return review;

    const QStringList effective = toStringList(c.value(QStringLiteral("permissions")));
    const QString type = c.value(QStringLiteral("type")).toString();
    QVariantList rows;
    for (const QVariant& item : permissionCatalog()) {
        QVariantMap row = item.toMap();
        const QString group = row.value(QStringLiteral("group")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        // Channel review includes only channel-scoped actions. Categories can
        // affect both text and voice children, so retain both groups there.
        if (group == tr("Members") || name == u"CREATE_INVITES" || name == u"MANAGE_SERVER" || name == u"MANAGE_ROLES"
            || name == u"ADMINISTRATOR" || name == u"CREATE_CHANNEL")
            continue;
        if ((type == u"text" && group == tr("Voice")) || (type == u"voice" && group == tr("Text")))
            continue;
        row.insert(QStringLiteral("allowed"), effective.contains(name));
        rows.append(row);
    }
    review.insert(QStringLiteral("rows"), rows);
    const QString parentId = c.value(QStringLiteral("parent_id")).toString();
    const QJsonObject parent = channel(parentId);
    review.insert(QStringLiteral("categoryId"), parentId);
    review.insert(QStringLiteral("category"), parent.value(QStringLiteral("name")).toString());
    const bool hasCategory = !parentId.isEmpty() && parentId != u"0";
    review.insert(QStringLiteral("inheritance"),
        !hasCategory
            ? tr("Server roles → channel overrides. Member overrides take precedence within each layer.")
            : tr("Server roles → category #%1 → channel overrides. Member overrides take precedence within each layer.")
                  .arg(parent.value(QStringLiteral("name")).toString(tr("Unavailable category"))));
    review.insert(QStringLiteral("explanation"),
        tr("Owners and administrators bypass overrides. The server supplies the final result; per-role source details "
           "and previews for other members are unavailable."));
    return review;
}

QVariantMap AppController::channelCreationAccessReview(const QString& type, const QString& parentId) const
{
    const bool nested = type != u"category" && !parentId.isEmpty() && parentId != u"0";
    const QJsonObject server = m_serversById.value(m_selectedServer);
    const QJsonObject parent = nested ? channel(parentId) : QJsonObject{};
    const QJsonValue permissions
        = nested ? parent.value(QStringLiteral("permissions")) : server.value(QStringLiteral("permissions"));
    const bool validType = type == u"text" || type == u"voice" || type == u"category";
    const bool validParent = !nested
        || (parent.value(QStringLiteral("type")).toString() == u"category"
            && parent.value(QStringLiteral("server_id")).toString() == m_selectedServer);
    const bool available
        = state() == u"connected" && !homeSelected() && validType && validParent && permissions.isArray();
    QVariantMap review{{"available", available}, {"rows", QVariantList{}},
        {"source",
            available ? tr("Your inherited access, based on the latest server-reported permissions.")
                      : tr("Connect and select an available category to review inherited access.")},
        {"inheritance",
            nested ? tr("Server roles → category #%1 → new channel with no overrides.")
                         .arg(parent.value(QStringLiteral("name")).toString())
                   : tr("Server roles → new channel with no overrides.")},
        {"explanation",
            tr("Limited older-server estimate from your current account snapshot, not a full creation evaluation. "
               "Other members inherit their own roles and category "
               "overrides. Owners and administrators bypass overrides. No private access rules are added "
               "during creation; permissions can change before the server accepts the request.")}};
    if (!available)
        return review;
    const QStringList effective = toStringList(permissions);
    const bool bypass = isServerOwner() || effective.contains(QStringLiteral("ADMINISTRATOR"));
    const bool canView = bypass || effective.contains(QStringLiteral("VIEW_CHANNEL"));
    QVariantList rows;
    for (const QVariant& item : permissionCatalog()) {
        QVariantMap row = item.toMap();
        const QString group = row.value(QStringLiteral("group")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        if (group == tr("Members") || name == u"CREATE_INVITES" || name == u"MANAGE_SERVER" || name == u"MANAGE_ROLES"
            || name == u"ADMINISTRATOR" || name == u"CREATE_CHANNEL")
            continue;
        if ((type == u"text" && group == tr("Voice")) || (type == u"voice" && group == tr("Text")))
            continue;
        row.insert(QStringLiteral("allowed"), canView && (bypass || effective.contains(name)));
        rows.append(row);
    }
    review.insert(QStringLiteral("rows"), rows);
    return review;
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
    call(
        QStringLiteral("role.update"),
        {{"role", roleId}, {"name", name.trimmed()}, {"color", color.isEmpty() ? QStringLiteral("000000") : color},
            {"permissions", toJsonArray(permissions)},
            {"position", std::max(1, role.value(QStringLiteral("position")).toInt())}},
        [this, roleId](const QJsonObject&) { emit administrationFinished(QStringLiteral("role.update"), roleId, {}); },
        tr("Cannot update role"),
        [this, roleId](
            const QString& error) { emit administrationFinished(QStringLiteral("role.update"), roleId, error); });
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
