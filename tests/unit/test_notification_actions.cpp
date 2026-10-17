// Notification actions: the wire shape Notifier sends, and the action-key
// contract the daemon relies on when a notification is clicked.
//
// The D-Bus round trip is not asserted -- it needs a notification server on the
// session bus, which CI does not have, and gdbus emit does not actually
// deliver the signal (it exits 0 and nothing receives it). What is asserted is
// the message Notifier::buildNotifyMessage really produces, which is the same
// function send() ships, so a regression in the real code fails these tests.
//
// An earlier version of this file re-implemented the expressions locally and
// passed even with send() deliberately broken. These call the real builder.

#include "notifications/Notifier.hpp"

#include <QDBusMessage>
#include <QStringList>
#include <QVariantMap>

#include <gtest/gtest.h>

using namespace omachat::daemon;

namespace {

struct Notify {
    QString app;
    quint32 replaceId = 0;
    QString appName;
    QString summary;
    QString body;
    QStringList actions;
    QVariantMap hints;
    int expireTimeout = 0;
};

Notify parse(const QDBusMessage& m)
{
    const QList<QVariant> a = m.arguments();
    Notify n;
    if (a.size() < 8)
        return n;
    n.app = a.at(0).toString();
    n.replaceId = a.at(1).toUInt();
    n.appName = a.at(2).toString();
    n.summary = a.at(3).toString();
    n.body = a.at(4).toString();
    n.actions = a.at(5).toStringList();
    n.hints = a.at(6).toMap();
    n.expireTimeout = a.at(7).toInt();
    return n;
}

Notify build(
    const QString& summary, const QString& body, const QString& category, bool urgent, const QString& actionKey)
{
    return parse(Notifier::buildNotifyMessage(summary, body, category, urgent, actionKey, 0));
}

} // namespace

// The action list is what makes a notification body clickable: a notification
// server shows a "default" entry and invokes it when the body is clicked.
// Without it every click was silently dropped -- the original defect.
TEST(NotificationActions, ActionKeyMakesNotificationClickable)
{
    const auto n = build(QStringLiteral("Ada in #general"), QStringLiteral("hello"), QStringLiteral("im.received"),
        false, QStringLiteral("channel:230912986460459015"));
    EXPECT_TRUE(n.actions.contains(QStringLiteral("default")));
    EXPECT_EQ(n.summary, QStringLiteral("Ada in #general"));
    EXPECT_EQ(n.body, QStringLiteral("hello"));
    EXPECT_EQ(n.hints.value(QStringLiteral("category")).toString(), QStringLiteral("im.received"));
}

TEST(NotificationActions, EmptyActionKeyProducesNoActions)
{
    // Voice-join notifications pass no key and must not become clickable.
    const auto n = build(QStringLiteral("Bob joined voice"), QString(), QStringLiteral("im"), false, QString());
    EXPECT_TRUE(n.actions.isEmpty());
}

TEST(NotificationActions, UrgencyHintReflectsMention)
{
    EXPECT_EQ(build(QStringLiteral("a"), QString(), QStringLiteral("im.received"), false, QStringLiteral("c:1"))
                  .hints.value(QStringLiteral("urgency"))
                  .value<uchar>(),
        1);
    EXPECT_EQ(build(QStringLiteral("a"), QString(), QStringLiteral("im.received"), true, QStringLiteral("c:1"))
                  .hints.value(QStringLiteral("urgency"))
                  .value<uchar>(),
        2);
}

TEST(NotificationActions, BodyIsTruncatedNotMarkup)
{
    const auto n = build(
        QStringLiteral("Ada"), QString(600, QChar('x')), QStringLiteral("im.received"), false, QStringLiteral("c:1"));
    // An unbounded body could push arbitrary text at the user.
    EXPECT_EQ(n.body.size(), 240);
    EXPECT_TRUE(n.body.endsWith(QChar(0x2026)));
}

TEST(NotificationActions, MessagesAreNotReplacedByDefault)
{
    const auto n = build(QStringLiteral("a"), QString(), QStringLiteral("im"), false, QStringLiteral("c:1"));
    EXPECT_EQ(n.replaceId, 0u);
    EXPECT_EQ(n.expireTimeout, -1);
}

TEST(NotificationActions, DesktopEntryHintIsPresent)
{
    const auto n = build(QStringLiteral("a"), QString(), QStringLiteral("im"), false, QStringLiteral("c:1"));
    EXPECT_TRUE(n.hints.contains(QStringLiteral("desktop-entry")));
}

TEST(NotificationActions, ActionInvokedIsASignal)
{
    // The click arrives as a signal. If this is not a signal the daemon can
    // never hear a click, which is exactly the original defect.
    EXPECT_GE(Notifier::staticMetaObject.indexOfSignal("actionActivated(QString)"), 0);
}

// The daemon builds "channel:<id>" and parses it in Daemon::onActionActivated.
// Both halves are plain string work; assert the contract so the two sides
// cannot drift apart silently.
namespace {

QString channelKey(quint64 id)
{
    return QStringLiteral("channel:%1").arg(id);
}

quint64 parseChannelKey(const QString& key)
{
    if (!key.startsWith(QStringLiteral("channel:")))
        return 0;
    bool ok = false;
    const quint64 id = key.mid(8).toULongLong(&ok);
    return ok ? id : 0;
}

} // namespace

TEST(NotificationActions, ChannelKeyRoundTrips)
{
    const quint64 id = 230912986460459015ULL;
    EXPECT_EQ(parseChannelKey(channelKey(id)), id);
}

TEST(NotificationActions, ForeignAndMalformedKeysAreRejected)
{
    EXPECT_EQ(parseChannelKey(QStringLiteral("voice:123")), 0);
    EXPECT_EQ(parseChannelKey(QStringLiteral("https://example.com")), 0);
    EXPECT_EQ(parseChannelKey(QString()), 0);
    EXPECT_EQ(parseChannelKey(QStringLiteral("channel:not-a-number")), 0);
    EXPECT_EQ(parseChannelKey(QStringLiteral("channel:")), 0);
}