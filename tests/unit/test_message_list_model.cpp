#include "models/MessageListModel.hpp"

#include <gtest/gtest.h>

using omachat::client::MessageListModel;

TEST(MessageListModel, SearchAnchorShowsMatchAndEarlierMessages)
{
    MessageListModel model;
    struct Pending {
        QString before;
        std::function<void(const QList<QJsonObject>&, bool, const QString&)> done;
    };
    QList<Pending> requests;
    MessageListModel::Hooks hooks;
    hooks.render = [](const QString& content) { return content; };
    hooks.fetchPage
        = [&requests](const QString&, const QString& before, auto done) { requests.append({before, std::move(done)}); };
    model.setHooks(std::move(hooks));

    model.setChannel(QStringLiteral("7"));
    ASSERT_EQ(requests.size(), 1);
    EXPECT_TRUE(requests[0].before.isEmpty());

    const QJsonObject match{{QStringLiteral("id"), QStringLiteral("42")},
        {QStringLiteral("channel_id"), QStringLiteral("7")}, {QStringLiteral("content"), QStringLiteral("match")},
        {QStringLiteral("timestamp"), 2000.0}};
    model.openAt(QStringLiteral("7"), match);
    ASSERT_EQ(requests.size(), 2);
    EXPECT_EQ(requests[1].before, QStringLiteral("42"));
    EXPECT_EQ(model.anchorMessageId(), QStringLiteral("42"));
    ASSERT_EQ(model.count(), 1);
    EXPECT_EQ(model.rowOf(QStringLiteral("42")), 0);

    // A late response from the latest-page request must not replace the match.
    requests[0].done({QJsonObject{{QStringLiteral("id"), QStringLiteral("99")}}}, false, {});
    EXPECT_EQ(model.count(), 1);
    requests[1].done(
        {QJsonObject{{QStringLiteral("id"), QStringLiteral("41")}, {QStringLiteral("channel_id"), QStringLiteral("7")},
            {QStringLiteral("content"), QStringLiteral("earlier")}, {QStringLiteral("timestamp"), 1000.0}}},
        false, {});
    EXPECT_EQ(model.count(), 2);
    EXPECT_EQ(model.rowOf(QStringLiteral("41")), 1);

    model.addMessage(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("43")}, {QStringLiteral("channel_id"), QStringLiteral("7")}});
    EXPECT_EQ(model.count(), 2);

    model.reload();
    EXPECT_TRUE(model.anchorMessageId().isEmpty());
    ASSERT_EQ(requests.size(), 3);
    EXPECT_TRUE(requests[2].before.isEmpty());

    model.openAt(QStringLiteral("7"), match);
    ASSERT_EQ(requests.size(), 4);
    requests[3].done({}, false, QStringLiteral("offline"));
    EXPECT_EQ(model.error(), QStringLiteral("offline"));
    model.retry();
    ASSERT_EQ(requests.size(), 5);
    EXPECT_EQ(requests[4].before, QStringLiteral("42"));
    EXPECT_TRUE(model.error().isEmpty());

    model.removeMessage(QStringLiteral("42"));
    EXPECT_TRUE(model.anchorMessageId().isEmpty());
    ASSERT_EQ(requests.size(), 6);
    EXPECT_TRUE(requests[5].before.isEmpty());
}
