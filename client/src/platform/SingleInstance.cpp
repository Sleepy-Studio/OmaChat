#include "platform/SingleInstance.hpp"

#include "omachat/core/Paths.hpp"

#include <QLocalSocket>

namespace omachat::client {

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
    , m_path(paths::runtimeDir() + QStringLiteral("/gui.sock"))
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* s = m_server.nextPendingConnection()) {
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                if (!s->canReadLine())
                    return;
                const QString line = QString::fromUtf8(s->readLine(4096)).trimmed();
                emit activated(line.isEmpty() ? QStringList() : line.split(u'\t'));
                s->disconnectFromServer();
            });
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });
}

bool SingleInstance::forwardToRunning(const QStringList& args)
{
    QLocalSocket s;
    s.connectToServer(m_path);
    if (!s.waitForConnected(250))
        return false;
    s.write(args.join(u'\t').toUtf8() + '\n');
    s.flush();
    s.waitForBytesWritten(250);
    return true;
}

bool SingleInstance::listen()
{
    paths::ensurePrivateDir(paths::runtimeDir());
    QLocalServer::removeServer(m_path); // stale socket; we verified nobody answers
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    return m_server.listen(m_path);
}

} // namespace omachat::client
