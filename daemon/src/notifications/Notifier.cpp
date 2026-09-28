#include "notifications/Notifier.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariantMap>

namespace omachat::daemon {

namespace {
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
} // namespace

Notifier::Notifier(QObject* parent)
    : QObject(parent)
{
}

bool Notifier::available() const
{
    auto bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface() && bus.interface()->isServiceRegistered(kService);
}

void Notifier::notify(const QString& summary, const QString& body, const QString& category, bool urgent)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("Notify"));
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QString::fromLatin1(kAppId));
    hints.insert(QStringLiteral("category"), category);
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(urgent ? 2 : 1));
    // Body markup is not requested, so the text is shown literally.
    QString plain = body;
    if (plain.size() > 240)
        plain = plain.left(239) + QChar(0x2026);
    msg << QStringLiteral("OmaChat") << quint32(0) << QString::fromLatin1(kAppId) << summary << plain << QStringList()
        << hints << qint32(-1);
    bus.asyncCall(msg);
}

} // namespace omachat::daemon
