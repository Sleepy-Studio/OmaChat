// The local IPC method table. Every method name here is part of the stable
// omachatd contract documented in docs/protocol.md#local-ipc.

#include "application/Daemon.hpp"
#include "networking/OAuthLoginFlow.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Permissions.hpp"
#include "omachat/core/Validation.hpp"
#include "omachat/core/Version.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>

namespace omachat::daemon {

namespace e = ipc::errors;

namespace {

// Resolves to the active link's model at call time.
struct ActiveModel {
    ServerConnection* const* conn;
    const ClientState* operator->() const { return &(*conn)->model(); }
};

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

QString discordStringId(const QJsonValue& value)
{
    const QString id = value.toString();
    if (id.isEmpty() || id.size() > 20 || id == u"0")
        return {};
    for (QChar ch : id)
        if (!ch.isDigit())
            return {};
    return id;
}

// Only files included beside the selected export can be read. Remote URLs
// stay as links in the message, never as untrusted download instructions.
QString discordLocalAsset(const QFileInfo& exportFile, const QString& url)
{
    const QUrl parsed(url);
    if (!parsed.isRelative() || url.startsWith(u'/') || url.startsWith(u'\\'))
        return {};
    const QString root = exportFile.absoluteDir().canonicalPath();
    const QFileInfo asset(exportFile.absoluteDir().filePath(QUrl::fromPercentEncoding(parsed.path().toUtf8())));
    const QString path = asset.canonicalFilePath();
    if (path.isEmpty() || !asset.isFile() || !path.startsWith(root + u'/'))
        return {};
    return path;
}

struct DiscordImportPlan {
    QString guildId;
    std::vector<proto::ImportDiscordBatchRequest> batches;
    int messages = 0;
};

std::optional<DiscordImportPlan> prepareDiscordImport(const QJsonArray& paths, QString* error)
{
    DiscordImportPlan plan;
    if (paths.isEmpty() || paths.size() > 100) {
        *error = QStringLiteral("choose 1–100 Discord JSON export files");
        return std::nullopt;
    }
    QMimeDatabase mime;
    qint64 totalBytes = 0;
    for (const auto& value : paths) {
        const QString path = QUrl(value.toString()).toLocalFile();
        const QFileInfo info(path);
        if (!info.isFile() || info.size() > 128 * 1024 * 1024 || info.size() < 2) {
            *error = QStringLiteral("could not read export file: %1").arg(value.toString());
            return std::nullopt;
        }
        totalBytes += info.size();
        if (totalBytes > 256 * 1024 * 1024) {
            *error = QStringLiteral("select fewer export files at once (256 MB limit)");
            return std::nullopt;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("could not open export file: %1").arg(path);
            return std::nullopt;
        }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        const QJsonObject data = doc.object();
        const QJsonObject channel = data.value(QStringLiteral("channel")).toObject();
        const QString guild = discordStringId(data.value(QStringLiteral("guild")).toObject().value(QStringLiteral("id")));
        const QString channelId = discordStringId(channel.value(QStringLiteral("id")));
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() || !data.value(QStringLiteral("messages")).isArray()
            || guild.isEmpty() || channelId.isEmpty() || (!plan.guildId.isEmpty() && plan.guildId != guild)) {
            *error = QStringLiteral("invalid or mixed-guild Discord JSON exports: %1").arg(path);
            return std::nullopt;
        }
        plan.guildId = guild;
        const auto startBatch = [&] {
            proto::ImportDiscordBatchRequest batch;
            batch.set_guild_id(guild.toStdString());
            batch.set_channel_id(channelId.toStdString());
            batch.set_channel_name(channel.value(QStringLiteral("name")).toString().toStdString());
            batch.set_channel_topic(channel.value(QStringLiteral("topic")).toString().left(512).toStdString());
            batch.set_category_id(discordStringId(channel.value(QStringLiteral("categoryId"))).toStdString());
            batch.set_category_name(channel.value(QStringLiteral("category")).toString().toStdString());
            return batch;
        };
        auto batch = startBatch();
        for (const auto& item : data.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = item.toObject();
            const QJsonObject author = message.value(QStringLiteral("author")).toObject();
            const QString messageId = discordStringId(message.value(QStringLiteral("id")));
            const QString authorId = discordStringId(author.value(QStringLiteral("id")));
            const QDateTime date = QDateTime::fromString(message.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
            if (messageId.isEmpty() || authorId.isEmpty() || !date.isValid()) {
                *error = QStringLiteral("message has an invalid ID or timestamp in %1").arg(path);
                return std::nullopt;
            }
            auto* imported = batch.add_messages();
            imported->set_discord_id(messageId.toStdString());
            imported->set_author_id(authorId.toStdString());
            imported->set_author_name(author.value(QStringLiteral("nickname")).toString(
                author.value(QStringLiteral("name")).toString()).toStdString());
            imported->set_timestamp(date.toMSecsSinceEpoch());
            const QDateTime edited = QDateTime::fromString(message.value(QStringLiteral("timestampEdited")).toString(), Qt::ISODateWithMs);
            if (edited.isValid())
                imported->set_edited_at(edited.toMSecsSinceEpoch());
            const QString reply = discordStringId(message.value(QStringLiteral("reference")).toObject().value(QStringLiteral("messageId")));
            imported->set_reply_discord_id(reply.toStdString());
            QString content = message.value(QStringLiteral("content")).toString();
            for (const auto& attachmentValue : message.value(QStringLiteral("attachments")).toArray()) {
                const QJsonObject attachment = attachmentValue.toObject();
                const QString url = attachment.value(QStringLiteral("url")).toString();
                const QString local = discordLocalAsset(info, url);
                const QFileInfo assetInfo(local);
                if (!url.isEmpty() && QUrl(url).isRelative()
                    && (local.isEmpty() || assetInfo.size() > 4 * 1024 * 1024 || imported->assets_size() >= 10)) {
                    *error = QStringLiteral("local attachment is missing, over 4 MB, or exceeds 10 files: %1").arg(url);
                    return std::nullopt;
                }
                if (!local.isEmpty()) {
                    QFile asset(local);
                    if (asset.open(QIODevice::ReadOnly)) {
                        auto* a = imported->add_assets();
                        a->set_filename(attachment.value(QStringLiteral("fileName")).toString(assetInfo.fileName()).toStdString());
                        a->set_mime_type(mime.mimeTypeForFile(assetInfo).name().toStdString());
                        a->set_data(asset.readAll().toStdString());
                        continue;
                    }
                    *error = QStringLiteral("cannot read local attachment: %1").arg(local);
                    return std::nullopt;
                }
                if (!url.isEmpty())
                    content += u'\n' + url;
            }
            for (const auto& embedValue : message.value(QStringLiteral("embeds")).toArray()) {
                const QJsonObject embed = embedValue.toObject();
                for (const char* key : {"url", "description"}) {
                    const QString embedText = embed.value(QLatin1StringView(key)).toString();
                    if (!embedText.isEmpty() && !content.contains(embedText))
                        content += u'\n' + embedText;
                }
            }
            for (const auto& stickerValue : message.value(QStringLiteral("stickers")).toArray()) {
                const QString url = stickerValue.toObject().value(QStringLiteral("sourceUrl")).toString();
                if (!url.isEmpty())
                    content += u'\n' + url;
            }
            for (const auto& mentionValue : message.value(QStringLiteral("mentions")).toArray()) {
                const QJsonObject mention = mentionValue.toObject();
                const QString id = discordStringId(mention.value(QStringLiteral("id")));
                if (!id.isEmpty()) {
                    auto* person = imported->add_mentions();
                    person->set_id(id.toStdString());
                    person->set_name(mention.value(QStringLiteral("nickname")).toString(
                        mention.value(QStringLiteral("name")).toString()).toStdString());
                }
            }
            for (const auto& reactionValue : message.value(QStringLiteral("reactions")).toArray()) {
                const QJsonObject reaction = reactionValue.toObject();
                const QJsonObject emoji = reaction.value(QStringLiteral("emoji")).toObject();
                const QString symbol = emoji.value(QStringLiteral("code")).toString(
                    emoji.value(QStringLiteral("name")).toString());
                if (symbol.isEmpty())
                    continue;
                auto* importedReaction = imported->add_reactions();
                importedReaction->set_emoji(symbol.toStdString());
                for (const auto& userValue : reaction.value(QStringLiteral("users")).toArray()) {
                    const QJsonObject user = userValue.toObject();
                    const QString id = discordStringId(user.value(QStringLiteral("id")));
                    if (!id.isEmpty()) {
                        auto* person = importedReaction->add_users();
                        person->set_id(id.toStdString());
                        person->set_name(user.value(QStringLiteral("name")).toString().toStdString());
                    }
                }
            }
            imported->set_content(content.toStdString());
            ++plan.messages;
            if (batch.messages_size() >= 25 || batch.ByteSizeLong() >= 4 * 1024 * 1024) {
                plan.batches.push_back(std::move(batch));
                batch = startBatch();
            }
        }
        if (batch.messages_size() || data.value(QStringLiteral("messages")).toArray().isEmpty())
            plan.batches.push_back(std::move(batch));
    }
    return plan;
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

void Daemon::sendMessage(
    Id cid, const QString& content, Id replyTo, bool action, const QStringList& files, const Responder& r)
{
    if (files.size() > 10) {
        r.error(e::BadRequest, QStringLiteral("at most 10 files per message"));
        return;
    }
    const ActiveModel model{&m_conn};
    auto send = [this, model, cid, replyTo, action, r](proto::Envelope env) {
        auto* s = env.mutable_send_message();
        s->set_channel_id(cid);
        s->set_reply_to(replyTo);
        s->set_is_action(action);
        forward(std::move(env), r,
            [model](const proto::Envelope& reply) { return model->messageJson(reply.chat_message()); });
    };

    if (!m_e2e->appliesTo(cid)) {
        proto::Envelope env;
        env.mutable_send_message()->set_content(content.toStdString());
        if (files.isEmpty()) {
            send(std::move(env));
            return;
        }
        if (!requireConnected(r))
            return;
        uploadAll(cid, files, {},
            [r, send, env](bool ok, const QString& code, const QString& message,
                const std::vector<proto::Attachment>& attachments) mutable {
                if (!ok) {
                    r.error(code, message);
                    return;
                }
                for (const auto& a : attachments)
                    env.mutable_send_message()->add_attachment_ids(a.id());
                send(std::move(env));
            });
        return;
    }

    // End-to-end: files are encrypted before they leave this machine, and
    // their real names and keys travel inside the sealed message.
    if (!m_e2e->active()) {
        r.error(e::BadRequest, QStringLiteral("this device's encryption key is not ready yet; try again in a moment"));
        return;
    }
    struct Sealed {
        QString temp;
        proto::E2EFile file;
    };
    std::vector<Sealed> sealed;
    const QString dir = paths::cacheDir() + QStringLiteral("/e2e-upload");
    if (!files.isEmpty() && (!paths::ensurePrivateDir(paths::cacheDir()) || !paths::ensurePrivateDir(dir))) {
        r.error(e::StorageError, QStringLiteral("cannot create %1").arg(dir));
        return;
    }
    for (const QString& path : files) {
        const QFileInfo info(path);
        Sealed s;
        s.temp = QStringLiteral("%1/%2.enc")
                     .arg(dir)
                     .arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
        QByteArray key;
        QString error;
        if (!info.isFile() || !e2e::encryptFile(info.absoluteFilePath(), s.temp, &key, &error)) {
            for (const auto& done : sealed)
                QFile::remove(done.temp);
            r.error(e::StorageError,
                QStringLiteral("cannot encrypt %1: %2")
                    .arg(info.fileName(), error.isEmpty() ? QStringLiteral("not a file") : error));
            return;
        }
        s.file.set_filename(info.fileName().toStdString());
        s.file.set_mime_type(QMimeDatabase().mimeTypeForFile(info).name().toStdString());
        s.file.set_size(static_cast<std::uint64_t>(info.size()));
        s.file.set_key(key.toStdString());
        sealed.push_back(std::move(s));
    }
    auto sealAndSend
        = [this, cid, content, send, r](std::vector<Sealed> done, const std::vector<proto::Attachment>& attachments) {
              proto::E2EBody body;
              body.set_content(content.toStdString());
              proto::Envelope env;
              for (std::size_t i = 0; i < done.size() && i < attachments.size(); ++i) {
                  done[i].file.set_attachment_id(attachments[i].id());
                  *body.add_files() = done[i].file;
                  env.mutable_send_message()->add_attachment_ids(attachments[i].id());
              }
              m_e2e->seal(cid, std::move(body),
                  [send, env, r](std::optional<std::string> payload, const QString& error) mutable {
                      if (!payload) {
                          r.error(e::BadRequest, error);
                          return;
                      }
                      env.mutable_send_message()->set_encrypted(*payload);
                      send(std::move(env));
                  });
          };
    if (sealed.empty()) {
        sealAndSend({}, {});
        return;
    }
    if (!requireConnected(r))
        return;
    QStringList temps;
    for (const auto& s : sealed)
        temps << s.temp;
    uploadAll(cid, temps, {},
        [r, sealed, sealAndSend](
            bool ok, const QString& code, const QString& message, const std::vector<proto::Attachment>& attachments) {
            for (const auto& s : sealed)
                QFile::remove(s.temp);
            if (!ok) {
                r.error(code, message);
                return;
            }
            sealAndSend(sealed, attachments);
        });
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
                    m_conn->request(std::move(env), [](const proto::Envelope&) { });
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
    // Always the active account's model, also after account.switch.
    const ActiveModel model{&m_conn};

    // ------------------------------------------------------------ daemon
    m[QStringLiteral("daemon.status")] = [this](const QJsonObject&, const Responder& r) { r.ok(statusJson()); };
    m[QStringLiteral("daemon.version")] = [](const QJsonObject&, const Responder& r) {
        r.ok({{"version", QString::fromLatin1(kVersion)}, {"ipc", kIpcVersion},
            {"protocol_major", int(kProtocolMajor)}});
    };
    m[QStringLiteral("state.snapshot")] = [this, model](const QJsonObject&, const Responder& r) {
        QJsonObject snap = model->snapshotJson();
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
        const QJsonArray states = accountsJson();
        for (const auto& a : m_store.accounts()) {
            QJsonObject entry = accountJson(a);
            for (const auto& s : states) {
                if (s.toObject().value(QStringLiteral("id")).toString()
                    == entry.value(QStringLiteral("id")).toString()) {
                    for (const QString& key : {QStringLiteral("state"), QStringLiteral("unread"),
                             QStringLiteral("mentions"), QStringLiteral("active"), QStringLiteral("instance")})
                        entry.insert(key, s.toObject().value(key));
                }
            }
            list.append(entry);
        }
        r.ok(
            {{"accounts", list}, {"active", m_conn->hasAccount() ? QString::number(m_conn->account().id) : QString()}});
    };
    // Makes another saved account the active one; it connects if it was not.
    m[QStringLiteral("account.switch")] = [this](const QJsonObject& p, const Responder& r) {
        const auto a = m_store.account(p.value(QStringLiteral("account")).toVariant().toLongLong());
        if (!a) {
            r.error(e::NotFound, QStringLiteral("no such account"));
            return;
        }
        const bool leftVoice = m_voiceChannel != 0 && a->id != m_active;
        Link& link = linkFor(a->id);
        const auto state = link.conn->state();
        if (!link.conn->hasAccount() || state == ServerConnection::State::Disconnected
            || state == ServerConnection::State::NotConfigured)
            startLink(link, *a);
        activate(a->id);
        r.ok({{"account", accountJson(*a)}, {"left_voice", leftVoice}});
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
        // Another account on the same server already pinned its certificate
        // with the user's explicit consent; the new one inherits that pin.
        for (const auto& other : m_store.accounts()) {
            if (other.host.compare(host, Qt::CaseInsensitive) == 0 && other.port == port
                && !other.trustedFingerprint.isEmpty())
                a.trustedFingerprint = other.trustedFingerprint;
        }
        a.id = m_store.addAccount(a);
        if (!a.id) {
            r.error(e::StorageError, QStringLiteral("could not save account"));
            return std::nullopt;
        }
        if (!a.trustedFingerprint.isEmpty())
            m_store.setTrustedFingerprint(a.id, a.trustedFingerprint);
        return a;
    };
    m[QStringLiteral("account.add")] = [ensureAccount](const QJsonObject& p, const Responder& r) {
        if (auto a = ensureAccount(p, r))
            r.ok(accountJson(*a));
    };
    m[QStringLiteral("account.remove")] = [this](const QJsonObject& p, const Responder& r) {
        const auto id = p.value(QStringLiteral("account")).toVariant().toLongLong();
        if (!m_store.account(id)) {
            r.error(e::NotFound, QStringLiteral("no such account"));
            return;
        }
        auto it = m_links.find(id);
        if (it == m_links.end() || it->second.conn->state() != ServerConnection::State::Connected) {
            r.error(e::NotConnected, QStringLiteral("connect this account before deleting it from the server"));
            return;
        }
        proto::Envelope env;
        env.mutable_delete_account();
        it->second.conn->request(std::move(env), [this, id, r](const proto::Envelope& reply) {
            if (reply.has_error()) {
                r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            auto removedLink = m_links.find(id);
            if (removedLink == m_links.end()) {
                r.error(e::NotConnected, QStringLiteral("account connection was lost"));
                return;
            }
            if (id == m_active) {
                leaveVoice(nullptr);
                const auto others = m_store.accounts();
                const auto next = std::ranges::find_if(others, [id](const Account& a) { return a.id != id; });
                activate(next != others.end() ? next->id : 0);
            }
            removedLink->second.conn->stop();
            removedLink->second.e2e->forget();
            m_credentials->remove(QStringLiteral("refresh/%1").arg(id), {});
            if (!m_store.removeAccount(id)) {
                r.error(e::StorageError, QStringLiteral("server account deleted, but local cleanup failed"));
                return;
            }
            QTimer::singleShot(0, this, [this, id] {
                m_links.erase(id);
                scheduleStatus();
            });
            scheduleStatus();
            r.ok();
        });
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
    static const QHash<QString, proto::OAuthProvider> kOAuthProviders{
        {QStringLiteral("discord"), proto::OAUTH_PROVIDER_DISCORD},
        {QStringLiteral("github"), proto::OAUTH_PROVIDER_GITHUB},
        {QStringLiteral("google"), proto::OAUTH_PROVIDER_GOOGLE},
    };
    m[QStringLiteral("account.oauthLogin")] = [this](const QJsonObject& p, const Responder& r) {
        const auto it = kOAuthProviders.find(p.value(QStringLiteral("provider")).toString().toLower());
        if (it == kOAuthProviders.end()) {
            r.error(e::BadRequest, QStringLiteral("unknown sign-in provider"));
            return;
        }
        const QString host = p.value(QStringLiteral("host")).toString().trimmed();
        const int port = p.value(QStringLiteral("port")).toInt(kDefaultControlPort);
        if (host.isEmpty() || port <= 0 || port > 65535) {
            r.error(e::BadRequest, QStringLiteral("host and port are required"));
            return;
        }
        // The real username is only known once the server responds; this
        // placeholder is renamed after a successful sign-in.
        const QString placeholder = QStringLiteral("oauth-%1").arg(it.key());
        auto account = m_store.findAccount(host, static_cast<quint16>(port), placeholder);
        const bool isNewAccount = !account.has_value();
        if (!account) {
            Account a{0, host, static_cast<quint16>(port), placeholder, {}, 0};
            a.id = m_store.addAccount(a);
            if (!a.id) {
                r.error(e::StorageError, QStringLiteral("could not save account"));
                return;
            }
            account = a;
        }
        startAccount(*account);
        const std::int64_t accountId = account->id;
        const proto::OAuthProvider provider = it.value();
        OAuthLoginFlow::start(*m_conn, provider, OAuthLoginFlow::Mode::Login,
            [this, accountId, isNewAccount, r](bool ok, const QString& code, const QString& message) {
            if (!ok) {
                // An untrusted certificate is not a dead end: the account
                // stays put, exactly like a normal login would, so the
                // client's certificate-trust flow can resume it. Anything
                // else means a placeholder account made just for this
                // attempt is useless; leaving it around would keep retrying
                // an unreachable/misconfigured host forever in the
                // background, with no way back to the login screen while
                // it's active.
                if (isNewAccount && code != e::CertificateError) {
                    const Responder none(nullptr, nullptr, 0);
                    dispatch(QStringLiteral("account.remove"), {{"account", QString::number(accountId)}}, none);
                }
                r.error(code, message);
                return;
            }
            const QString realUsername = m_conn->lastAuthUsername();
            if (!realUsername.isEmpty()) {
                m_store.setUsername(accountId, realUsername);
                if (auto a = m_store.account(accountId))
                    m_conn->updateAccount(*a);
            }
            const auto a = m_store.account(accountId);
            r.ok({{"account", accountJson(a.value_or(Account{}))}});
        });
    };
    m[QStringLiteral("account.oauthLink")] = [this](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        const auto it = kOAuthProviders.find(p.value(QStringLiteral("provider")).toString().toLower());
        if (it == kOAuthProviders.end()) {
            r.error(e::BadRequest, QStringLiteral("unknown sign-in provider"));
            return;
        }
        OAuthLoginFlow::start(*m_conn, it.value(), OAuthLoginFlow::Mode::Link,
            [r](bool ok, const QString& code, const QString& message) {
                if (ok)
                    r.ok();
                else
                    r.error(code, message);
            });
    };
    m[QStringLiteral("account.oauthUnlink")] = [this](const QJsonObject& p, const Responder& r) {
        const auto it = kOAuthProviders.find(p.value(QStringLiteral("provider")).toString().toLower());
        if (it == kOAuthProviders.end()) {
            r.error(e::BadRequest, QStringLiteral("unknown sign-in provider"));
            return;
        }
        proto::Envelope env;
        env.mutable_oauth_unlink()->set_provider(it.value());
        forward(std::move(env), r);
    };
    m[QStringLiteral("account.oauthIdentities")] = [this](const QJsonObject&, const Responder& r) {
        proto::Envelope env;
        env.mutable_list_oauth_identities();
        forward(std::move(env), r, [](const proto::Envelope& reply) {
            static const QHash<int, QString> kNames{
                {proto::OAUTH_PROVIDER_DISCORD, QStringLiteral("discord")},
                {proto::OAUTH_PROVIDER_GITHUB, QStringLiteral("github")},
                {proto::OAUTH_PROVIDER_GOOGLE, QStringLiteral("google")},
            };
            QJsonArray list;
            for (const auto& identity : reply.oauth_identity_list().identities()) {
                list.append(QJsonObject{{"provider", kNames.value(identity.provider())},
                    {"username", QString::fromStdString(identity.provider_username())},
                    {"linked_at", static_cast<double>(identity.linked_at())}});
            }
            return QJsonObject{{"identities", list}};
        });
    };
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
    m[QStringLiteral("server.list")] = [this, model](const QJsonObject&, const Responder& r) {
        if (!requireConnected(r))
            return;
        QJsonArray list;
        for (const auto& [id, s] : model->servers())
            list.append(model->serverJson(s));
        r.ok({{"servers", list}});
    };
    // Creating or joining a server brings roles, channels and members with
    // it: reply only once the resynchronized model contains all of it.
    auto createOrJoin = [this, model](proto::Envelope env, const Responder& r) {
        if (!requireConnected(r))
            return;
        m_conn->request(std::move(env), [this, r, model](const proto::Envelope& reply) {
            if (reply.has_error()) {
                r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            const proto::Server server = reply.server();
            m_conn->resync([r, server, model](bool) {
                const proto::Server* synced = model->server(server.id());
                r.ok(model->serverJson(synced ? *synced : server));
            });
        });
    };
    m[QStringLiteral("server.create")] = [createOrJoin](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_create_server()->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        createOrJoin(std::move(env), r);
    };
    m[QStringLiteral("server.create_from_discord")] = [this, model](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        if (!m_conn->capabilities().contains(QStringLiteral("discord.import"))) {
            r.error(e::BadRequest, QStringLiteral("this OmaChat server does not support Discord imports yet"));
            return;
        }
        const auto name = validation::serverName(p.value(QStringLiteral("name")).toString());
        if (!name) {
            r.error(e::BadRequest, QStringLiteral("enter a server name"));
            return;
        }
        QString error;
        auto prepared = prepareDiscordImport(p.value(QStringLiteral("files")).toArray(), &error);
        if (!prepared) {
            r.error(e::BadRequest, error);
            return;
        }
        auto plan = std::make_shared<DiscordImportPlan>(std::move(*prepared));
        proto::Envelope env;
        env.mutable_create_server()->set_name(name->toStdString());
        m_conn->request(std::move(env), [this, r, model, plan](const proto::Envelope& created) {
            if (created.has_error()) {
                r.error(ipcErrorCode(created.error().code()), QString::fromStdString(created.error().message()));
                return;
            }
            if (!created.has_server()) {
                r.error(e::Internal, QStringLiteral("server creation returned no server"));
                return;
            }
            const proto::Server server = created.server();
            auto index = std::make_shared<size_t>(0);
            auto imported = std::make_shared<int>(0);
            auto next = std::make_shared<std::function<void()>>();
            const std::weak_ptr<std::function<void()>> weakNext = next;
            *next = [this, r, model, plan, server, index, imported, weakNext]() {
                if (*index == plan->batches.size()) {
                    m_conn->resync([this, r, model, server, imported](bool ok) {
                        if (!ok) {
                            r.error(e::NotConnected, QStringLiteral("import finished, but refresh failed; reconnect to see it"));
                            return;
                        }
                        const auto* synced = model->server(server.id());
                        QJsonObject result = model->serverJson(synced ? *synced : server);
                        result.insert(QStringLiteral("imported"), *imported);
                        r.ok(result);
                    });
                    return;
                }
                proto::Envelope batch;
                auto* request = batch.mutable_import_discord_batch();
                *request = plan->batches[*index];
                request->set_server_id(server.id());
                auto continuation = weakNext.lock();
                m_conn->request(std::move(batch), [this, r, index, imported, continuation, server, plan](const proto::Envelope& reply) {
                    if (reply.has_error() || !reply.has_import_discord_result()) {
                        m_conn->resync([](bool) {});
                        const QString detail = reply.has_error() ? QString::fromStdString(reply.error().message())
                                                               : QStringLiteral("invalid import reply");
                        r.error(e::Internal, QStringLiteral("Server was created, but Discord import stopped: %1. "
                            "You can retry the export into this server with the offline importer. (Server ID %2)")
                            .arg(detail, QString::number(server.id())));
                        return;
                    }
                    *imported += static_cast<int>(reply.import_discord_result().imported());
                    ++*index;
                    m_ipc.broadcast(QStringLiteral("discord.import_progress"),
                        {{"completed", static_cast<int>(*index)}, {"total", static_cast<int>(plan->batches.size())},
                         {"messages", *imported}});
                    (*continuation)();
                }, 60000);
            };
            (*next)();
        });
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
    m[QStringLiteral("channel.list")] = [this, model](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        Id sid = 0;
        if (p.contains(QStringLiteral("server")) && !(sid = serverParam(p, r)))
            return;
        QJsonArray list;
        for (const auto& [id, c] : model->channels()) {
            if (sid && c.server_id() != sid)
                continue;
            QJsonObject cj = model->channelJson(c);
            if (c.type() == proto::CHANNEL_TYPE_VOICE) {
                QJsonArray members;
                for (Id uid : model->voiceParticipants(id))
                    members.append(idString(uid));
                cj.insert(QStringLiteral("voice_members"), members);
            }
            cj.insert(QStringLiteral("muted"), m_mutedChannels.contains(id));
            list.append(cj);
        }
        r.ok({{"channels", list}});
    };
    m[QStringLiteral("channel.create")] = [this, model](const QJsonObject& p, const Responder& r) {
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
        forward(std::move(env), r, [this, model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model->channelJson(reply.channel());
        });
    };
    m[QStringLiteral("channel.update")] = [this, model](const QJsonObject& p, const Responder& r) {
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
            std::move(env), r, [model](const proto::Envelope& reply) { return model->channelJson(reply.channel()); });
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
    m[QStringLiteral("dm.open")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        proto::Envelope env;
        env.mutable_open_dm()->set_user_id(uid);
        forward(std::move(env), r, [this, model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model->channelJson(reply.channel());
        });
    };
    m[QStringLiteral("dm.create")] = [this, model](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        auto* g = env.mutable_create_group_dm();
        for (const auto& v : p.value(QStringLiteral("users")).toArray()) {
            const Id uid = userParam(QJsonObject{{"user", v}}, r);
            if (!uid)
                return;
            g->add_user_ids(uid);
        }
        g->set_name(p.value(QStringLiteral("name")).toString().toStdString());
        forward(std::move(env), r, [this, model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model->channelJson(reply.channel());
        });
    };
    m[QStringLiteral("dm.add")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        const Id uid = cid ? userParam(p, r) : 0;
        if (!uid)
            return;
        proto::Envelope env;
        env.mutable_add_group_dm_recipient()->set_channel_id(cid);
        env.mutable_add_group_dm_recipient()->set_user_id(uid);
        forward(std::move(env), r, [this, model](const proto::Envelope& reply) {
            m_conn->model().upsertChannel(reply.channel());
            return model->channelJson(reply.channel());
        });
    };
    m[QStringLiteral("dm.leave")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        proto::Envelope env;
        env.mutable_leave_group_dm()->set_channel_id(cid);
        forward(std::move(env), r);
    };

    // ----------------------------------------------------------- messages
    m[QStringLiteral("message.history")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        proto::Envelope env;
        auto* g = env.mutable_get_messages();
        g->set_channel_id(cid);
        g->set_before_message_id(idFromJson(p.value(QStringLiteral("before"))));
        g->set_limit(static_cast<std::uint32_t>(std::clamp(p.value(QStringLiteral("limit")).toInt(50), 1, 100)));
        forward(std::move(env), r, [model](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& msg : reply.message_page().messages())
                list.append(model->messageJson(msg));
            return QJsonObject{{"channel_id", idString(reply.message_page().channel_id())}, {"messages", list},
                {"has_more", reply.message_page().has_more()}};
        });
    };
    m[QStringLiteral("message.send")] = [this](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        QStringList files;
        for (const auto& f : p.value(QStringLiteral("files")).toArray())
            files << f.toString();
        sendMessage(cid, p.value(QStringLiteral("content")).toString(), idFromJson(p.value(QStringLiteral("reply_to"))),
            p.value(QStringLiteral("action")).toBool(false), files, r);
    };
    // One step for scripts: opens the conversation and sends into it.
    m[QStringLiteral("dm.send")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        proto::Envelope env;
        env.mutable_open_dm()->set_user_id(uid);
        const QString content = p.value(QStringLiteral("content")).toString();
        m_conn->request(std::move(env), [this, model, content, r](const proto::Envelope& reply) {
            if (reply.has_error()) {
                r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            m_conn->model().upsertChannel(reply.channel());
            sendMessage(reply.channel().id(), content, 0, false, {}, r);
        });
    };
    m[QStringLiteral("message.edit")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id messageId = idFromJson(p.value(QStringLiteral("message")));
        const QString content = p.value(QStringLiteral("content")).toString();
        proto::Envelope env;
        auto* ed = env.mutable_edit_message();
        ed->set_message_id(messageId);
        const auto channel = m_e2e->channelOf(messageId);
        if (!channel) {
            ed->set_content(content.toStdString());
            forward(std::move(env), r,
                [model](const proto::Envelope& reply) { return model->messageJson(reply.chat_message()); });
            return;
        }
        // An encrypted message stays encrypted, attachments included.
        proto::E2EBody body;
        body.set_content(content.toStdString());
        for (const auto& f : m_e2e->filesOf(messageId))
            *body.add_files() = f;
        m_e2e->seal(*channel, std::move(body),
            [this, model, env, r](std::optional<std::string> payload, const QString& error) mutable {
                if (!payload) {
                    r.error(e::BadRequest, error);
                    return;
                }
                env.mutable_edit_message()->set_encrypted(*payload);
                forward(std::move(env), r,
                    [model](const proto::Envelope& reply) { return model->messageJson(reply.chat_message()); });
            });
    };
    m[QStringLiteral("message.delete")] = [this](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        env.mutable_delete_message()->set_message_id(idFromJson(p.value(QStringLiteral("message"))));
        forward(std::move(env), r);
    };
    m[QStringLiteral("message.search")] = [this, model](const QJsonObject& p, const Responder& r) {
        // {channel} searches one channel; {server} every channel of it you can read.
        proto::Envelope env;
        auto* s = env.mutable_search_messages();
        if (p.contains(QStringLiteral("server")) && !p.contains(QStringLiteral("channel"))) {
            const Id sid = serverParam(p, r);
            if (!sid)
                return;
            s->set_server_id(sid);
        } else {
            const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
            if (!cid)
                return;
            s->set_channel_id(cid);
        }
        s->set_query(p.value(QStringLiteral("query")).toString().toStdString());
        s->set_limit(static_cast<std::uint32_t>(std::clamp(p.value(QStringLiteral("limit")).toInt(25), 1, 50)));
        forward(std::move(env), r, [model](const proto::Envelope& reply) {
            QJsonArray list;
            for (const auto& msg : reply.message_page().messages())
                list.append(model->messageJson(msg));
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
    // ------------------------------------------------------------- emoji
    m[QStringLiteral("emoji.create")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Messages);
        if (!cid)
            return;
        const QString name = p.value(QStringLiteral("name")).toString();
        const QString path = p.value(QStringLiteral("file")).toString();
        if (!requireConnected(r))
            return;
        uploadAll(cid, {path}, {},
            [this, model, sid, name, r](bool ok, const QString& code, const QString& message,
                const std::vector<proto::Attachment>& attachments) {
                if (!ok || attachments.empty()) {
                    r.error(ok ? e::Internal : code, ok ? QStringLiteral("upload failed") : message);
                    return;
                }
                proto::Envelope env;
                auto* ce = env.mutable_create_emoji();
                ce->set_server_id(sid);
                ce->set_name(name.toStdString());
                ce->set_attachment_id(attachments.front().id());
                forward(std::move(env), r,
                    [model](const proto::Envelope& reply) { return model->emojiJson(reply.custom_emoji()); });
            });
    };
    m[QStringLiteral("emoji.delete")] = [this](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        proto::Envelope env;
        auto* de = env.mutable_delete_emoji();
        de->set_server_id(sid);
        de->set_emoji_id(idFromJson(p.value(QStringLiteral("emoji"))));
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
        if (p.value(QStringLiteral("to")).toString() == u"cache" && size > 0
            && QFileInfo(dest).size() == qint64(size)) {
            r.ok({{"path", dest}, {"cached", true}});
            return;
        }
        if (!requireConnected(r))
            return;
        if (const auto file = m_e2e->fileFor(aid)) {
            // End-to-end encrypted: fetch the ciphertext, then decrypt it in place.
            const QString sealed = dest + QStringLiteral(".encrypted");
            const QByteArray key = QByteArray::fromStdString(file->key());
            m_transfers->download(aid, sealed, [r, sealed, dest, key](const FileTransfers::Result& res) {
                if (!res.ok) {
                    r.error(res.code, res.message);
                    return;
                }
                QString why;
                const bool ok = e2e::decryptFile(sealed, dest, key, &why);
                QFile::remove(sealed);
                if (ok)
                    r.ok({{"path", dest}, {"cached", false}});
                else
                    r.error(e::StorageError, why);
            });
            return;
        }
        m_transfers->download(aid, dest, [r](const FileTransfers::Result& res) {
            if (res.ok)
                r.ok({{"path", res.path}, {"cached", false}});
            else
                r.error(res.code, res.message);
        });
    };
    m[QStringLiteral("transfer.list")]
        = [this](const QJsonObject&, const Responder& r) { r.ok({{"transfers", m_transfers->activeJson()}}); };
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
    m[QStringLiteral("profile.update")] = [this](const QJsonObject& p, const Responder& r) {
        proto::Envelope env;
        auto* profile = env.mutable_update_profile();
        profile->set_display_name(p.value(QStringLiteral("display_name")).toString().toStdString());
        profile->set_avatar_url(p.value(QStringLiteral("avatar_url")).toString().toStdString());
        profile->set_bio(p.value(QStringLiteral("bio")).toString().toStdString());
        forward(std::move(env), r);
    };
    m[QStringLiteral("member.list")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id sid = serverParam(p, r);
        if (!sid)
            return;
        const QJsonObject snap = model->snapshotJson();
        QJsonArray members;
        for (const auto& v : snap.value(QStringLiteral("members")).toArray()) {
            const QJsonObject mem = v.toObject();
            if (idFromJson(mem.value(QStringLiteral("server_id"))) != sid)
                continue;
            QJsonObject entry = mem;
            if (const auto* u = model->user(idFromJson(mem.value(QStringLiteral("user_id")))))
                entry.insert(QStringLiteral("user"), model->userJson(*u));
            members.append(entry);
        }
        r.ok({{"members", members}});
    };

    // -------------------------------------------------------------- voice
    m[QStringLiteral("voice.join")] = [this, model](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        const Id cid = channelParam(p, r, "channel", ClientState::ChannelKind::Voice);
        if (!cid)
            return;
        const auto* c = model->channel(cid);
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

    // --------------------------------------------------- end-to-end encryption
    m[QStringLiteral("e2e.status")] = [this](const QJsonObject&, const Responder& r) { r.ok(m_e2e->statusJson()); };
    // Safety number with one person: compare it with them over another channel.
    m[QStringLiteral("e2e.safety")] = [this](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (uid)
            r.ok(m_e2e->safetyJson(uid));
    };
    m[QStringLiteral("e2e.verify")] = [this](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        if (!m_e2e->setVerified(uid, p.value(QStringLiteral("verified")).toBool(true))) {
            r.error(e::NotFound, QStringLiteral("no encryption keys are known for that user yet"));
            return;
        }
        r.ok(m_e2e->safetyJson(uid));
    };

    // ------------------------------------------------------- screen sharing
    // stream.start answers once the desktop picker is done: call it without a timeout.
    m[QStringLiteral("stream.start")] = [this](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        if (!m_conn->capabilities().contains(QStringLiteral("video.h264"))) {
            r.error(e::BadRequest, QStringLiteral("this server does not support screen sharing"));
            return;
        }
        if (!m_voiceChannel || !m_voice->active()) {
            r.error(e::BadRequest, QStringLiteral("join a voice channel to share your screen"));
            return;
        }
        m_video->setShareAudio(p.value(QStringLiteral("audio")).toBool(m_config.video.audio));
        m_video->startSharing([this, r](bool ok, const QString& error) {
            if (!ok) {
                r.error(error == u"cancelled" ? e::BadRequest : e::MediaDeviceUnavailable, error);
                return;
            }
            setStreaming(true, [this, r](bool accepted, const QString& code, const QString& message) {
                if (!accepted) {
                    m_video->stopSharing();
                    r.error(code, message);
                    return;
                }
                m_streamConfirmed = true;
                scheduleStatus();
                r.ok(m_video->statsJson().value(QStringLiteral("share")).toObject());
            });
        });
    };
    m[QStringLiteral("stream.stop")] = [this](const QJsonObject&, const Responder& r) {
        const bool was = m_video->sharing();
        m_video->stopSharing();
        m_streamConfirmed = false;
        if (was && m_voiceChannel && m_conn->state() == ServerConnection::State::Connected)
            setStreaming(false, {});
        scheduleStatus();
        r.ok();
    };
    m[QStringLiteral("stream.watch")] = [this](const QJsonObject& p, const Responder& r) {
        if (!requireConnected(r))
            return;
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        proto::Envelope env;
        env.mutable_watch_stream()->set_user_id(uid);
        env.mutable_watch_stream()->set_watch(true);
        m_conn->request(std::move(env), [this, r, uid](const proto::Envelope& reply) {
            if (reply.has_error()) {
                r.error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            QString error;
            const auto path = m_video->watch(uid, &error);
            if (!path) {
                r.error(e::StorageError, error);
                return;
            }
            r.ok({{"user_id", idString(uid)}, {"path", *path}});
        });
    };
    m[QStringLiteral("stream.unwatch")] = [this](const QJsonObject& p, const Responder& r) {
        const Id uid = userParam(p, r);
        if (!uid)
            return;
        m_video->unwatch(uid);
        if (m_conn->state() == ServerConnection::State::Connected) {
            proto::Envelope env;
            env.mutable_watch_stream()->set_user_id(uid);
            env.mutable_watch_stream()->set_watch(false);
            m_conn->request(std::move(env), [](const proto::Envelope&) { });
        }
        r.ok();
    };
    m[QStringLiteral("stream.stats")] = [this](const QJsonObject&, const Responder& r) { r.ok(m_video->statsJson()); };
    // Fire-and-forget: the GUI sends these continuously while pointing, so
    // there is nothing useful to report back beyond an error.
    m[QStringLiteral("stream.pointer")] = [this](const QJsonObject& p, const Responder& r) {
        if (!m_video->sharing()) {
            r.error(e::BadRequest, QStringLiteral("not sharing your screen"));
            return;
        }
        m_video->sendPointer(p.value(QStringLiteral("active")).toBool(),
            p.value(QStringLiteral("x")).toDouble(), p.value(QStringLiteral("y")).toDouble());
        r.ok();
    };
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
    // Screen-share quality; applies to the next share.
    m[QStringLiteral("video.settings")] = [this](const QJsonObject&, const Responder& r) {
        const auto& v = m_config.video;
        r.ok({{"fps", v.fps}, {"max_height", v.maxHeight}, {"bitrate_kbps", v.bitrateKbps}, {"encoder", v.encoder},
            {"audio", v.audio}, {"encoders", QJsonArray::fromStringList(video::H264Encoder::available())}});
    };
    m[QStringLiteral("video.set")] = [this](const QJsonObject& p, const Responder& r) {
        auto& v = m_config.video;
        if (p.contains(QStringLiteral("fps")))
            v.fps = std::clamp(p.value(QStringLiteral("fps")).toInt(), 5, 60);
        if (p.contains(QStringLiteral("max_height")))
            v.maxHeight = std::clamp(p.value(QStringLiteral("max_height")).toInt(), 360, 1440);
        if (p.contains(QStringLiteral("bitrate_kbps")))
            v.bitrateKbps = std::clamp(p.value(QStringLiteral("bitrate_kbps")).toInt(), 500, 20000);
        if (p.contains(QStringLiteral("audio")))
            v.audio = p.value(QStringLiteral("audio")).toBool();
        if (p.contains(QStringLiteral("encoder"))) {
            const QString enc = p.value(QStringLiteral("encoder")).toString();
            if (enc != u"auto" && !video::H264Encoder::available().contains(enc)) {
                r.error(e::BadRequest,
                    QStringLiteral("encoder must be auto or one of: %1")
                        .arg(video::H264Encoder::available().join(QStringLiteral(", "))));
                return;
            }
            v.encoder = enc;
        }
        QString err;
        if (!saveConfig(&err))
            OMA_WARN("daemon", "could not save config", {"error", err});
        applyConfig();
        dispatch(QStringLiteral("video.settings"), {}, r);
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
    m[QStringLiteral("role.create")] = [this, model](const QJsonObject& p, const Responder& r) {
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
        forward(std::move(env), r, [model](const proto::Envelope& reply) { return model->roleJson(reply.role()); });
    };
    // Fields left out keep their current value.
    m[QStringLiteral("role.update")] = [this, model](const QJsonObject& p, const Responder& r) {
        const proto::Role* current = model->role(idFromJson(p.value(QStringLiteral("role"))));
        if (!current) {
            r.error(e::NotFound, QStringLiteral("no such role"));
            return;
        }
        bool ok = true;
        const auto bits = p.contains(QStringLiteral("permissions"))
            ? permissionBits(p.value(QStringLiteral("permissions")), &ok)
            : current->permissions();
        if (!ok) {
            r.error(e::BadRequest, QStringLiteral("unknown permission name"));
            return;
        }
        proto::Envelope env;
        auto* u = env.mutable_update_role();
        u->set_role_id(current->id());
        u->set_name(p.contains(QStringLiteral("name")) ? p.value(QStringLiteral("name")).toString().toStdString()
                                                       : current->name());
        u->set_permissions(bits);
        u->set_position(p.contains(QStringLiteral("position"))
                ? static_cast<std::uint32_t>(std::max(1, p.value(QStringLiteral("position")).toInt(1)))
                : current->position());
        u->set_color(p.contains(QStringLiteral("color"))
                ? p.value(QStringLiteral("color")).toString().remove(u'#').toUInt(nullptr, 16)
                : current->color());
        forward(std::move(env), r, [model](const proto::Envelope& reply) { return model->roleJson(reply.role()); });
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
            const Id uid = userParam(p, r);
            if (!uid)
                return;
            o->set_target_type(proto::PermissionOverride::TARGET_USER);
            o->set_target_id(uid);
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

    m[QStringLiteral("channel.join")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        const auto* c = model->channel(cid);
        if (c && c->type() == proto::CHANNEL_TYPE_VOICE) {
            if (requireConnected(r))
                joinVoice(cid, &r);
            return;
        }
        m_ipc.broadcast(QStringLiteral("ui.navigate"),
            {{"channel_id", idString(cid)}, {"server_id", idString(c ? c->server_id() : 0)}});
        r.ok({{"channel_id", idString(cid)}, {"focused", true}});
    };
    m[QStringLiteral("ui.navigate")] = [this, model](const QJsonObject& p, const Responder& r) {
        const Id cid = channelParam(p, r);
        if (!cid)
            return;
        const auto* c = model->channel(cid);
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
