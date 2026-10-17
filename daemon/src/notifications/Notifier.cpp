#include "notifications/Notifier.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace omachat::daemon {

namespace {
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

// freedesktop says a caller that passes replace_id != 0 has its previous
// notification closed and replaced. 0 always means "new notification".
constexpr quint32 kNoReplace = 0;
} // namespace

Notifier::Notifier(QObject* parent)
    : QObject(parent)
{
    // ActionInvoked(id, action_key) is how a click comes back. Subscribed in
    // the constructor: the notification server may not be running yet, but a
    // missed subscription would silently drop every click forever.
    if (!QDBusConnection::sessionBus().isConnected())
        return;
    QDBusConnection::sessionBus().connect(kService, kPath, kService,
        SIGNAL(ActionInvoked(quint32, QString)), this, SLOT(onActionInvoked(quint32, QString)));
}

bool Notifier::available() const
{
    auto bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface() && bus.interface()->isServiceRegistered(kService);
}

void Notifier::notify(const QString& summary, const QString& body, const QString& category, bool urgent,
    const QString& actionKey)
{
    send(summary, body, category, urgent, actionKey, kNoReplace);
}

void Notifier::notifyReplacing(const QString& summary, const QString& body, const QString& category, bool urgent,
    const QString& actionKey, quint32 replaceId)
{
    send(summary, body, category, urgent, actionKey, replaceId);
}

void Notifier::send(const QString& summary, const QString& body, const QString& category, bool urgent,
    const QString& actionKey, quint32 replaceId)
{
    if (!QDBusConnection::sessionBus().isConnected())
        return;
    // A quiet, local cue for new chat messages. Keep it out of the IPC
    // contract: the daemon plays it directly through the user's audio session.
    if (category == QStringLiteral("im.received")) {
        const QString player = QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play"));
        if (!player.isEmpty()) {
            QProcess::startDetached(player,
                {QStringLiteral("--id=message-new-instant"), QStringLiteral("--volume=-18.0")});
        }
    }
    // Built by the same helper the tests assert against, so the wire shape has
    // one definition.
    QDBusConnection::sessionBus().asyncCall(buildNotifyMessage(summary, body, category, urgent, actionKey, replaceId));
}

QDBusMessage Notifier::buildNotifyMessage(const QString& summary, const QString& body, const QString& category,
    bool urgent, const QString& actionKey, quint32 replaceId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("Notify"));
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QString::fromLatin1(kAppId));
    hints.insert(QStringLiteral("category"), category);
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(urgent ? 2 : 1));
    // Body markup is not requested, so the text is shown literally.
    QString plain = body;
    if (plain.size() > 240)
        plain = plain.left(239) + QChar(0x2026);

    // An action with an empty key is what makes the notification body
    // clickable: the server shows a "default" entry and invokes it when the
    // user clicks the body rather than a button.
    QStringList actions;
    if (!actionKey.isEmpty())
        actions << QStringLiteral("default");

    // Notify's signature ends with expire_timeout; -1 = server default.
    msg << QStringLiteral("omachat") << replaceId << QString::fromLatin1(kAppId) << summary << plain << actions
        << hints << qint32(-1);
    return msg;
}

void Notifier::onActionInvoked(quint32 id, const QString& actionKey)
{
    OMA_INFO("notify", "notification action invoked", {"id", id}, {"action", actionKey});
    emit actionActivated(actionKey);
}

} // namespace omachat::daemon
