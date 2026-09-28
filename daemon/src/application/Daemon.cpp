#include "application/Daemon.hpp"

#include "audio/PipeWireBackend.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Version.hpp"

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

    m_conn = std::make_unique<ServerConnection>(*m_credentials, this);
    connect(m_conn.get(), &ServerConnection::stateChanged, this, [this] {
        m_ipc.broadcast(QStringLiteral("connection"), statusJson());
        scheduleStatus();
        if (m_conn->state() == ServerConnection::State::Disconnected
            || m_conn->state() == ServerConnection::State::LoginRequired) {
            if (m_voice->active())
                m_voice->stop();
        }
    });
    connect(m_conn.get(), &ServerConnection::modelEvent, this, &Daemon::onModelEvent);
    m_transfers = std::make_unique<FileTransfers>(*m_conn, this);
    connect(m_transfers.get(), &FileTransfers::progress, this,
        [this](const QJsonObject& data) { m_ipc.broadcast(QStringLiteral("transfer.progress"), data); });
    connect(m_conn.get(), &ServerConnection::synchronized, this, [this] {
        m_mutedChannels = m_store.mutedChannels(m_conn->account().id);
        for (const auto& [uid, gain] : m_store.userVolumes(m_conn->account().id))
            m_voice->setUserGain(uid, gain);
        m_ipc.broadcast(QStringLiteral("state.reset"), {});
        checkVoiceAfterSync();
        scheduleStatus();
    });

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

    const auto accounts = m_store.accounts();
    if (!accounts.empty())
        startAccount(accounts.front());
    return true;
}

void Daemon::shutdown()
{
    if (m_voice)
        m_voice->stop();
    if (m_conn)
        m_conn->stop();
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

void Daemon::applyConfig()
{
    const auto before = m_voice->settings();
    const auto after = engineSettings();
    m_voice->applySettings(after);
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
    if (m_voice->active())
        m_voice->stop();
    m_voiceChannel = 0;
    m_speaking.clear();
    m_store.touchAccount(account.id);
    m_conn->start(account, std::move(creds));
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
    } else {
        status.insert(QStringLiteral("account"), QJsonValue::Null);
        status.insert(QStringLiteral("instance"), QJsonValue::Null);
        status.insert(QStringLiteral("max_upload_bytes"), 0);
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
                {"deafened", v && (v->self_deaf() || v->server_deaf())}});
        }
    }
    QJsonObject voice{{"joined", voiceChannel != nullptr}, {"pending", m_voicePending},
        {"channel_id", voiceChannel ? idString(voiceChannel->id()) : QString()},
        {"channel", voiceChannel ? QString::fromStdString(voiceChannel->name()) : QString()}, {"muted", m_selfMute},
        {"deafened", m_selfDeaf}, {"mode", config::toString(m_config.audio.mode)},
        {"ptt", m_voice && m_voice->pushToTalk()}, {"transmitting", m_voice && m_voice->transmitting()},
        {"registered", m_voice && m_voice->active() && m_voice->statsJson().value("registered").toBool()},
        {"participants", participants}, {"count", participants.size()}};
    status.insert(QStringLiteral("voice"), voice);
    status.insert(QStringLiteral("audio"),
        QJsonObject{{"backend", m_audio ? m_audio->name() : QString()}, {"input", m_config.audio.input},
            {"output", m_config.audio.output}, {"error", m_audioError}});
    status.insert(QStringLiteral("clients"), m_ipc.clientCount());
    return status;
}

// ------------------------------------------------------------------ events

void Daemon::onModelEvent(const QString& name, const QJsonObject& data)
{
    m_ipc.broadcast(name, data);

    if (name == u"message.created") {
        maybeNotify(data);
        return;
    }
    if (name == u"voice.state") {
        const Id user = idFromJson(data.value(QStringLiteral("user_id")));
        const Id channel = idFromJson(data.value(QStringLiteral("channel_id")));
        const Id self = m_conn->model().self().id();
        if (user == self) {
            if (channel == 0 && m_voiceChannel && !m_voicePending) {
                // The server removed us (kicked from channel, permission change).
                OMA_INFO("voice", "removed from voice by server");
                m_voiceChannel = 0;
                m_voice->stop();
            }
        } else if (channel != m_voiceChannel) {
            m_voice->removeSpeaker(user);
            m_speaking.erase(user);
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

void Daemon::maybeNotify(const QJsonObject& msg)
{
    if (!m_options.notifications)
        return;
    const ClientState& model = m_conn->model();
    const Id author = idFromJson(msg.value(QStringLiteral("author_id")));
    const Id channelId = idFromJson(msg.value(QStringLiteral("channel_id")));
    if (author == model.self().id())
        return;
    if (model.self().status() == proto::USER_STATUS_DO_NOT_DISTURB)
        return;
    if (m_mutedChannels.contains(channelId))
        return;
    if (m_windowFocused && m_focusedChannel == channelId)
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

void Daemon::leaveVoice(const Responder* r)
{
    if (m_voiceChannel)
        OMA_INFO("voice", "leaving voice", {"requested_by_client", r != nullptr});
    const std::optional<Responder> responder = r ? std::optional<Responder>(*r) : std::nullopt;
    m_voiceChannel = 0;
    m_voicePending = false;
    m_voice->stop();
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
    if (mine && mine->channel_id() == m_voiceChannel)
        return;
    // Full resync after an outage lost our voice session: rejoin transparently.
    if (!model.channel(m_voiceChannel)) {
        leaveVoice(nullptr);
        return;
    }
    OMA_INFO("voice", "rejoining voice after resynchronization", {"channel", m_voiceChannel});
    m_voice->stop();
    joinVoice(m_voiceChannel, nullptr);
}

} // namespace omachat::daemon
