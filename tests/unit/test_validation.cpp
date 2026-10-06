#include "omachat/core/Validation.hpp"

#include <gtest/gtest.h>

using namespace omachat::validation;

TEST(Validation, Usernames)
{
    EXPECT_EQ(username(QStringLiteral("  Alice ")).value(), QStringLiteral("alice"));
    EXPECT_TRUE(username(QStringLiteral("bob.smith-2_x")));
    EXPECT_FALSE(username(QStringLiteral("a")));
    EXPECT_FALSE(username(QStringLiteral("_bob")));
    EXPECT_FALSE(username(QStringLiteral("bob smith")));
    EXPECT_FALSE(username(QStringLiteral("bøb")));
    EXPECT_FALSE(username(QString(33, u'a')));
}

TEST(Validation, MessageContent)
{
    EXPECT_FALSE(messageContent(QStringLiteral("   \n  ")));
    EXPECT_EQ(messageContent(QStringLiteral("hi\r\nthere  \n")).value(), QStringLiteral("hi\nthere"));
    EXPECT_EQ(messageContent(QStringLiteral("  indented code")).value(), QStringLiteral("  indented code"));
    EXPECT_EQ(messageContent(QString::fromUtf8("a\x01"
                                               "b"))
                  .value(),
        QStringLiteral("ab"));
    EXPECT_FALSE(messageContent(QString(kMaxMessageLength + 1, u'x')));
    EXPECT_TRUE(messageContent(QString(kMaxMessageLength, u'x')));
}

TEST(Validation, LabelsRejectControlCharacters)
{
    EXPECT_FALSE(serverName(QStringLiteral("bad\nname")));
    EXPECT_FALSE(displayName(QString::fromUtf8("zero​width"))); // Cf format char
    EXPECT_TRUE(channelName(QStringLiteral("general")));
    EXPECT_FALSE(channelName(QString()));
}

TEST(Validation, ChannelDescriptionAllowsUsefulTextButNotControlCodes)
{
    EXPECT_EQ(
        channelDescription(QStringLiteral("  Purpose\n- Rule one  ")).value(), QStringLiteral("Purpose\n- Rule one"));
    EXPECT_TRUE(channelDescription(QString()));
    EXPECT_FALSE(channelDescription(QString(kMaxChannelDescriptionLength + 1, u'x')));
    EXPECT_FALSE(channelDescription(QString::fromUtf8("bad\x01text")));
}

TEST(Validation, Passwords)
{
    EXPECT_FALSE(passwordAcceptable(QStringLiteral("short")));
    EXPECT_TRUE(passwordAcceptable(QStringLiteral("long enough")));
    EXPECT_FALSE(passwordAcceptable(QString(2000, u'x')));
}

TEST(Validation, Reactions)
{
    EXPECT_TRUE(reaction(QString::fromUtf8("👍")));
    EXPECT_FALSE(reaction(QStringLiteral("two words")));
    EXPECT_FALSE(reaction(QString(40, u'x')));
}
