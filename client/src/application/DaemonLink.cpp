#include "application/DaemonLink.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QProcess>

namespace omachat::client {

DaemonLink::DaemonLink(bool mayLaunch, QObject* parent)
    : QObject(parent)
    , m_socket(paths::socketPath())
    , m_mayLaunch(mayLaunch)
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { m_client.connectToDaemon(m_socket); });
    connect(&m_client, &ipc::IpcClient::connected, this, [this] {
        m_attempts = 0;
        setState(State::Connected);
        emit connected();
    });
    connect(&m_client, &ipc::IpcClient::connectionFailed, this, &DaemonLink::onConnectFailed);
    connect(&m_client, &ipc::IpcClient::disconnected, this, [this] {
        // The daemon restarted (service restart, upgrade). Reconnect; the
        // retry timer only exists while we are disconnected.
        OMA_WARN("gui", "lost connection to omachatd");
        m_attempts = 0;
        setState(State::Reconnecting, QStringLiteral("Lost connection to omachatd"));
        m_retry.start(300);
    });
    connect(&m_client, &ipc::IpcClient::eventReceived, this, &DaemonLink::eventReceived);
}

QString DaemonLink::stateName() const
{
    switch (m_state) {
    case State::Starting:
        return QStringLiteral("starting");
    case State::Launching:
        return QStringLiteral("launching");
    case State::Connected:
        return QStringLiteral("connected");
    case State::Reconnecting:
        return QStringLiteral("reconnecting");
    case State::Unavailable:
        return QStringLiteral("unavailable");
    }
    return QStringLiteral("starting");
}

void DaemonLink::setState(State s, const QString& error)
{
    if (s == m_state && error == m_error)
        return;
    m_state = s;
    m_error = error;
    emit stateChanged();
}

void DaemonLink::start()
{
    setState(State::Starting);
    m_client.connectToDaemon(m_socket);
}

void DaemonLink::retry()
{
    m_launched = false;
    m_attempts = 0;
    start();
}

void DaemonLink::request(const QString& method, const QJsonObject& params, ipc::IpcClient::Callback cb, int timeoutMs)
{
    m_client.request(method, params, std::move(cb), timeoutMs);
}

void DaemonLink::onConnectFailed()
{
    ++m_attempts;
    if (!m_launched && m_mayLaunch && m_state != State::Reconnecting) {
        launch();
        return;
    }
    // Give a freshly started daemon a few seconds to create its socket.
    const int limit = m_state == State::Reconnecting ? 40 : 24;
    if (m_attempts < limit) {
        m_retry.start(m_attempts < 8 ? 250 : 500);
        return;
    }
    setState(State::Unavailable,
        m_mayLaunch ? QStringLiteral("omachatd could not be started. Check `systemctl --user status omachat.service` "
                                     "and `journalctl --user -u omachat.service`.")
                    : QStringLiteral("omachatd is not running and automatic launch is disabled "
                                     "(startup.launch_daemon = false)."));
}

void DaemonLink::launch()
{
    m_launched = true;
    setState(State::Launching);
    OMA_INFO("gui", "starting omachat.service via systemd --user");
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
        QStringLiteral("/org/freedesktop/systemd1"), QStringLiteral("org.freedesktop.systemd1.Manager"),
        QStringLiteral("StartUnit"));
    call << QStringLiteral("omachat.service") << QStringLiteral("replace");
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            OMA_INFO("gui", "systemd unit unavailable, launching omachatd directly", {"reason", w->error().message()});
            if (!launchBinary()) {
                setState(State::Unavailable,
                    QStringLiteral("omachatd is not installed as a user service and no omachatd binary was found."));
                return;
            }
        }
        m_retry.start(150);
    });
}

bool DaemonLink::launchBinary()
{
    const QString dir = QCoreApplication::applicationDirPath();
    // Installed layout: omachat and omachatd share a directory. Build tree:
    // build/client/omachat and build/daemon/omachatd.
    for (const QString& candidate : {dir + QStringLiteral("/omachatd"), dir + QStringLiteral("/../daemon/omachatd")}) {
        if (QFileInfo(candidate).isExecutable())
            return QProcess::startDetached(QFileInfo(candidate).canonicalFilePath(), {});
    }
    return false;
}

} // namespace omachat::client
