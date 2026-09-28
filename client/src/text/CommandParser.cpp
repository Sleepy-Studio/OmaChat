#include "text/CommandParser.hpp"

namespace omachat::client {

std::optional<Command> CommandParser::parse(const QString& text)
{
    const QString t = text.trimmed();
    if (!t.startsWith(u'/') || t.startsWith(QStringLiteral("//")) || t.size() < 2)
        return std::nullopt;
    Command c;
    const qsizetype space = t.indexOf(u' ');
    c.name = (space < 0 ? t.mid(1) : t.mid(1, space - 1)).toLower();
    c.rest = space < 0 ? QString() : t.mid(space + 1).trimmed();
    const qsizetype space2 = c.rest.indexOf(u' ');
    c.argument = space2 < 0 ? c.rest : c.rest.left(space2);
    c.restAfterArgument = space2 < 0 ? QString() : c.rest.mid(space2 + 1).trimmed();
    return c;
}

QString CommandParser::unescape(const QString& text)
{
    return text.trimmed().startsWith(QStringLiteral("//")) ? text.trimmed().mid(1) : text;
}

const QList<CommandInfo>& CommandParser::commands()
{
    static const QList<CommandInfo> list{
        {QStringLiteral("join"), QStringLiteral("/join <channel>"),
            QStringLiteral("Open a text channel or join a voice channel")},
        {QStringLiteral("leave"), QStringLiteral("/leave"), QStringLiteral("Leave the current voice channel")},
        {QStringLiteral("msg"), QStringLiteral("/msg <user> <text>"), QStringLiteral("Send a direct message")},
        {QStringLiteral("reply"), QStringLiteral("/reply <text>"),
            QStringLiteral("Reply to the latest message from someone else")},
        {QStringLiteral("me"), QStringLiteral("/me <action>"), QStringLiteral("Send an action message")},
        {QStringLiteral("mute"), QStringLiteral("/mute"), QStringLiteral("Mute your microphone")},
        {QStringLiteral("unmute"), QStringLiteral("/unmute"), QStringLiteral("Unmute your microphone")},
        {QStringLiteral("deafen"), QStringLiteral("/deafen"),
            QStringLiteral("Stop hearing voice and stop transmitting")},
        {QStringLiteral("undeafen"), QStringLiteral("/undeafen"), QStringLiteral("Hear voice again")},
        {QStringLiteral("topic"), QStringLiteral("/topic [text]"), QStringLiteral("Show or set the channel topic")},
        {QStringLiteral("invite"), QStringLiteral("/invite"), QStringLiteral("Create an invite link and copy it")},
        {QStringLiteral("kick"), QStringLiteral("/kick <user> [reason]"),
            QStringLiteral("Remove a member from this server")},
        {QStringLiteral("ban"), QStringLiteral("/ban <user> [reason]"),
            QStringLiteral("Ban a member from this server")},
        {QStringLiteral("status"), QStringLiteral("/status online|idle|dnd"), QStringLiteral("Set your presence")},
        {QStringLiteral("help"), QStringLiteral("/help"), QStringLiteral("Show available commands")},
    };
    return list;
}

bool CommandParser::isKnown(const QString& name)
{
    for (const auto& c : commands()) {
        if (c.name == name)
            return true;
    }
    return false;
}

} // namespace omachat::client
