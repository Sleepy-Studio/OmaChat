#pragma once

#include "networking/ServerConnection.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace omachat::daemon {

// Drives one "log in with Discord/GitHub/Google" attempt end to end: waits
// for the control connection to reach the login prompt (so the server's
// advertised provider list is known), runs a PKCE authorization-code flow
// through the user's system browser via a one-shot loopback HTTP listener on
// 127.0.0.1, then hands the code to the server over the existing connection.
//
// Self-deleting: owns itself for the duration of the attempt and calls
// `done` exactly once before destruction.
// The same flow also drives "attach a provider to my existing account": the
// only differences are which state it waits for (already Connected, not
// LoginRequired) and which request the resulting code goes into
// (OAuthLinkRequest over the plain request/reply channel, rather than the
// credentials path that authenticates the connection itself).
class OAuthLoginFlow : public QObject {
    Q_OBJECT
public:
    enum class Mode { Login, Link };
    using Done = std::function<void(bool ok, const QString& code, const QString& message)>;

    // `conn` must outlive the attempt; the caller normally starts the
    // connection immediately before calling this.
    static void start(ServerConnection& conn, proto::OAuthProvider provider, Mode mode, Done done);

private:
    OAuthLoginFlow(ServerConnection& conn, proto::OAuthProvider provider, Mode mode, Done done);
    void awaitLoginPrompt();
    void beginBrowserFlow();
    void onLoopbackConnection();
    void handleCallback(const QString& target);
    void exchangeCode(const QString& code);
    void finish(bool ok, const QString& code, const QString& message);

    ServerConnection& m_conn;
    proto::OAuthProvider m_provider;
    Mode m_mode;
    Done m_done;
    QTcpServer* m_server = nullptr;
    QString m_redirectUri;
    QString m_codeVerifier;
    QString m_state;
    QTimer m_timeout;
    bool m_finished = false;
};

} // namespace omachat::daemon
