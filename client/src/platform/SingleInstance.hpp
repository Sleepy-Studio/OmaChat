#pragma once

#include <QLocalServer>
#include <QObject>
#include <QStringList>

namespace omachat::client {

// Keeps one GUI per user session. A second launch (e.g. from the app
// launcher, or an omachat:// link handed over by the desktop) forwards its
// arguments to the running window and exits.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);

    // Returns true if another instance received `args` (caller should exit).
    bool forwardToRunning(const QStringList& args);
    bool listen();

signals:
    void activated(const QStringList& args);

private:
    QString m_path;
    QLocalServer m_server;
};

} // namespace omachat::client
