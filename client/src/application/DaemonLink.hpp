#pragma once

#include "omachat/ipc/IpcClient.hpp"

#include <QObject>
#include <QTimer>

namespace omachat::client {

// The GUI's connection to omachatd.
//
// Startup: connect to the socket; if nobody answers, ask systemd --user to
// start omachat.service (D-Bus, no shell), falling back to launching the
// omachatd binary next to this executable for development builds. If the
// daemon still cannot be reached the GUI shows an explicit error.
class DaemonLink : public QObject {
    Q_OBJECT
public:
    enum class State { Starting, Launching, Connected, Reconnecting, Unavailable };
    Q_ENUM(State)

    DaemonLink(bool mayLaunch, QObject* parent = nullptr);

    void start();
    void retry();
    State state() const { return m_state; }
    QString stateName() const;
    QString error() const { return m_error; }
    ipc::IpcClient& client() { return m_client; }

    void request(
        const QString& method, const QJsonObject& params = {}, ipc::IpcClient::Callback cb = {}, int timeoutMs = 20000);

signals:
    void stateChanged();
    void connected();
    void eventReceived(const QString& name, const QJsonObject& data);

private:
    void setState(State s, const QString& error = {});
    void onConnectFailed();
    void launch();
    bool launchBinary();

    ipc::IpcClient m_client;
    QString m_socket;
    bool m_mayLaunch;
    bool m_launched = false;
    int m_attempts = 0;
    QTimer m_retry;
    State m_state = State::Starting;
    QString m_error;
};

} // namespace omachat::client
