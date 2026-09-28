// The local IPC method table. Every method name here is part of the stable
// omachatd contract documented in docs/protocol.md#local-ipc.

#include "application/Daemon.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Permissions.hpp"
#include "omachat/core/Validation.hpp"
#include "omachat/core/Version.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrl>

namespace omachat::daemon {

namespace e = ipc::errors;

namespace {

QJsonObject accountJson(const Account& a)
{
    return {{"id", QString::number(a.id)}, {"host", a.host}, {"port", a.port}, {"username", a.username},
        {"trusted_fingerprint", a.trustedFingerprint}};
}

permissions::Bits permissionBits(const QJsonValue& v, bool* ok)
{
    *ok = true;
    permissions::Bits bits = 0;
    for (const auto& item : v.toArray()) {
        const QByteArray name = item.toString().toUpper().toLatin1();
        const auto bit = permissions::fromName(std::string_view(name.constData(), static_cast<size_t>(name.size())));
        if (!bit)
            *ok = false;
        bits |= bit;
    }
    return bits;
}

QString inviteTokenFrom(QString text)
{
    text = text.trimmed();
    // Accept omachat://invite/TOKEN as well as the bare token.
    const QUrl url(text);
    if (url.scheme() == u"omachat" && url.host() == u"invite")
        return url.path().mid(1);
    return text;
}

} // namespace

bool Daemon::requireConnected(const Responder& r) const
{
    if (m_conn->state() == ServerConnection::State::Connected)
        return true;
    r.error(e::NotConnected, QStringLiteral("not connected (%1)").arg(ServerConnection::stateName(m_conn->state())));
    return false;
}

void Daemon::forward(
    proto::Envelope env, const Responder& r, std::function<QJsonObject(const proto::Envelope&)> transform)
{
    if (!requireConnected(r))
        return;
    m_conn->request(std::move(env), [r, transform = std::move(transform)](const proto::Envelope& reply) {
        if (reply.has_error()) {
            r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
            return;
        }
        r.ok(transform ? transform(reply) : QJsonObject{});
    });
}

Id Daemon::channelParam(const QJsonObject& params, const Responder& r, const char* key, ClientState::ChannelKind kind)
{
    const QJsonValue v = params.value(QLatin1StringView(key));
    const QString ref = v.isString() ? v.toString() : QString::number(static_cast<qint64>(v.toDouble()));
    const Id id = m_conn->model().resolveChannel(ref, kind);
    if (!id)
        r.error(e::NotFound, QStringLiteral("no such channel: %1").arg(ref));
    return id;
}

Id Daemon::serverParam(const QJsonObject& params, const Responder& r, const char* key)
{
    const QJsonValue v = params.value(QLatin1StringView(key));
    const QString ref = v.isString() ? v.toString() : QString::number(static_cast<qint64>(v.toDouble()));
    const Id id = m_conn->model().resolveServer(ref);
    if (!id)
        r.error(e::NotFound, QStringLiteral("no such server: %1").arg(ref));
    return id;
}

Id Daemon::userParam(const QJsonObject& params, const Responder& r, const char* key)
{
    const QJsonValue v = params.value(QLatin1StringView(key));
    const QString ref = v.isString() ? v.toString() : QString::number(static_cast<qint64>(v.toDouble()));
    const Id id = m_conn->model().resolveUser(ref);
    if (!id)
        r.error(e::NotFound, QStringLiteral("no such user: %1").arg(ref));
    return id;
}

void Daemon::uploadAll(Id channelId, QStringList files, std::vector<proto::Attachment> done,
    std::function<void(bool, const QString&, const QString&, const std::vector<proto::Attachment>&)> finish)
{
    if (files.isEmpty()) {
        finish(true, {}, {}, done);
        return;
    }
    const QString next = files.takeFirst();
    m_transfers->upload(channelId, next,
        [this, channelId, files, done, finish = std::move(finish)](const FileTransfers::Result& res) mutable {
            if (!res.ok) {
                // Withdraw what already went up so it does not linger as pending.
                for (const auto& a : done) {
                    proto::Envelope env;
                    env.mutable_cancel_upload()->set_attachment_id(a.id());
                    m_conn->request(std::move(env), [](const proto::Envelope&) {});
                }
                finish(false, res.code, res.message, {});
                return;
            }
            done.push_back(res.attachment);
            uploadAll(channelId, files, std::move(done), std::move(finish));
        });
}

QString Daemon::downloadDestination(Id attachmentId, const QString& filename, const QString& to, QString* error) const
{
    const QString name = validation::filename(filename).value_or(QStringLiteral("attachment-%1").arg(attachmentId));
    auto fail = [error](const QString& why) {
        *error = why;
        return QString();
    };
    if (to == u"cache") {
        // One directory per attachment keeps the original name without collisions.
        const QString dir = paths::cacheDir() + QStringLiteral("/attachments/") + QString::number(attachmentId);
        if (!paths::ensurePrivateDir(paths::cacheDir()) || !QDir().mkpath(dir))
            return fail(QStringLiteral("cannot create %1").arg(dir));
        return QDir(dir).filePath(name);
    }
    QString dir;
    if (to.isEmpty() || to == u"downloads") {
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
    } else {
        const QFileInfo target(to);
        if (!target.isAbsolute())
            return fail(QStringLiteral("destination must be an absolute path"));
        if (!target.isDir())
            return target.absoluteFilePath(); // an explicit file path is used as given
        dir = target.absoluteFilePath();
    }
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("cannot create %1").arg(dir));
    // Never overwrite: "report.pdf" becomes "report (1).pdf".
    const QFileInfo base(QDir(dir).filePath(name));
    QString candidate = base.absoluteFilePath();
    for (int i = 1; QFileInfo::exists(candidate) || QFileInfo::exists(candidate + QStringLiteral(".part")); ++i) {
        const QString suffix = base.completeSuffix();
        candidate = QDir(dir).filePath(suffix.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(base.baseName()).arg(i)
                : QStringLiteral("%1 (%2).%3").arg(base.baseName()).arg(i).arg(suffix));
    }
    return candidate;
}

void Daemon::dispatch(const QString& method, const QJsonObject& params, const Responder& r)
{
    auto it = m_methods.find(method);
    if (it == m_methods.end()) {
        r.error(e::UnknownMethod, QStringLiteral("unknown method '%1'").arg(method));
        return;
    }
    OMA_TRACE("ipc", "request", {"method", method});
    it.value()(params, r);
}

void Daemon::registerMethods()
{
    auto& m = m_methods;
    const ClientState& model = m_conn->model();

    // ------------------------------------------------------------ daemon
    m[QStringLiteral("daemon.status")] = [this](const QJsonObject&, const Responder& r) { r.ok(statusJson()); };
    m[QStringLiteral("daemon.version")] = [](const QJsonObject&, const Responder& r) {
        r.ok({{"version", QString::fromLatin1(kVersion)}, {"ipc", kIpcVersion},
            {"protocol_major", int(kProtocolMajor)}});
    };
    m[QStringLiteral("state.snapshot")] = [this, &model](const QJsonObject&, const Responder& r) {
        QJsonObject snap = model.snapshotJson();
        QJsonArray muted;
        for (auto id : m_mutedChannels)
            muted.append(idString(id));
        snap.insert(QStringLiteral("muted_channels"), muted);
        QJsonObject volumes;
        if (m_conn->hasAccount()) {
            for (const auto& [uid, gain] : m_store.userVolumes(m_conn->account().id))
                volumes.insert(idString(uid), gain);
        }
        snap.insert(QStringLiteral("user_volumes"), volumes);
        snap.insert(QStringLiteral("status"), statusJson());
        r.ok(snap);
    };

    // ----------------------------------------------------------- accounts
    m[QStringLiteral("account.list")] = [this](const QJsonObject&, const Responder& r) {
        QJsonArray list;
        for (const auto& a : m_store.accounts())
            list.append(accountJson(a));
        r.ok(
            {{"accounts", list}, {"active", m_conn->hasAccount() ? QString::number(m_conn->account().id) : QString()}});
    };
    auto ensureAccount = [this](const QJsonObject& p, const Responder& r) -> std::optional<Account> {
        if (p.contains(QStringLiteral("account"))) {
            auto a = m_store.account(p.value(QStringLiteral("account")).toVariant().toLongLong());
            if (!a)
                r.error(e::NotFound, QStringLiteral("no such account"));
            return a;
        }
        const QString host = p.value(QStringLiteral("host")).toString().trimmed();
        const int port = p.value(QStringLiteral("port")).toInt(kDefaultControlPort);
        const auto username = validation::username(p.value(QStringLiteral("username")).toString());
        if (host.isEmpty() || port <= 0 || port > 65535 || !username) {
            r.error(e::BadRequest, QStringLiteral("host, port and a valid username are required"));
            return std::nullopt;
        }
        if (auto existing = m_store.findAccount(host, static_cast<quint16>(port), *username))
            return existing;
        Account a{0, host, static_cast<quint16>(port), *username, {}, 0};
        a.id = m_store.addAccount(a);
        if (!a.id) {
            r.error(e::StorageError, QStringLiteral("could not save account"));
            return std::nullopt;
        }
        return a;
    };
    m[QStringLiteral("account.add")] = [ensureAccount](const QJsonObject& p, const Responder& r) {
        if (auto a = ensureAccount(p, r))
            r.ok(accountJson(*a));
    };
    m[QStringLiteral("account.remove")] = [this](const QJsonObject& p, const Responder& r) {
        const auto id = p.value(QStringLiteral("account")).toVariant().toLongLong();
        if (m_conn->hasAccount() && m_conn->account().id == id) {
            leaveVoice(nullptr);
            m_conn->logout({});
        }
        m_credentials->remove(QStringLiteral("refresh/%1").arg(id), {});
        if (!m_store.removeAccount(id)) {
            r.error(e::NotFound, QStringLiteral("no such account"));
            return;
        }
        r.ok();
    };
    auto authMethod = [this, ensureAccount](bool registering) {
        return [this, ensureAccount, registering](const QJsonObject& p, const Responder& r) {
            const QString password = p.value(QStringLiteral("password")).toString();
            if (password.isEmpty()) {
                r.error(e::BadRequest, QStringLiteral("password is required"));
                return;
            }
            auto account = ensureAccount(p, r);
            if (!account)
                return;
            ServerConnection::Credentials creds;
            creds.kind = registering ? ServerConnection::Credentials::Kind::Register
                                     : ServerConnection::Credentials::Kind::Login;
            creds.username = account->username;
            creds.password = password;
            creds.displayName = p.value(QStringLiteral("display_name")).toString();
            const Account a = *account;
            creds.done = [this, r, a](bool ok, const QString& code, const QString& message) {
                if (ok)
                    r.ok({{"account", accountJson(a)}});
                else
                    r.error(code, message);
            };
            // Same account and already at the login prompt: reuse the link.
            if (m_conn->hasAccount() && m_conn->account().id == a.id
                && m_conn->state() == ServerConnection::State::LoginRequired) {
                m_conn->login(std::move(creds));
            } else {
                startAccount(a, std::move(creds));
            }
        };
    };
    m[QStringLiteral("account.login")] = authMethod(false);
    m[QStringLiteral("account.register")] = authMethod(true);
    m[QStringLiteral("account.logout")] = [this](const QJsonObject&, const Responder& r) {
        leaveVoice(nullptr);
        m_conn->logout([r](bool, const QString&, const QString&) { r.ok(); });
    };
    m[QStringLiteral("connect")] = [this](const QJsonObject& p, const Responder& r) {
        std::optional<Account> a;
        if (p.contains(QStringLiteral("account")))
            a = m_store.account(p.value(QStringLiteral("account")).toVariant().toLongLong());
        else if (m_conn->hasAccount())
            a = m_store.account(m_conn->account().id);
        else if (auto all = m_store.accounts(); !all.empty())
            a = all.front();
        if (!a) {
            r.error(e::NotFound, QStringLiteral("no account configured; use account.login first"));
            return;
        }
        startAccount(*a);
        r.ok({{"account", accountJson(*a)}});
    };
    m[QStringLiteral("disconnect")] = [this](const QJsonObject&, const Responder& r) {
        leaveVoice(nullptr);
        m_conn->stop();
        r.ok();
    };
    m[QStringLiteral("certificate.trust")] = [this](const QJsonObject& p, const Responder& r) {
        const QString fp = p.value(QStringLiteral("fingerprint"))
                               .toString()
                               .trimmed()
                               .toUpper()
                               .replace(QStringLiteral("SHA256:"), QStringLiteral("SHA256:"));
        if (!m_conn->hasAccount()) {
            r.error(e::NotFound, QStringLiteral("no active account"));
            return;
        }
        // Only the fingerprint the user was actually shown can be trusted,
        // so a stale or mistyped value never silently pins something else.
        if (fp.isEmpty() || fp.compare(m_conn->certificateFingerprint(), Qt::CaseInsensitive) != 0) {
            r.error(e::CertificateError,
                QStringLiteral("fingerprint does not match the certificate presented by the server"));
            return;
        }
        Account a = m_conn->account();
        a.trustedFingerprint = m_conn->certificateFingerprint();
        m_store.setTrustedFingerprint(a.id, a.trustedFingerprint);
        OMA_INFO("daemon", "certificate trusted by user", {"host", a.host}, {"fingerprint", a.trustedFingerprint});
        startAccount(a);
        r.ok({{"account", accountJson(a)}});
    };

    // ------------------------------------------------------------ servers
    m[QStringLiteral("server.list")] = [this, &model](const QJsonObject&, const Responder& r) {
        if (!requireConnected(r))
            return;
        QJsonArray list;
        for (const auto& [id, s] : model.servers())
            list.append(model.serverJson(s));
        r.ok({{"servers", list}});
    };
    // Creating or joining a server brings roles, channels and members with
    // it: reply only once the resynchronized model contains all of it.
    auto createOrJoin = [this, &model](proto::Envelope env, const Responder& r) {
        if (!requireConnected(r))
            return;
        m_conn->request(std::move(env), [this, r, &model](const proto::Envelope& reply) {
            if (reply.has_error()) {
                r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            const proto::Server server = reply.server();
            m_conn->resync([r, server, &model](bool) {
                const proto::Server* synced = model.server(server.id());
                r.ok(model.serverJson(synced ? *synced : server));
            });
        });
    };
    m[QStringLiteral("server.create")] = [createOrJoin](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_create_server()->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        createOrJoin(std::move(env), r);
    };
    m[QStringLiteral("server.join")] = [createOrJoin](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_join_invite()->set_token(
            inviteTokenFrom(p.value(QStringLiteral("invite")).toString()).toStdString());
        createOrJoin(std::move(env), r);
    };
    m[QStringLiteral("server.leave")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        env.mutable_leave_server()->set_server_id(sid);
        forward(std::move(env), r);
    };
    m[QStringLiteral("server.delete")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        env.mutable_delete_server()->set_server_id(sid);
        forward(std::move(env), r);
    };
    m[QStringLiteral("invite.create")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        auto* c = env.mutable_create_invite();
        c->set_server_id(sid);
        c->set_max_uses(static_cast<std::uint32_t>(std::max(0, p.value(QStringLiteral("max_uses")).toInt(0))));
        c->set_expires_in_seconds(
            static_cast<std::uint32_t>(std::max(0, p.value(QStringLiteral("expires_in")).toInt(7 * 86400))));
        forward(std::move(env), r, [](const proto::Envelope& reply) {
            const QString token = QString::fromStdString(reply.invite().token());
            return QJsonObject{{"token", token}, {"uri", QStringLiteral("omachat://invite/") + token},
                {"expires_at", static_cast<double>(reply.invite().expires_at())},
                {"max_uses", static_cast<int>(reply.invite().max_uses())}};
        });
    };
    m[QStringLiteral("invite.list")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        env.mutable_list_invites()->set_server_id(sid);
        forward(std::move(env), r, [](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& i : reply.invite_list().invites())
                list.append(QJsonObject{{"token", QString::fromStdString(i.token())},
                    {"uses", static_cast<int>(i.uses())}, {"max_uses", static_cast<int>(i.max_uses())},
                    {"expires_at", static_cast<double>(i.expires_at())}});
            return QJsonObject{{"invites", list}};
        });
    };

    // ----------------------------------------------------------- channels
    m[QStringLiteral("channel.list")] = [this, &model](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        Id sid = 0;
        if (p.contains(QStringLiteral("server")) && !(sid = serverParam(p, r)))
            return;
        QJsonArray list;
        for (const auto& [id, c] : model.channels()) {
            if (sid && c.server_id() != sid)
                continue;
            QJsonObject cj = model.channelJson(c);
            if (c.type() == proto::CHANNEL_TYPE_VOICE) {
                QJsonArray members;
                for (Id uid : model.voiceParticipants(id))
                    members.append(idString(uid));
                cj.insert(QStringLiteral("voice_members"), members);
            }
            cj.insert(QStringLiteral("muted"), m_mutedChannels.contains(id));
            list.append(cj);
        }
        r.ok({{"channels", list}});
    };
    m[QStringLiteral("channel.create")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        const QString type = p.value(QStringLiteral("type")).toString(QStringLiteral("text"));
        proto::Envelope env;
        auto* c = env.mutable_create_channel();
        c->set_server_id(sid);
        c->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        c->set_type(type == u"voice"  ? proto::CHANNEL_TYPE_VOICE
                : type == u"category" ? proto::CHANNEL_TYPE_CATEGORY
                                      : proto::CHANNEL_TYPE_TEXT);
        if (p.contains(QStringLiteral("parent")))
            c->set_parent_id(idFromJson(p.value(QStringLiteral("parent"))));
        forward(std::move(env), r, [this, &model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model.channelJson(reply.channel());
        });
    };
    m[QStringLiteral("channel.update")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        proto::Envelope env;
        auto* u = env.mutable_update_channel();
        u->set_channel_id(cid);
        u->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        if (p.contains(QStringLiteral("topic"))) {
            u->set_set_topic(true);
            u->set_topic(p.value(QStringLiteral("topic")).toString().toStdString());
        }
        forward(
            std::move(env), r, [&model](const proto::Envelope& reply) { return model.channelJson(reply.channel()); });
    };
    m[QStringLiteral("channel.delete")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        proto::Envelope env;
        env.mutable_delete_channel()->set_channel_id(cid);
        forward(std::move(env), r);
    };
    m[QStringLiteral("channel.mute")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid || !m_conn->hasAccount())
            return;
        const bool muted = p.value(QStringLiteral("muted")).toBool(true);
        m_store.setChannelMuted(m_conn->account().id, cid, muted);
        if (muted)
            m_mutedChannels.insert(cid);
        else
            m_mutedChannels.erase(cid);
        m_ipc.broadcast(QStringLiteral("channel.muted"), {{"channel_id", idString(cid)}, {"muted", muted}});
        r.ok({{"channel_id", idString(cid)}, {"muted", muted}});
    };
    m[QStringLiteral("dm.open")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        proto::Envelope env;
        env.mutable_open_dm()->set_user_id(uid);
        forward(std::move(env), r, [this, &model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model.channelJson(reply.channel());
        });
    };

    // ----------------------------------------------------------- messages
    m[QStringLiteral("message.history")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        proto::Envelope env;
        auto* g = env.mutable_get_messages();
        g->set_channel_id(cid);
        g->set_before_message_id(idFromJson(p.value(QStringLiteral("before"))));
        g->set_limit(static_cast<std::uint32_t>(std::clamp(p.value(QStringLiteral("limit")).toInt(50), 1, 100)));
        forward(std::move(env), r, [&model](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& msg : reply.message_page().messages())
                list.append(model.messageJson(msg));
            return QJsonObject{{"channel_id", idString(reply.message_page().channel_id())}, {"messages", list},
                {"has_more", reply.message_page().has_more()}};
        });
    };
    m[QStringLiteral("message.send")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        QStringList files;
        for (const auto& f : p.value(QStringLiteral("files")).toArray())
            files << f.toString();
        if (files.size() > 10) {
            r.error(e::BadRequest, QStringLiteral("at most 10 files per message"));
            return;
        }
        auto send = [this, &model, cid, p, r](const std::vector<proto::Attachment>& attachments) {
            proto::Envelope env;
            auto* s = env.mutable_send_message();
            s->set_channel_id(cid);
            s->set_content(p.value(QStringLiteral("content")).toString().toStdString());
            s->set_reply_to(idFromJson(p.value(QStringLiteral("reply_to"))));
            s->set_is_action(p.value(QStringLiteral("action")).toBool(false));
            for (const auto& a : attachments)
                s->add_attachment_ids(a.id());
            forward(std::move(env), r,
                [&model](const proto::Envelope& reply) { return model.messageJson(reply.chat_message()); });
        };
        if (files.isEmpty()) {
            send({});
            return;
        }
        if (!requireConnected(r))
            return;
        uploadAll(cid, files, {},
            [r, send](bool ok, const QString& code, const QString& message,
                const std::vector<proto::Attachment>& attachments) {
                if (!ok) {
                    r.error(code, message);
                    return;
                }
                send(attachments);
            });
    };
    m[QStringLiteral("message.edit")] = [this, &model](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        auto* ed = env.mutable_edit_message();
        ed->set_message_id(idFromJson(p.value(QStringLiteral("message"))));
        ed->set_content(p.value(QStringLiteral("content")).toString().toStdString());
        forward(std::move(env), r,
            [&model](const proto::Envelope& reply) { return model.messageJson(reply.chat_message()); });
    };
    m[QStringLiteral("message.delete")] = [this](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_delete_message()->set_message_id(idFromJson(p.value(QStringLiteral("message"))));
        forward(std::move(env), r);
    };
    m[QStringLiteral("message.search")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        proto::Envelope env;
        auto* s = env.mutable_search_messages();
        s->set_channel_id(cid);
        s->set_query(p.value(QStringLiteral("query")).toString().toStdString());
        s->set_limit(static_cast<std::uint32_t>(std::clamp(p.value(QStringLiteral("limit")).toInt(25), 1, 50)));
        forward(std::move(env), r, [&model](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& msg : reply.message_page().messages())
                list.append(model.messageJson(msg));
            return QJsonObject{{"messages", list}};
        });
    };
    m[QStringLiteral("message.react")] = [this](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        auto* re = env.mutable_reaction();
        re->set_message_id(idFromJson(p.value(QStringLiteral("message"))));
        re->set_emoji(p.value(QStringLiteral("emoji")).toString().toStdString());
        re->set_add(p.value(QStringLiteral("add")).toBool(true));
        forward(std::move(env), r);
    };
    // -------------------------------------------------------- attachments
    m[QStringLiteral("attachment.download")] = [this](const QJsonObject& p, const Responder& r) {
        const Id aid = idFromJson(p.value(QStringLiteral("attachment")));
        if (!aid) {
            r.error(e::BadRequest, QStringLiteral("attachment id required"));
            return;
        }
        QString error;
        const QString dest = downloadDestination(aid, p.value(QStringLiteral("filename")).toString(),
            p.value(QStringLiteral("to")).toString(QStringLiteral("downloads")), &error);
        if (dest.isEmpty()) {
            r.error(e::StorageError, error);
            return;
        }
        // Cached copies are reused; attachments never change once sent.
        const auto size = p.value(QStringLiteral("size")).toDouble(-1);
        if (p.value(QStringLiteral("to")).toString() == u"cache" && size > 0 && QFileInfo(dest).size() == qint64(size)) {
            r.ok({{"path", dest}, {"cached", true}});
            return;
        }
        if (!requireConnected(r))
            return;
        m_transfers->download(aid, dest, [r](const FileTransfers::Result& res) {
            if (res.ok)
                r.ok({{"path", res.path}, {"cached", false}});
            else
                r.error(res.code, res.message);
        });
    };
    m[QStringLiteral("transfer.list")] = [this](const QJsonObject&, const Responder& r) {
        r.ok({{"transfers", m_transfers->activeJson()}});
    };
    m[QStringLiteral("transfer.cancel")] = [this](const QJsonObject& p, const Responder& r) {
        if (m_transfers->cancel(p.value(QStringLiteral("id")).toString().toULongLong()))
            r.ok();
        else
            r.error(e::NotFound, QStringLiteral("no such transfer"));
    };

    m[QStringLiteral("typing")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        proto::Envelope env;
        env.mutable_typing()->set_channel_id(cid);
        forward(std::move(env), r);
    };
    m[QStringLiteral("presence.set")] = [this](const QJsonObject& p, const Responder& r) {
        bool ok = false;
        const auto status = statusFromName(p.value(QStringLiteral("status")).toString(), &ok);
        if (!ok || status == proto::USER_STATUS_OFFLINE) {
            r.error(e::BadRequest, QStringLiteral("status must be online, idle or dnd"));
            return;
        }
        proto::Envelope env;
        env.mutable_set_presence()->set_status(status);
        forward(std::move(env), r);
    };
    m[QStringLiteral("member.list")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        const QJsonObject snap = model.snapshotJson();
        QJsonArray members;
        for (const auto& v : snap.value(QStringLiteral("members")).toArray()) {
            const QJsonObject mem = v.toObject();
            if (idFromJson(mem.value(QStringLiteral("server_id"))) != sid)
                continue;
            QJsonObject entry = mem;
            if (const auto* u = model.user(idFromJson(mem.value(QStringLiteral("user_id")))))
                entry.insert(QStringLiteral("user"), model.userJson(*u));
            members.append(entry);
        }
        r.ok({{"members", members}});
    };

    // -------------------------------------------------------------- voice
    m[QStringLiteral("voice.join")] = [this, &model](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Voice);
        if (!cid)
            return;
        const auto* c = model.channel(cid);
        if (!c || c->type() != proto::CHANNEL_TYPE_VOICE) {
            r.error(e::BadRequest, QStringLiteral("not a voice channel"));
            return;
        }
        joinVoice(cid, &r);
    };
    m[QStringLiteral("voice.leave")] = [this](const QJsonObject&, const Responder& r) { leaveVoice(&r); };
    auto voiceSet = [this](std::function<std::pair<bool, bool>()> next) {
        return [this, next](const QJsonObject&, const Responder& r) {
            const auto [mute, deaf] = next();
            setSelfVoiceState(mute, deaf);
            r.ok(statusJson().value(QStringLiteral("voice")).toObject());
        };
    };
    m[QStringLiteral("voice.mute")] = voiceSet([this] { return std::pair{true, m_selfDeaf}; });
    // Unmuting while deafened also undeafens, matching common client behavior.
    m[QStringLiteral("voice.unmute")] = voiceSet([] { return std::pair{false, false}; });
    m[QStringLiteral("voice.toggle_mute")]
        = voiceSet([this] { return m_selfMute || m_selfDeaf ? std::pair{false, false} : std::pair{true, false}; });
    m[QStringLiteral("voice.deafen")] = voiceSet([this] { return std::pair{m_selfMute, true}; });
    m[QStringLiteral("voice.undeafen")] = voiceSet([this] { return std::pair{m_selfMute, false}; });
    m[QStringLiteral("voice.toggle_deafen")] = voiceSet([this] { return std::pair{m_selfMute, !m_selfDeaf}; });
    m[QStringLiteral("voice.stats")] = [this](const QJsonObject&, const Responder& r) { r.ok(m_voice->statsJson()); };
    m[QStringLiteral("voice.mode")] = [this](const QJsonObject& p, const Responder& r) {
        const QString mode = p.value(QStringLiteral("mode")).toString();
        if (mode != u"vad" && mode != u"ptt" && mode != u"always") {
            r.error(e::BadRequest, QStringLiteral("mode must be vad, ptt or always"));
            return;
        }
        m_config.audio.mode = config::inputModeFromString(mode);
        QString err;
        saveConfig(&err);
        applyConfig();
        r.ok({{"mode", mode}});
    };
    m[QStringLiteral("ptt.begin")] = [this](const QJsonObject&, const Responder& r) {
        m_pttOwner = r.socket();
        m_voice->setPushToTalk(true);
        m_ipc.broadcast(QStringLiteral("voice.ptt"), {{"active", true}});
        scheduleStatus();
        r.ok({{"ptt", true}, {"mode", config::toString(m_config.audio.mode)}});
    };
    m[QStringLiteral("ptt.end")] = [this](const QJsonObject&, const Responder& r) {
        m_pttOwner = nullptr;
        m_voice->setPushToTalk(false);
        m_ipc.broadcast(QStringLiteral("voice.ptt"), {{"active", false}});
        scheduleStatus();
        r.ok({{"ptt", false}});
    };

    // -------------------------------------------------------------- audio
    m[QStringLiteral("audio.devices")] = [this](const QJsonObject&, const Responder& r) {
        QJsonArray inputs, outputs;
        for (const auto& d : m_audio->devices())
            (d.input ? inputs : outputs).append(QJsonObject{{"id", d.id}, {"name", d.description}});
        r.ok({{"backend", m_audio->name()}, {"inputs", inputs}, {"outputs", outputs}});
    };
    m[QStringLiteral("audio.settings")] = [this](const QJsonObject&, const Responder& r) {
        const auto& a = m_config.audio;
        r.ok({{"input", a.input}, {"output", a.output}, {"mode", config::toString(a.mode)},
            {"noise_suppression", a.noiseSuppression},
            {"noise_suppression_available", voice::AudioProcessor::noiseSuppressionAvailable()},
            {"echo_cancellation", a.echoCancellation}, {"high_pass", a.highPass}, {"automatic_gain", a.automaticGain},
            {"vad_threshold_db", a.vadThresholdDb}, {"bitrate", a.bitrate}, {"input_volume", a.inputVolume},
            {"output_volume", a.outputVolume}, {"input_level_db", static_cast<double>(m_voice->inputLevelDb())}});
    };
    m[QStringLiteral("audio.set")] = [this](const QJsonObject& p, const Responder& r) {
        auto& a = m_config.audio;
        if (p.contains(QStringLiteral("input")))
            a.input = p.value(QStringLiteral("input")).toString(QStringLiteral("default"));
        if (p.contains(QStringLiteral("output")))
            a.output = p.value(QStringLiteral("output")).toString(QStringLiteral("default"));
        if (p.contains(QStringLiteral("mode")))
            a.mode = config::inputModeFromString(p.value(QStringLiteral("mode")).toString(), a.mode);
        if (p.contains(QStringLiteral("noise_suppression")))
            a.noiseSuppression = p.value(QStringLiteral("noise_suppression")).toBool();
        if (p.contains(QStringLiteral("high_pass")))
            a.highPass = p.value(QStringLiteral("high_pass")).toBool();
        if (p.contains(QStringLiteral("automatic_gain")))
            a.automaticGain = p.value(QStringLiteral("automatic_gain")).toBool();
        if (p.contains(QStringLiteral("vad_threshold_db")))
            a.vadThresholdDb = std::clamp(p.value(QStringLiteral("vad_threshold_db")).toDouble(), -90.0, 0.0);
        if (p.contains(QStringLiteral("bitrate")))
            a.bitrate = std::clamp(p.value(QStringLiteral("bitrate")).toInt(), 24000, 96000);
        if (p.contains(QStringLiteral("input_volume")))
            a.inputVolume = std::clamp(p.value(QStringLiteral("input_volume")).toDouble(), 0.0, 2.0);
        if (p.contains(QStringLiteral("output_volume")))
            a.outputVolume = std::clamp(p.value(QStringLiteral("output_volume")).toDouble(), 0.0, 2.0);
        QString err;
        if (!saveConfig(&err))
            OMA_WARN("daemon", "could not save config", {"error", err});
        applyConfig();
        dispatch(QStringLiteral("audio.settings"), {}, r);
    };
    m[QStringLiteral("audio.user_volume")] = [this](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        const double gain = std::clamp(p.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 2.0);
        m_voice->setUserGain(uid, gain);
        if (m_conn->hasAccount())
            m_store.setUserVolume(m_conn->account().id, uid, gain);
        r.ok({{"user_id", idString(uid)}, {"volume", gain}});
    };

    // --------------------------------------------------------- moderation
    m[QStringLiteral("moderation.kick")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        const Id uid = sid ? userParam(p, r) : 0;
        if (!uid)
            return;
        proto::Envelope env;
        auto* k = env.mutable_kick();
        k->set_server_id(sid);
        k->set_user_id(uid);
        k->set_reason(p.value(QStringLiteral("reason")).toString().toStdString());
        forward(std::move(env), r);
    };
    m[QStringLiteral("moderation.ban")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        const Id uid = sid ? userParam(p, r) : 0;
        if (!uid)
            return;
        proto::Envelope env;
        auto* b = env.mutable_ban();
        b->set_server_id(sid);
        b->set_user_id(uid);
        b->set_reason(p.value(QStringLiteral("reason")).toString().toStdString());
        forward(std::move(env), r);
    };
    m[QStringLiteral("moderation.unban")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        auto* u = env.mutable_unban();
        u->set_server_id(sid);
        u->set_user_id(idFromJson(p.value(QStringLiteral("user"))));
        forward(std::move(env), r);
    };
    m[QStringLiteral("moderation.voice_mute")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        const Id uid = sid ? userParam(p, r) : 0;
        if (!uid)
            return;
        proto::Envelope env;
        auto* s = env.mutable_server_mute();
        s->set_server_id(sid);
        s->set_user_id(uid);
        s->set_mute(p.value(QStringLiteral("mute")).toBool(true));
        s->set_deaf(p.value(QStringLiteral("deaf")).toBool(false));
        forward(std::move(env), r);
    };
    m[QStringLiteral("role.create")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        bool ok = false;
        const auto bits = permissionBits(p.value(QStringLiteral("permissions")), &ok);
        if (!ok) {
            r.error(e::BadRequest, QStringLiteral("unknown permission name"));
            return;
        }
        proto::Envelope env;
        auto* c = env.mutable_create_role();
        c->set_server_id(sid);
        c->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        c->set_permissions(bits);
        forward(std::move(env), r, [&model](const proto::Envelope& reply) { return model.roleJson(reply.role()); });
    };
    m[QStringLiteral("role.update")] = [this, &model](const QJsonObject& p, const Responder& r) {
        bool ok = false;
        const auto bits = permissionBits(p.value(QStringLiteral("permissions")), &ok);
        if (!ok) {
            r.error(e::BadRequest, QStringLiteral("unknown permission name"));
            return;
        }
        proto::Envelope env;
        auto* u = env.mutable_update_role();
        u->set_role_id(idFromJson(p.value(QStringLiteral("role"))));
        u->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        u->set_permissions(bits);
        u->set_position(static_cast<std::uint32_t>(std::max(1, p.value(QStringLiteral("position")).toInt(1))));
        u->set_color(p.value(QStringLiteral("color")).toString().remove(u'#').toUInt(nullptr, 16));
        forward(std::move(env), r, [&model](const proto::Envelope& reply) { return model.roleJson(reply.role()); });
    };
    m[QStringLiteral("role.delete")] = [this](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_delete_role()->set_role_id(idFromJson(p.value(QStringLiteral("role"))));
        forward(std::move(env), r);
    };
    m[QStringLiteral("role.assign")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        const Id uid = sid ? userParam(p, r) : 0;
        if (!uid)
            return;
        proto::Envelope env;
        auto* a = env.mutable_assign_role();
        a->set_server_id(sid);
        a->set_user_id(uid);
        a->set_role_id(idFromJson(p.value(QStringLiteral("role"))));
        a->set_add(p.value(QStringLiteral("add")).toBool(true));
        forward(std::move(env), r);
    };
    m[QStringLiteral("override.set")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        bool okAllow = false, okDeny = false;
        const auto allow = permissionBits(p.value(QStringLiteral("allow")), &okAllow);
        const auto deny = permissionBits(p.value(QStringLiteral("deny")), &okDeny);
        if (!okAllow || !okDeny) {
            r.error(e::BadRequest, QStringLiteral("unknown permission name"));
            return;
        }
        proto::Envelope env;
        auto* s = env.mutable_set_override();
        auto* o = s->mutable_override();
        o->set_channel_id(cid);
        if (p.contains(QStringLiteral("user"))) {
            o->set_target_type(proto::PermissionOverride::TARGET_USER);
            o->set_target_id(idFromJson(p.value(QStringLiteral("user"))));
        } else {
            o->set_target_type(proto::PermissionOverride::TARGET_ROLE);
            o->set_target_id(idFromJson(p.value(QStringLiteral("role"))));
        }
        o->set_allow(allow);
        o->set_deny(deny);
        s->set_remove(p.value(QStringLiteral("remove")).toBool(false));
        forward(std::move(env), r);
    };
    m[QStringLiteral("override.list")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        proto::Envelope env;
        env.mutable_list_overrides()->set_channel_id(cid);
        forward(std::move(env), r, [](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& o : reply.override_list().overrides()) {
                QJsonArray allow, deny;
                for (auto n : permissions::names(o.allow()))
                    allow.append(QString::fromLatin1(n.data(), static_cast<qsizetype>(n.size())));
                for (auto n : permissions::names(o.deny()))
                    deny.append(QString::fromLatin1(n.data(), static_cast<qsizetype>(n.size())));
                list.append(QJsonObject{
                    {"target_type", o.target_type() == proto::PermissionOverride::TARGET_USER ? "user" : "role"},
                    {"target_id", idString(o.target_id())}, {"allow", allow}, {"deny", deny}});
            }
            return QJsonObject{{"overrides", list}};
        });
    };

    // ----------------------------------------------------------- UI hints
    m[QStringLiteral("ui.focus")] = [this](const QJsonObject& p, const Responder& r) {
        m_focusedChannel = idFromJson(p.value(QStringLiteral("channel")));
        const Id server = idFromJson(p.value(QStringLiteral("server")));
        if (server != m_focusedServer) {
            m_focusedServer = server;
            scheduleStatus();
        }
        m_windowFocused = p.value(QStringLiteral("focused")).toBool(false);
        r.ok();
    };

    m[QStringLiteral("channel.join")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        const auto* c = model.channel(cid);
        if (c && c->type() == proto::CHANNEL_TYPE_VOICE) {
            if (requireConnected(r))
                joinVoice(cid, &r);
            return;
        }
        m_ipc.broadcast(QStringLiteral("ui.navigate"),
            {{"channel_id", idString(cid)}, {"server_id", idString(c ? c->server_id() : 0)}});
        r.ok({{"channel_id", idString(cid)}, {"focused", true}});
    };
    m[QStringLiteral("ui.navigate")] = [this, &model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        const auto* c = model.channel(cid);
        m_ipc.broadcast(QStringLiteral("ui.navigate"),
            {{"channel_id", idString(cid)}, {"server_id", idString(c ? c->server_id() : 0)}});
        r.ok({{"channel_id", idString(cid)}});
    };

    // ------------------------------------------------------------- config
    m[QStringLiteral("config.get")] = [this](const QJsonObject&, const Responder& r) {
        QJsonObject shortcuts;
        for (auto it = m_config.shortcuts.cbegin(); it != m_config.shortcuts.cend(); ++it)
            shortcuts.insert(it.key(), it.value());
        r.ok({{"path", m_options.configPath},
            {"notifications",
                QJsonObject{{"messages", m_config.notifications.messages},
                    {"mentions", m_config.notifications.mentions}, {"voice_join", m_config.notifications.voiceJoin}}},
            {"ui",
                QJsonObject{{"compact_mode", m_config.ui.compactMode}, {"scale", m_config.ui.scale},
                    {"reduced_motion", m_config.ui.reducedMotion}, {"theme", m_config.ui.theme}}},
            {"shortcuts", shortcuts}});
    };
    m[QStringLiteral("config.set_notifications")] = [this](const QJsonObject& p, const Responder& r) {
        auto& n = m_config.notifications;
        n.messages = p.value(QStringLiteral("messages")).toBool(n.messages);
        n.mentions = p.value(QStringLiteral("mentions")).toBool(n.mentions);
        n.voiceJoin = p.value(QStringLiteral("voice_join")).toBool(n.voiceJoin);
        QString err;
        if (!saveConfig(&err)) {
            r.error(e::StorageError, err);
            return;
        }
        r.ok();
    };
    m[QStringLiteral("config.reload")] = [this](const QJsonObject&, const Responder& r) {
        QString err;
        m_config = config::ClientConfig::load(m_options.configPath, &err);
        applyConfig();
        if (!err.isEmpty()) {
            r.error(e::BadRequest, err);
            return;
        }
        r.ok();
    };
}

} // namespace omachat::daemon
