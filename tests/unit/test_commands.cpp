#include "text/CommandParser.hpp"

#include <gtest/gtest.h>

using omachat::client::CommandParser;

TEST(Commands, ParsesNameArgumentAndRest)
{
    const auto c = CommandParser::parse(QStringLiteral("/MSG bob hello there"));
    ASSERT_TRUE(c);
    EXPECT_EQ(c->name, QStringLiteral("msg"));
    EXPECT_EQ(c->argument, QStringLiteral("bob"));
    EXPECT_EQ(c->rest, QStringLiteral("bob hello there"));
    EXPECT_EQ(c->restAfterArgument, QStringLiteral("hello there"));
}

TEST(Commands, PlainTextAndEscapesAreMessages)
{
    EXPECT_FALSE(CommandParser::parse(QStringLiteral("hello")));
    EXPECT_FALSE(CommandParser::parse(QStringLiteral("//not a command")));
    EXPECT_EQ(CommandParser::unescape(QStringLiteral("//path/to")), QStringLiteral("/path/to"));
    EXPECT_FALSE(CommandParser::parse(QStringLiteral("/")));
}

TEST(Commands, KnownCommandsCoverTheSpec)
{
    for (const char* name : {"join", "leave", "msg", "reply", "mute", "unmute", "deafen", "undeafen", "topic", "invite",
             "kick", "ban", "me", "help"})
        EXPECT_TRUE(CommandParser::isKnown(QString::fromLatin1(name))) << name;
    EXPECT_FALSE(CommandParser::isKnown(QStringLiteral("exec")));
}
