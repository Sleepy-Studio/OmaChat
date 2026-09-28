#pragma once

#include <QObject>
#include <QString>

namespace omachat::daemon {

// Desktop notifications through org.freedesktop.Notifications (asynchronous
// D-Bus calls; the daemon never blocks on the notification server).
class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject* parent = nullptr);

    bool available() const;
    void notify(const QString& summary, const QString& body, const QString& category, bool urgent = false);
};

} // namespace omachat::daemon
