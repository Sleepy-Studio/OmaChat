#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <QtGlobal>

namespace omachat::daemon {

// Desktop notifications through org.freedesktop.Notifications (asynchronous
// D-Bus calls; the daemon never blocks on the notification server).
//
// Notifications carry an action list and a default action, so clicking one can
// do something. The daemon does not interpret actions itself: it emits
// actionActivated(actionKey) and the owner decides, which keeps this class free
// of any knowledge of channels, clients or the GUI.
class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject* parent = nullptr);

    bool available() const;

    // Builds the org.freedesktop.Notifications.Notify call without sending it.
    // Exposed so the wire shape can be asserted in a test: the D-Bus round trip
    // needs a notification server on the session bus, which CI does not have.
    static QDBusMessage buildNotifyMessage(const QString& summary, const QString& body, const QString& category,
        bool urgent, const QString& actionKey, quint32 replaceId);

    // `actionKey` becomes the notification's action id and default action, so a
    // click comes back as actionActivated(actionKey). Pass an empty key for a
    // notification with no click behaviour.
    void notify(const QString& summary, const QString& body, const QString& category, bool urgent = false,
        const QString& actionKey = {});

    // Replaces any existing notification. Notification servers key on the id
    // returned by Notify, so reusing one keeps repeated events (typing, joins)
    // from stacking up.
    void notifyReplacing(const QString& summary, const QString& body, const QString& category, bool urgent,
        const QString& actionKey, quint32 replaceId);

signals:
    // The user clicked the notification body (default action) or one of its
    // buttons. `actionKey` is what was passed to notify().
    void actionActivated(const QString& actionKey);

private slots:
    // Slot rather than a lambda: QDBusConnection::connect's functor overload
    // is ambiguous against the const member overloads here, and the string
    // based overload is checked at runtime. This form is checked at compile
    // time.
    void onActionInvoked(quint32 id, const QString& actionKey);

private:
    void send(const QString& summary, const QString& body, const QString& category, bool urgent,
        const QString& actionKey, quint32 replaceId);
};

} // namespace omachat::daemon