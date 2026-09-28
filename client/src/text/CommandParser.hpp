#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace omachat::client {

// IRC-style composer commands. Parsing is pure; AppController maps each
// command onto the normal daemon API (never onto shell execution).
struct Command {
    QString name; // lowercased, without the slash
    QString argument; // first word after the command
    QString rest; // everything after the command
    QString restAfterArgument;
};

struct CommandInfo {
    QString name;
    QString usage;
    QString description;
};

class CommandParser {
public:
    // Returns nullopt when the text is an ordinary message. "//text" escapes
    // a leading slash and is sent as "/text".
    static std::optional<Command> parse(const QString& text);
    static QString unescape(const QString& text);
    static const QList<CommandInfo>& commands();
    static bool isKnown(const QString& name);
};

} // namespace omachat::client
