#include "auth/OAuthProviders.hpp"

#include <QJsonObject>
#include <gtest/gtest.h>

using namespace omachat;

TEST(OAuthProfile, DiscordDisplayNameAndAvatar)
{
    const QJsonObject source{{QStringLiteral("id"), QStringLiteral("123456789")},
        {QStringLiteral("username"), QStringLiteral("alice")},
        {QStringLiteral("global_name"), QStringLiteral("Alice Smith")},
        {QStringLiteral("avatar"), QStringLiteral("a_abcdef123")}};
    const auto profile = server::auth::parseProfile(proto::OAUTH_PROVIDER_DISCORD, source);
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile->displayName, QStringLiteral("Alice Smith"));
    ASSERT_TRUE(profile->avatarUrl);
    EXPECT_EQ(
        *profile->avatarUrl, QStringLiteral("https://cdn.discordapp.com/avatars/123456789/a_abcdef123.png?size=256"));
    EXPECT_FALSE(profile->bio); // Discord's identify response has no bio.
}

TEST(OAuthProfile, DiscordMissingNameAndClearedAvatar)
{
    const QJsonObject source{{QStringLiteral("id"), QStringLiteral("123")},
        {QStringLiteral("username"), QStringLiteral("alice")}, {QStringLiteral("avatar"), QJsonValue::Null}};
    const auto profile = server::auth::parseProfile(proto::OAUTH_PROVIDER_DISCORD, source);
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile->displayName, QStringLiteral("alice"));
    ASSERT_TRUE(profile->avatarUrl);
    EXPECT_TRUE(profile->avatarUrl->isEmpty());
}

TEST(OAuthProfile, GitHubFieldsAndNullableBio)
{
    const QJsonObject source{{QStringLiteral("id"), 42}, {QStringLiteral("login"), QStringLiteral("alice")},
        {QStringLiteral("name"), QStringLiteral("Alice Smith")},
        {QStringLiteral("avatar_url"), QStringLiteral("https://avatars.githubusercontent.com/u/42?v=4")},
        {QStringLiteral("bio"), QStringLiteral("Builder")}};
    const auto profile = server::auth::parseProfile(proto::OAUTH_PROVIDER_GITHUB, source);
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile->id, QStringLiteral("42"));
    EXPECT_EQ(profile->displayName, QStringLiteral("Alice Smith"));
    EXPECT_EQ(*profile->avatarUrl, QStringLiteral("https://avatars.githubusercontent.com/u/42?v=4"));
    EXPECT_EQ(*profile->bio, QStringLiteral("Builder"));

    QJsonObject cleared = source;
    cleared.insert(QStringLiteral("bio"), QJsonValue::Null);
    const auto clearedProfile = server::auth::parseProfile(proto::OAUTH_PROVIDER_GITHUB, cleared);
    ASSERT_TRUE(clearedProfile);
    ASSERT_TRUE(clearedProfile->bio);
    EXPECT_TRUE(clearedProfile->bio->isEmpty());
}

TEST(OAuthProfile, InvalidRemoteFieldsDoNotEnterProfile)
{
    const QJsonObject source{{QStringLiteral("id"), 42}, {QStringLiteral("login"), QStringLiteral("alice")},
        {QStringLiteral("name"), QStringLiteral("bad\nname")},
        {QStringLiteral("avatar_url"), QStringLiteral("http://example.com/avatar")},
        {QStringLiteral("bio"), QString(301, u'x')}};
    const auto profile = server::auth::parseProfile(proto::OAUTH_PROVIDER_GITHUB, source);
    ASSERT_TRUE(profile);
    EXPECT_EQ(profile->displayName, QStringLiteral("alice"));
    EXPECT_FALSE(profile->avatarUrl);
    EXPECT_FALSE(profile->bio);
}
