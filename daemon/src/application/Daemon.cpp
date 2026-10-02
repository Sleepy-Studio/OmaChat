#include "application/Daemon.hpp"

#include "audio/PipeWireBackend.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Version.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace omachat::daemon {

Daemon::Daemon(DaemonOptions options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    // Coalesce bursts of changes into a single "status" event.
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(0);
    connect(
        &m_statusTimer, &QTimer::timeout, this, [this] { m_ipc.broadcast(QStringLiteral("status"), statusJson()); });
}

Daemon::~Daemon()
{
    shutdown();
}

void Daemon::pruneAttachmentCache()
{
    // Cached copies can always be fetched again; drop any older than a month.
    const QDir cache(paths::cacheDir() + QStringLiteral("/attachments"));
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-30);
    int removed = 0;
    for (const QFileInfo& entry : cache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.lastModified() < cutoff && QDir(entry.absoluteFilePath()).removeRecursively())
            ++removed;
    }
    if (removed > 0)
        OMA_INFO("daemon", "pruned attachment cache", {"entries", removed});
}

bool Daemon::start(QString* error)
{
    QString configError;
    m_config = config::ClientConfig::load(m_options.configPath, &configError);
    if (!configError.isEmpty())
        OMA_WARN(
            "daemon", "config file has errors; using defaults", {"path", m_options.configPath}, {"error", configError});
    if (auto lvl = log::parseLevel(m_config.logLevel.toStdString());
        lvl && !qEnvironmentVariableIsSet("OMACHAT_LOG_LEVEL"))
        log::setLevel(*lvl);

    paths::ensurePrivateDir(QFileInfo(m_options.databasePath).absolutePath());
    if (!m_store.open(m_options.databasePath, error))
        return false;
    pruneAttachmentCache();

    if (m_options.memoryCredentials)
        m_credentials = std::make_unique<MemoryCredentialStore>();
    else
        m_credentials = makeKeychainStore(this);

    if (m_options.nullAudio) {
        m_audio = std::make_unique<audio::NullAudioBackend>();
    } else {
        auto pw = std::make_unique<audio::PipeWireBackend>();
        if (pw->available()) {
            m_audio = std::move(pw);
        } else {
            m_audioError = QStringLiteral("PipeWire is not available; voice will be receive-less and silent");
            OMA_WARN("daemon", "PipeWire unavailable, voice audio disabled");
            m_audio = std::make_unique<audio::NullAudioBackend>();
        }
    }
    connect(m_audio.get(), &audio::AudioBackend::devicesChanged, this, [this] {
        QJsonArray inputs, outputs;
        for (const auto& d : m_audio->devices())
            (d.input ? inputs : outputs).append(QJsonObject{{"id", d.id}, {"name", d.description}});
        m_ipc.broadcast(QStringLiteral("audio.devices"), {{"inputs", inputs}, {"outputs", outputs}});
    });

    m_voice = std::make_unique<voice::VoiceEngine>(*m_audio, this);
    m_voice->applySettings(engineSettings());
    connect(m_voice.get(), &voice::VoiceEngine::speakingChanged, this, [this](quint64 userId, bool speaking) {
        if (speaking)
            m_speaking.insert(userId);
        else
            m_speaking.erase(userId);
        m_ipc.broadcast(QStringLiteral("voice.speaking"), {{"user_id", idString(userId)}, {"speaking", speaking}});
        scheduleStatus();
    });
    connect(m_voice.get(), &voice::VoiceEngine::registeredChanged, this, [this](bool) { scheduleStatus(); });
    connect(m_voice.get(), &voice::VoiceEngine::failed, this, [this](const QString& reason) {
        m_audioError = reason;
        m_ipc.broadcast(
            QStringLiteral("voice.error"), {{"code", ipc::errors::MediaDeviceUnavailable}, {"message", reason}});
        scheduleStatus();
    });

    m_video = std::make_unique<video::VideoManager>(*m_voice, this);
    m_video->setSettings(videoSettings());
    m_video->setFrameDirectory(paths::runtimeDir() + QStringLiteral("/video"));
    if (m_options.syntheticScreen) {
        m_video->setSourceFactory([] { return std::make_unique<video::SyntheticScreenSource>(1280, 720, 30); });
        m_video->setAudioFactory([] { return std::make_unique<video::ToneAudioSource>(440.0); });
    }
    connect(m_video.get(), &video::VideoManager::changed, this, &Daemon::scheduleStatus);
    connect(m_video.get(), &video::VideoManager::sharingEnded, this, [this] {
        OMA_INFO("video", "screen sharing ended from the desktop");
        m_streamConfirmed = false;
        if (m_voiceChannel && m_conn->state() == ServerConnection::State::Connected)
            setStreaming(false, {});
        m_ipc.broadcast(QStringLiteral("stream.ended"), {});
    });

    activate(0); // a placeholder link until an account exists

    m_ipc.setHandler(
        [this](const QString& method, const QJsonObject& params, const Responder& r) { dispatch(method, params, r); });
    connect(&m_ipc, &IpcServer::clientDisconnected, this, [this](QLocalSocket* socket) {
        // A client holding push-to-talk vanished: never leave the mic open.
        if (socket == m_pttOwner) {
            m_pttOwner = nullptr;
            m_voice->setPushToTalk(false);
            scheduleStatus();
        }
    });
    registerMethods();

    if (!m_ipc.listen(m_options.socketPath, error))
        return false;
    OMA_INFO("daemon", "listening", {"socket", m_options.socketPath}, {"audio", m_audio->name()},
        {"credentials", m_credentials->backendName()});

    // Every saved account connects; the most recently used one is active.
    const auto accounts = m_store.accounts();
    for (auto it = accounts.rbegin(); it != accounts.rend(); ++it)
        startLink(linkFor(it->id), *it);
    if (!accounts.empty())
        activate(accounts.front().id);
    return true;
}

Daemon::Link& Daemon::linkFor(std::int64_t accountId)
{
    if (auto it = m_links.find(accountId); it != m_links.end())
        return it->second;
    Link& link = m_links[accountId];
    link.conn = std::make_unique<ServerConnection>(*m_credentials, this);
    link.transfers = std::make_unique<FileTransfers>(*link.conn, this);
    link.e2e = std::make_unique<E2EManager>(*link.conn, *m_credentials, m_store, this);
    ServerConnection* conn = link.conn.get();
    E2EManager* e2e = link.e2e.get();
    conn->model().setDecryptor([e2e](const proto::ChatMessage& m) {
        auto d = e2e->decrypt(m);
        return ClientState::Decrypted{d.content, std::move(d.files), d.status};
    });
    connect(e2e, &E2EManager::keysChanged, this, [this, conn](quint64 userId) {
        if (conn != m_conn)
            return;
        const proto::User* u = conn->model().user(userId);
        m_ipc.broadcast(QStringLiteral("e2e.keys_changed"),
            {{"user_id", idString(userId)}, {"name", u ? QString::fromStdString(u->display_name()) : QString()},
                {"self", userId == conn->model().self().id()}});
    });
    connect(e2e, &E2EManager::readyChanged, this, [this, conn] {
        if (conn != m_conn)
            return;
        m_ipc.broadcast(QStringLiteral("state.reset"), {}); // messages may now decrypt
        scheduleStatus();
    });
    connect(conn, &ServerConnection::stateChanged, this, [this, conn] {
        scheduleStatus();
        if (conn != m_conn)
            return;
        m_ipc.broadcast(QStringLiteral("connection"), statusJson());
        if (conn->state() == ServerConnection::State::Disconnected
            || conn->state() == ServerConnection::State::LoginRequired) {
            if (m_voice->active())
                stopVoiceEngine();
        }
    });
    connect(conn, &ServerConnection::modelEvent, this, [e2e, conn](const QString& name, const QJsonObject& data) {
        if (name == u"e2e.device_keys_changed") {
            e2e->onDeviceKeysChanged(idFromJson(data.value(QStringLiteral("user_id"))));
        } else if (name == u"channel.created") {
            if (const proto::Channel* c = conn->model().channel(idFromJson(data.value(QStringLiteral("id")))))
                e2e->onChannel(*c);
        }
    });
    connect(conn, &ServerConnection::modelEvent, this,
        [this, accountId, conn](const QString& name, const QJsonObject& data) {
            if (conn == m_conn)
                onModelEvent(name, data);
            else if (auto it = m_links.find(accountId); it != m_links.end())
                onBackgroundEvent(it->second, name, data);
        });
    connect(link.transfers.get(), &FileTransfers::progress, this, [this, accountId](const QJsonObject& data) {
        QJsonObject withAccount = data;
        withAccount.insert(QStringLiteral("account"), QString::number(accountId));
        m_ipc.broadcast(QStringLiteral("transfer.progress"), withAccount);
    });
    connect(conn, &ServerConnection::synchronized, this, [this, accountId, conn, e2e] {
        e2e->onSynchronized();
        if (conn != m_conn) {
            if (auto it = m_links.find(accountId); it != m_links.end())
                it->second.muted = m_store.mutedChannels(accountId);
            scheduleStatus();
            return;
        }
        m_mutedChannels = m_store.mutedChannels(conn->account().id);
        for (const auto& [uid, gain] : m_store.userVolumes(conn->account().id))
            m_voice->setUserGain(uid, gain);
        m_ipc.broadcast(QStringLiteral("state.reset"), {});
        checkVoiceAfterSync();
        scheduleStatus();
    });
    return link;
}

void Daemon::activate(std::int64_t accountId)
{
    Link& link = linkFor(accountId);
    if (m_conn == link.conn.get())
        return;
    if (m_conn && (m_voiceChannel || m_voice->active())) {
        OMA_INFO("voice", "left voice: switched account");
        leaveVoice(nullptr);
    }
    if (m_pttOwner) {
        m_pttOwner = nullptr;
        m_voice->setPushToTalk(false);
    }
    m_active = accountId;
    m_conn = link.conn.get();
    m_transfers = link.transfers.get();
    m_e2e = link.e2e.get();
    link.unread = 0;
    link.mentions = 0;
    m_focusedChannel = 0;
    m_focusedServer = 0;
    m_mutedChannels = accountId ? m_store.mutedChannels(accountId) : std::set<std::uint64_t>{};
    if (accountId) {
        m_store.touchAccount(accountId);
        for (const auto& [uid, gain] : m_store.userVolumes(accountId))
            m_voice->setUserGain(uid, gain);
    }
    OMA_INFO("daemon", "active account", {"account", accountId});
    m_ipc.broadcast(QStringLiteral("state.reset"), {});
    m_ipc.broadcast(QStringLiteral("connection"), statusJson());
    scheduleStatus();
}

void Daemon::onBackgroundEvent(Link& link, const QString& name, const QJsonObject& data)
{
    if (name != u"message.created")
        return;
    const Id author = idFromJson(data.value(QStringLiteral("author_id")));
    const Id channel = idFromJson(data.value(QStringLiteral("channel_id")));
    if (author == link.conn->model().self().id() || link.muted.contains(channel))
        return;
    link.unread += 1;
    const bool mentioned = data.value(QStringLiteral("mentions_me")).toBool();
    const proto::Channel* c = link.conn->model().channel(channel);
    const bool dm = c && (c->type() == proto::CHANNEL_TYPE_DM || c->type() == proto::CHANNEL_TYPE_GROUP_DM);
    if (mentioned || dm)
        link.mentions += 1;
    maybeNotify(*link.conn, link.muted, data);
    m_ipc.broadcast(QStringLiteral("account.activity"),
        {{"account", QString::number(link.conn->account().id)}, {"unread", link.unread}, {"mentions", link.mentions}});
    scheduleStatus();
}

QJsonArray Daemon::accountsJson() const
{
    QJsonArray out;
    for (const auto& a : m_store.accounts()) {
        auto it = m_links.find(a.id);
        const ServerConnection* c = it == m_links.end() ? nullptr : it->second.conn.get();
        out.append(QJsonObject{{"id", QString::number(a.id)}, {"host", a.host}, {"port", a.port},
            {"username", a.username}, {"active", a.id == m_active},
            {"state", c ? ServerConnection::stateName(c->state()) : QStringLiteral("disconnected")},
            {"instance", c ? c->instanceName() : QString()}, {"unread", it == m_links.end() ? 0 : it->second.unread},
            {"mentions", it == m_links.end() ? 0 : it->second.mentions}});
    }
    return out;
}

void Daemon::shutdown()
{
    if (m_voice)
        stopVoiceEngine();
    for (auto& [id, link] : m_links)
        link.conn->stop();
    m_ipc.close();
}

void Daemon::scheduleStatus()
{
    if (!m_statusTimer.isActive())
        m_statusTimer.start();
}

voice::VoiceEngine::Settings Daemon::engineSettings() const
{
    voice::VoiceEngine::Settings s;
    s.input = m_config.audio.input;
    s.output = m_config.audio.output;
    s.mode = m_config.audio.mode;
    s.dsp.highPass = m_config.audio.highPass;
    s.dsp.noiseSuppression = m_config.audio.noiseSuppression;
    s.dsp.automaticGain = m_config.audio.automaticGain;
    s.dsp.inputGain = m_config.audio.inputVolume;
    s.dsp.vadThresholdDb = m_config.audio.vadThresholdDb;
    s.dsp.vadHangoverMs = m_config.audio.vadHangoverMs;
    s.bitrate = m_config.audio.bitrate;
    s.fec = m_config.audio.fec;
    s.jitterMinMs = m_config.audio.jitterMinMs;
    s.jitterMaxMs = m_config.audio.jitterMaxMs;
    s.outputVolume = m_config.audio.outputVolume;
    return s;
}

video::H264Encoder::Settings Daemon::videoSettings() const
{
    video::H264Encoder::Settings s;
    s.fps = m_config.video.fps;
    s.maxHeight = m_config.video.maxHeight;
    s.maxWidth = (m_config.video.maxHeight * 16 + 8) / 9; // wide enough for 16:9 at that height
    s.bitrateKbps = m_config.video.bitrateKbps;
    s.encoder = m_config.video.encoder;
    return s;
}

void Daemon::setStreaming(bool streaming, std::function<void(bool, const QString&, const QString&)> done)
{
    proto::Envelope env;
    env.mutable_set_streaming()->set_streaming(streaming);
    m_conn->request(std::move(env), [done](const proto::Envelope& reply) {
        if (!done)
            return;
        if (reply.has_error())
            done(false, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
        else
            done(true, {}, {});
    });
}

void Daemon::applyConfig()
{
    const auto before = m_voice->settings();
    const auto after = engineSettings();
    m_voice->applySettings(after);
    if (m_video)
        m_video->setSettings(videoSettings()); // applies to the next share
    // Device changes need the streams reopened.
    if (m_voice->active() && (before.input != after.input || before.output != after.output) && m_voiceChannel) {
        const Id channel = m_voiceChannel;
        leaveVoice(nullptr);
        joinVoice(channel, nullptr);
    }
    scheduleStatus();
}

bool Daemon::saveConfig(QString* error)
{
    return m_config.save(m_options.configPath, error);
}

void Daemon::startAccount(const Account& account, ServerConnection::Credentials creds)
{
    // (Re)starts that account's link and makes it the active one.
    activate(account.id);
    if (m_voice->active())
        stopVoiceEngine();
    m_voiceChannel = 0;
    m_speaking.clear();
    m_store.touchAccount(account.id);
    startLink(m_links.at(account.id), account, std::move(creds));
}

void Daemon::startLink(Link& link, const Account& account, ServerConnection::Credentials creds)
{
    link.e2e->load(account.id);
    link.conn->start(account, std::move(creds));
}

// ------------------------------------------------------------------ status

QJsonObject Daemon::statusJson() const
{
    const auto state = m_conn ? m_conn->state() : ServerConnection::State::NotConfigured;
    const bool connected = state == ServerConnection::State::Connected;
    QJsonObject status{{"version", QString::fromLatin1(kVersion)}, {"connected", connected},
        {"state", ServerConnection::stateName(state)}};
    if (m_conn && !m_conn->errorCode().isEmpty()) {
        QJsonObject err{{"code", m_conn->errorCode()}, {"message", m_conn->errorMessage()}};
        if (!m_conn->certificateFingerprint().isEmpty())
            err.insert(QStringLiteral("fingerprint"), m_conn->certificateFingerprint());
        status.insert(QStringLiteral("error"), err);
    } else {
        status.insert(QStringLiteral("error"), QJsonValue::Null);
    }
    status.insert(QStringLiteral("reconnect_in_ms"), m_conn ? m_conn->reconnectInMs() : 0);

    if (m_conn && m_conn->hasAccount()) {
        const auto& a = m_conn->account();
        status.insert(QStringLiteral("account"),
            QJsonObject{{"id", QString::number(a.id)}, {"host", a.host}, {"port", a.port}, {"username", a.username}});
        status.insert(QStringLiteral("instance"), m_conn->instanceName());
        status.insert(QStringLiteral("max_upload_bytes"), static_cast<double>(m_conn->maxUploadBytes()));
        status.insert(QStringLiteral("capabilities"), QJsonArray::fromStringList(m_conn->capabilities()));
    } else {
        status.insert(QStringLiteral("account"), QJsonValue::Null);
        status.insert(QStringLiteral("instance"), QJsonValue::Null);
        status.insert(QStringLiteral("max_upload_bytes"), 0);
        status.insert(QStringLiteral("capabilities"), QJsonArray());
    }

    const ClientState* model = m_conn ? &m_conn->model() : nullptr;
    if (model && model->valid())
        status.insert(QStringLiteral("user"), model->userJson(model->self()));
    else
        status.insert(QStringLiteral("user"), QJsonValue::Null);

    // "server" is the server of the current voice channel, else the one the
    // GUI last focused.
    Id serverId = 0;
    const proto::Channel* voiceChannel = model && m_voiceChannel ? model->channel(m_voiceChannel) : nullptr;
    if (voiceChannel)
        serverId = voiceChannel->server_id();
    else
        serverId = m_focusedServer;
    const proto::Server* server = model && serverId ? model->server(serverId) : nullptr;
    status.insert(QStringLiteral("server"),
        server
            ? QJsonValue(QJsonObject{{"id", idString(server->id())}, {"name", QString::fromStdString(server->name())}})
            : QJsonValue::Null);

    QJsonArray participants;
    if (model && voiceChannel) {
        for (Id uid : model->voiceParticipants(voiceChannel->id())) {
            const proto::User* u = model->user(uid);
            const proto::VoiceState* v = model->voiceState(uid);
            participants.append(QJsonObject{{"user_id", idString(uid)},
                {"name", u ? QString::fromStdString(u->display_name()) : idString(uid)},
                {"speaking", m_speaking.contains(uid)}, {"muted", v && (v->self_mute() || v->server_mute())},
                {"deafened", v && (v->self_deaf() || v->server_deaf())}, {"streaming", v && v->streaming()}});
        }
    }
    QJsonObject voice{{"joined", voiceChannel != nullptr}, {"pending", m_voicePending},
        {"channel_id", voiceChannel ? idString(voiceChannel->id()) : QString()},
        {"channel", voiceChannel ? QString::fromStdString(voiceChannel->name()) : QString()}, {"muted", m_selfMute},
        {"deafened", m_selfDeaf}, {"mode", config::toString(m_config.audio.mode)},
        {"ptt", m_voice && m_voice->pushToTalk()}, {"transmitting", m_voice && m_voice->transmitting()},
        {"registered", m_voice && m_voice->active() && m_voice->statsJson().value("registered").toBool()},
        {"participants", participants}, {"count", participants.size()}, {"streaming", m_video && m_video->sharing()},
        {"self_preview", m_video ? m_video->selfPreviewPath() : QString()},
        {"watching", m_video ? m_video->watchingJson() : QJsonArray()}};
    status.insert(QStringLiteral("voice"), voice);
    status.insert(QStringLiteral("audio"),
        QJsonObject{{"backend", m_audio ? m_audio->name() : QString()}, {"input", m_config.audio.input},
            {"output", m_config.audio.output}, {"error", m_audioError}});
    status.insert(QStringLiteral("clients"), m_ipc.clientCount());
    status.insert(QStringLiteral("accounts"), accountsJson());
    status.insert(QStringLiteral("e2e"), m_e2e ? m_e2e->statusJson() : QJsonObject());
    return status;
}

// ------------------------------------------------------------------ events

void Daemon::onModelEvent(const QString& name, const QJsonObject& data)
{
    m_ipc.broadcast(name, data);

    if (name == u"message.created") {
        maybeNotify(*m_conn, m_mutedChannels, data);
        return;
    }
    if (name == u"voice.state") {
        const Id user = idFromJson(data.value(QStringLiteral("user_id")));
        const Id channel = idFromJson(data.value(QStringLiteral("channel_id")));
        const Id self = m_conn->model().self().id();
        if (user == self) {
            const Id owner = idFromJson(data.value(QStringLiteral("owner_session_id")));
            if (owner && owner != m_conn->sessionId() && m_voiceChannel) {
                m_voiceChannel = 0;
                stopVoiceEngine();
                m_video->stopSharing();
                m_ipc.broadcast(QStringLiteral("voice.ownership_lost"), {});
            }
            if (channel == 0 && m_voiceChannel && !m_voicePending) {
                // The server removed us (kicked from channel, permission change).
                OMA_INFO("voice", "removed from voice by server");
                m_voiceChannel = 0;
                stopVoiceEngine();
            }
        } else if (channel != m_voiceChannel) {
            m_voice->removeSpeaker(user);
            m_speaking.erase(user);
        }
        // A stream we watch is gone once its sender stops or leaves.
        if (user != self && m_video->watching(user)
            && (channel != m_voiceChannel || !data.value(QStringLiteral("streaming")).toBool())) {
            m_video->unwatch(user);
            m_ipc.broadcast(QStringLiteral("stream.ended"), {{"user_id", idString(user)}});
        }
        // The server can end our share (permission change).
        if (user == self && m_streamConfirmed && m_video->sharing()
            && !data.value(QStringLiteral("streaming")).toBool()) {
            m_streamConfirmed = false;
            m_video->stopSharing();
            m_ipc.broadcast(QStringLiteral("stream.ended"), {{"user_id", idString(user)}});
        }
        if (m_config.notifications.voiceJoin && user != self && channel && channel == m_voiceChannel
            && idFromJson(data.value(QStringLiteral("previous_channel_id"))) != channel) {
            if (const auto* u = m_conn->model().user(user))
                m_notifier.notify(QStringLiteral("%1 joined voice").arg(QString::fromStdString(u->display_name())),
                    QString(), QStringLiteral("im"));
        }
        scheduleStatus();
        return;
    }
    if (name == u"channel.deleted") {
        // Only the deletion of our own voice channel ends the session.
        if (m_voiceChannel && idFromJson(data.value(QStringLiteral("channel_id"))) == m_voiceChannel) {
            OMA_INFO("voice", "voice channel deleted");
            leaveVoice(nullptr);
        }
        scheduleStatus();
        return;
    }
    if (name == u"server.removed") {
        // server.removed fires after the model dropped the server's channels.
        if (m_voiceChannel && !m_conn->model().channel(m_voiceChannel)) {
            OMA_INFO("voice", "left voice: server removed");
            leaveVoice(nullptr);
        }
        scheduleStatus();
        return;
    }
    if (name == u"presence" || name == u"user.updated")
        scheduleStatus();
}

void Daemon::maybeNotify(const ServerConnection& conn, const std::set<std::uint64_t>& muted, const QJsonObject& msg)
{
    if (!m_options.notifications)
        return;
    const bool active = &conn == m_conn;
    const ClientState& model = conn.model();
    const Id author = idFromJson(msg.value(QStringLiteral("author_id")));
    const Id channelId = idFromJson(msg.value(QStringLiteral("channel_id")));
    if (author == model.self().id())
        return;
    if (model.self().status() == proto::USER_STATUS_DO_NOT_DISTURB)
        return;
    if (muted.contains(channelId))
        return;
    if (active && m_windowFocused && m_focusedChannel == channelId)
        return;
    const proto::Channel* channel = model.channel(channelId);
    if (!channel)
        return;
    const bool dm = channel->type() == proto::CHANNEL_TYPE_DM || channel->type() == proto::CHANNEL_TYPE_GROUP_DM;
    const bool mention = msg.value(QStringLiteral("mentions_me")).toBool();
    if (!(dm && m_config.notifications.messages) && !(mention && m_config.notifications.mentions))
        return;
    const proto::User* u = model.user(author);
    const QString who = u ? QString::fromStdString(u->display_name()) : QStringLiteral("Someone");
    QString summary;
    if (dm) {
        summary = who;
    } else {
        const proto::Server* server = model.server(channel->server_id());
        summary = QStringLiteral("%1 in #%2%3")
                      .arg(who, QString::fromStdString(channel->name()),
                          server ? QStringLiteral(" (%1)").arg(QString::fromStdString(server->name())) : QString());
    }
    if (!active) // say which account it came to
        summary += QStringLiteral(" · %1@%2").arg(conn.account().username, conn.account().host);
    m_notifier.notify(summary, msg.value(QStringLiteral("content")).toString(), QStringLiteral("im.received"), mention);
}

// ------------------------------------------------------------------- voice

void Daemon::joinVoice(Id channelId, const Responder* r)
{
    const std::optional<Responder> responder = r ? std::optional<Responder>(*r) : std::nullopt;
    m_voicePending = true;
    m_voiceChannel = channelId;
    scheduleStatus();
    proto::Envelope env;
    auto* j = env.mutable_join_voice();
    j->set_channel_id(channelId);
    j->set_self_mute(m_selfMute);
    j->set_self_deaf(m_selfDeaf);
    m_conn->request(std::move(env), [this, responder, channelId](const proto::Envelope& reply) {
        m_voicePending = false;
        if (reply.has_error()) {
            if (m_voiceChannel == channelId)
                m_voiceChannel = 0;
            scheduleStatus();
            if (responder)
                responder->error(ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
            return;
        }
        onVoiceSession(reply.voice_session());
        if (responder)
            responder->ok(statusJson().value(QStringLiteral("voice")).toObject());
    });
}

void Daemon::onVoiceSession(const proto::VoiceSession& s)
{
    voice::VoiceEngine::Session session;
    session.server = m_conn->serverAddress();
    session.port = static_cast<quint16>(s.udp_port());
    session.streamId = s.stream_id();
    session.selfUserId = m_conn->model().self().id();
    session.bitrate = static_cast<int>(s.bitrate());
    if (s.media_key().size() != session.key.size()) {
        OMA_ERROR("voice", "server sent an invalid media key");
        return;
    }
    std::copy(s.media_key().begin(), s.media_key().end(), session.key.begin());
    m_voiceChannel = s.channel_id();
    m_speaking.clear();
    m_audioError.clear();
    m_voice->setMuted(m_selfMute);
    m_voice->setDeafened(m_selfDeaf);
    QString error;
    if (!m_voice->start(session, &error)) {
        m_audioError = error;
        OMA_ERROR("voice", "cannot start voice engine", {"error", error});
    }
    scheduleStatus();
}

void Daemon::stopVoiceEngine()
{
    // Video rides the voice transport: it stops first.
    m_streamConfirmed = false;
    if (m_video)
        m_video->stopAll();
    if (m_voice)
        m_voice->stop();
}

void Daemon::leaveVoice(const Responder* r)
{
    if (m_voiceChannel)
        OMA_INFO("voice", "leaving voice", {"requested_by_client", r != nullptr});
    const std::optional<Responder> responder = r ? std::optional<Responder>(*r) : std::nullopt;
    m_voiceChannel = 0;
    m_voicePending = false;
    stopVoiceEngine();
    m_speaking.clear();
    scheduleStatus();
    if (m_conn->state() != ServerConnection::State::Connected) {
        if (responder)
            responder->ok();
        return;
    }
    proto::Envelope env;
    env.mutable_leave_voice();
    m_conn->request(std::move(env), [responder](const proto::Envelope&) {
        if (responder)
            responder->ok();
    });
}

void Daemon::setSelfVoiceState(bool mute, bool deafen)
{
    m_selfDeaf = deafen;
    m_selfMute = mute;
    m_voice->setMuted(mute || deafen);
    m_voice->setDeafened(deafen);
    if (m_voiceChannel && m_conn->state() == ServerConnection::State::Connected) {
        proto::Envelope env;
        auto* v = env.mutable_set_voice_state();
        v->set_self_mute(mute || deafen);
        v->set_self_deaf(deafen);
        m_conn->request(std::move(env), {});
    }
    m_ipc.broadcast(QStringLiteral("voice.self"), statusJson().value(QStringLiteral("voice")).toObject());
    scheduleStatus();
}

void Daemon::checkVoiceAfterSync()
{
    if (!m_voiceChannel || m_voicePending)
        return;
    const ClientState& model = m_conn->model();
    const proto::VoiceState* mine = model.voiceState(model.self().id());
    if (mine && mine->owner_session_id() && mine->owner_session_id() != m_conn->sessionId()) {
        m_voiceChannel = 0;
        stopVoiceEngine();
        m_video->stopSharing();
        m_ipc.broadcast(QStringLiteral("voice.ownership_lost"), {});
        return;
    }
    if (mine && mine->channel_id() == m_voiceChannel)
        return;
    // Full resync after an outage lost our voice session: rejoin transparently.
    if (!model.channel(m_voiceChannel)) {
        leaveVoice(nullptr);
        return;
    }
    OMA_INFO("voice", "rejoining voice after resynchronization", {"channel", m_voiceChannel});
    stopVoiceEngine();
    joinVoice(m_voiceChannel, nullptr);
}

} // namespace omachat::daemon
