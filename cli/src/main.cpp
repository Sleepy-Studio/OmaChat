// omachatctl: automation-safe command-line client for omachatd.
//
// Results go to stdout (human text, or the daemon's JSON with --json);
// diagnostics go to stderr. Exit codes: 0 success, 1 command failed,
// 2 usage error, 3 daemon not reachable.

#include "omachat/core/Paths.hpp"
#include "omachat/core/Version.hpp"
#include "omachat/ipc/IpcClient.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <iostream>
#include <optional>

#include <csignal>
#include <termios.h>
#include <unistd.h>

using namespace omachat;

namespace {

enum Exit { Ok = 0, Failed = 1, Usage = 2, NoDaemon = 3 };

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

const char* kUsage = R"(Usage: omachatctl [--json] [--socket PATH] <command> [arguments]

Status
  status                                  connection, voice and server summary
  events [TOPIC...]                       stream daemon events as JSON lines

Accounts
  account list
  account login HOST[:PORT] USERNAME      prompts for the password (or --password-stdin)
  account register HOST[:PORT] USERNAME [--display-name NAME]
  account logout
  account remove ACCOUNT_ID
  connect [ACCOUNT_ID]                    (alias: server connect ACCOUNT_ID)
  disconnect
  trust FINGERPRINT                       trust the server certificate shown in status

Servers and channels
  server list | create NAME | join INVITE | leave SERVER | delete SERVER
  invite create SERVER [--max-uses N] [--expires SECONDS]
  channel list [SERVER]
  channel join CHANNEL                    voice: join; text: focus in the GUI
  channel create SERVER NAME [--voice|--category] [--parent CATEGORY_ID]
  channel topic CHANNEL TEXT | delete CHANNEL | mute CHANNEL | unmute CHANNEL

Messages
  message send CHANNEL [TEXT...] [--attach FILE]...
  message history CHANNEL [--limit N]
  message edit MESSAGE_ID TEXT... | delete MESSAGE_ID
  message search CHANNEL QUERY...
  dm USER TEXT...
  attachment get ATTACHMENT_ID [--name NAME] [--output DIR|FILE]
                                          saves to ~/Downloads unless --output is given
  transfer list | cancel TRANSFER_ID
  presence online|idle|dnd

Voice
  voice join CHANNEL | leave | stats | mode vad|ptt|always
  mute | unmute | toggle-mute | deafen | undeafen | toggle-deafen
  ptt begin | end
  ptt hold                                transmit until stdin closes or Ctrl+C
  audio devices | audio input DEVICE | audio output DEVICE
  volume USER PERCENT                     local volume for one user (0-200)

Moderation
  kick SERVER USER [REASON...] | ban SERVER USER [REASON...] | unban SERVER USER_ID
  role create SERVER NAME [PERMISSION...]
  role assign SERVER USER ROLE_ID | role unassign SERVER USER ROLE_ID

CHANNEL accepts an id, a name ("general") or SERVER/NAME. USER accepts an id or
username. Screen sharing (stream start|stop) is not available in this release.
)";

struct Invocation {
    QString method;
    QJsonObject params;
    std::function<void(const QJsonObject&)> print; // human output
    bool stream = false; // "events": keep running
    bool holdPtt = false;
    int timeoutMs = 20000; // 0 = as long as the transfer takes
};

QString joinRest(const QStringList& a, int from)
{
    return a.mid(from).join(u' ');
}

std::optional<QString> optionValue(QStringList& args, const QString& name)
{
    const auto i = args.indexOf(name);
    if (i < 0 || i + 1 >= args.size())
        return std::nullopt;
    const QString v = args.at(i + 1);
    args.remove(i, 2);
    return v;
}

bool takeFlag(QStringList& args, const QString& name)
{
    return args.removeAll(name) > 0;
}

QString readPassword(bool fromStdin)
{
    std::string line;
    if (fromStdin || !::isatty(STDIN_FILENO)) {
        std::getline(std::cin, line);
        return QString::fromStdString(line);
    }
    std::fprintf(stderr, "Password: ");
    termios old{};
    tcgetattr(STDIN_FILENO, &old);
    termios noecho = old;
    noecho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    std::getline(std::cin, line);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::fprintf(stderr, "\n");
    return QString::fromStdString(line);
}

std::pair<QString, int> hostPort(const QString& s)
{
    // host, host:port, [v6]:port
    if (s.startsWith(u'[')) {
        const auto close = s.indexOf(u']');
        const QString host = s.mid(1, close - 1);
        const int port = s.mid(close + 2).toInt();
        return {host, port > 0 ? port : kDefaultControlPort};
    }
    const auto colon = s.lastIndexOf(u':');
    if (colon > 0 && s.count(u':') == 1)
        return {s.left(colon), s.mid(colon + 1).toInt()};
    return {s, kDefaultControlPort};
}

QString humanSize(double bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024), 0, 'f', 1);
}

void printMessages(const QJsonObject& r)
{
    const auto msgs = r.value(QStringLiteral("messages")).toArray();
    for (qsizetype i = msgs.size() - 1; i >= 0; --i) {
        const auto m = msgs.at(i).toObject();
        const auto ts = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m.value("timestamp").toDouble()));
        out() << ts.toString(QStringLiteral("yyyy-MM-dd HH:mm")) << "  [" << m.value("id").toString() << "] "
              << m.value("author_id").toString() << ": " << m.value("content").toString() << "\n";
        for (const auto& a : m.value("attachments").toArray()) {
            const auto o = a.toObject();
            out() << "                    attachment [" << o.value("id").toString() << "] "
                  << o.value("filename").toString() << " (" << humanSize(o.value("size").toDouble()) << ")\n";
        }
    }
}

void printStatus(const QJsonObject& s)
{
    out() << "state:     " << s.value("state").toString() << "\n";
    const auto err = s.value("error").toObject();
    if (!err.isEmpty()) {
        out() << "error:     " << err.value("code").toString() << ": " << err.value("message").toString() << "\n";
        if (err.contains("fingerprint"))
            out() << "           trust with: omachatctl trust " << err.value("fingerprint").toString() << "\n";
    }
    const auto acct = s.value("account").toObject();
    if (!acct.isEmpty())
        out() << "account:   " << acct.value("username").toString() << "@" << acct.value("host").toString() << ":"
              << acct.value("port").toInt() << "\n";
    else
        out() << "account:   not configured (omachatctl account login HOST USERNAME)\n";
    if (!s.value("instance").toString().isEmpty())
        out() << "instance:  " << s.value("instance").toString() << "\n";
    const auto server = s.value("server").toObject();
    if (!server.isEmpty())
        out() << "server:    " << server.value("name").toString() << "\n";
    const auto v = s.value("voice").toObject();
    if (v.value("joined").toBool()) {
        out() << "voice:     " << v.value("channel").toString() << " (" << v.value("count").toInt() << " connected)"
              << (v.value("muted").toBool() ? " muted" : "") << (v.value("deafened").toBool() ? " deafened" : "")
              << " mode=" << v.value("mode").toString() << "\n";
        for (const auto& p : v.value("participants").toArray()) {
            const auto o = p.toObject();
            out() << "           " << (o.value("speaking").toBool() ? "● " : "○ ") << o.value("name").toString()
                  << (o.value("muted").toBool() ? " (muted)" : "") << "\n";
        }
    } else {
        out() << "voice:     not connected" << (v.value("muted").toBool() ? " (muted)" : "")
              << (v.value("deafened").toBool() ? " (deafened)" : "") << "\n";
    }
    const QString audioError = s.value("audio").toObject().value("error").toString();
    if (!audioError.isEmpty())
        out() << "audio:     " << audioError << "\n";
}

std::optional<Invocation> parse(QStringList args, QString& usageError)
{
    Invocation inv;
    auto need = [&](int n) {
        if (args.size() < n) {
            usageError = QStringLiteral("missing arguments");
            return false;
        }
        return true;
    };
    if (args.isEmpty()) {
        usageError = QStringLiteral("no command given");
        return std::nullopt;
    }
    const QString cmd = args.at(0);
    const QString sub = args.value(1);
    auto simpleOk = [](const QString& text) { return [text](const QJsonObject&) { out() << text << "\n"; }; };

    if (cmd == u"status") {
        inv.method = QStringLiteral("daemon.status");
        inv.print = printStatus;
    } else if (cmd == u"events") {
        inv.method = QStringLiteral("events.subscribe");
        QJsonArray topics;
        for (const auto& t : args.mid(1))
            topics.append(t);
        inv.params = {{"topics", topics}};
        inv.stream = true;
    } else if (cmd == u"account") {
        if (sub == u"list") {
            inv.method = QStringLiteral("account.list");
            inv.print = [](const QJsonObject& r) {
                const QString active = r.value("active").toString();
                for (const auto& a : r.value("accounts").toArray()) {
                    const auto o = a.toObject();
                    out() << (o.value("id").toString() == active ? "* " : "  ") << o.value("id").toString() << "  "
                          << o.value("username").toString() << "@" << o.value("host").toString() << ":"
                          << o.value("port").toInt() << "\n";
                }
            };
        } else if (sub == u"login" || sub == u"register") {
            const bool fromStdin = takeFlag(args, QStringLiteral("--password-stdin"));
            const auto display = optionValue(args, QStringLiteral("--display-name"));
            if (!need(4))
                return std::nullopt;
            const auto [host, port] = hostPort(args.at(2));
            inv.method = sub == u"login" ? QStringLiteral("account.login") : QStringLiteral("account.register");
            inv.params
                = {{"host", host}, {"port", port}, {"username", args.at(3)}, {"password", readPassword(fromStdin)}};
            if (display)
                inv.params.insert(QStringLiteral("display_name"), *display);
            inv.print = simpleOk(sub == u"login" ? QStringLiteral("logged in") : QStringLiteral("account created"));
        } else if (sub == u"logout") {
            inv.method = QStringLiteral("account.logout");
            inv.print = simpleOk(QStringLiteral("logged out"));
        } else if (sub == u"remove" && need(3)) {
            inv.method = QStringLiteral("account.remove");
            inv.params = {{"account", args.at(2).toLongLong()}};
            inv.print = simpleOk(QStringLiteral("account removed"));
        } else {
            usageError = QStringLiteral("unknown account command");
            return std::nullopt;
        }
    } else if (cmd == u"connect" || (cmd == u"server" && sub == u"connect")) {
        inv.method = QStringLiteral("connect");
        const QString id = cmd == u"connect" ? args.value(1) : args.value(2);
        if (!id.isEmpty())
            inv.params = {{"account", id.toLongLong()}};
        inv.print = simpleOk(QStringLiteral("connecting"));
    } else if (cmd == u"disconnect") {
        inv.method = QStringLiteral("disconnect");
        inv.print = simpleOk(QStringLiteral("disconnected"));
    } else if (cmd == u"trust" && need(2)) {
        inv.method = QStringLiteral("certificate.trust");
        inv.params = {{"fingerprint", args.at(1)}};
        inv.print = simpleOk(QStringLiteral("certificate trusted; reconnecting"));
    } else if (cmd == u"server") {
        if (sub == u"list") {
            inv.method = QStringLiteral("server.list");
            inv.print = [](const QJsonObject& r) {
                for (const auto& s : r.value("servers").toArray()) {
                    const auto o = s.toObject();
                    out() << o.value("id").toString() << "  " << o.value("name").toString()
                          << (o.value("is_owner").toBool() ? "  (owner)" : "") << "\n";
                }
            };
        } else if (sub == u"create" && need(3)) {
            inv.method = QStringLiteral("server.create");
            inv.params = {{"name", joinRest(args, 2)}};
            inv.print = [](const QJsonObject& r) { out() << "created " << r.value("id").toString() << "\n"; };
        } else if (sub == u"join" && need(3)) {
            inv.method = QStringLiteral("server.join");
            inv.params = {{"invite", args.at(2)}};
            inv.print = [](const QJsonObject& r) { out() << "joined " << r.value("name").toString() << "\n"; };
        } else if ((sub == u"leave" || sub == u"delete") && need(3)) {
            inv.method = QStringLiteral("server.") + sub;
            inv.params = {{"server", args.at(2)}};
            inv.print = simpleOk(sub == u"leave" ? QStringLiteral("left server") : QStringLiteral("server deleted"));
        } else {
            usageError = QStringLiteral("unknown server command");
            return std::nullopt;
        }
    } else if (cmd == u"invite" && sub == u"create" && need(3)) {
        const auto maxUses = optionValue(args, QStringLiteral("--max-uses"));
        const auto expires = optionValue(args, QStringLiteral("--expires"));
        inv.method = QStringLiteral("invite.create");
        inv.params = {{"server", args.at(2)}};
        if (maxUses)
            inv.params.insert(QStringLiteral("max_uses"), maxUses->toInt());
        if (expires)
            inv.params.insert(QStringLiteral("expires_in"), expires->toInt());
        inv.print = [](const QJsonObject& r) { out() << r.value("uri").toString() << "\n"; };
    } else if (cmd == u"channel") {
        if (sub == u"list") {
            inv.method = QStringLiteral("channel.list");
            if (args.size() > 2)
                inv.params = {{"server", args.at(2)}};
            inv.print = [](const QJsonObject& r) {
                for (const auto& c : r.value("channels").toArray()) {
                    const auto o = c.toObject();
                    const QString type = o.value("type").toString();
                    const QString prefix = type == u"voice" ? QStringLiteral("voice ")
                        : type == u"category"               ? QStringLiteral("----- ")
                                                            : QStringLiteral("#     ");
                    out() << o.value("id").toString() << "  " << prefix << o.value("name").toString();
                    if (type == u"voice")
                        out() << "  (" << o.value("voice_members").toArray().size() << ")";
                    out() << "\n";
                }
            };
        } else if (sub == u"join" && need(3)) {
            // The daemon joins voice channels and focuses text channels in the GUI.
            inv.method = QStringLiteral("channel.join");
            inv.params = {{"channel", args.at(2)}};
            inv.print = simpleOk(QStringLiteral("ok"));
        } else if (sub == u"create" && need(4)) {
            const auto parent = optionValue(args, QStringLiteral("--parent"));
            const bool voice = takeFlag(args, QStringLiteral("--voice"));
            const bool category = takeFlag(args, QStringLiteral("--category"));
            inv.method = QStringLiteral("channel.create");
            inv.params = {{"server", args.at(2)}, {"name", joinRest(args, 3)},
                {"type",
                    voice          ? "voice"
                        : category ? "category"
                                   : "text"}};
            if (parent)
                inv.params.insert(QStringLiteral("parent"), *parent);
            inv.print = [](const QJsonObject& r) { out() << "created " << r.value("id").toString() << "\n"; };
        } else if (sub == u"topic" && need(3)) {
            inv.method = QStringLiteral("channel.update");
            inv.params = {{"channel", args.at(2)}, {"topic", joinRest(args, 3)}};
            inv.print = simpleOk(QStringLiteral("topic updated"));
        } else if (sub == u"delete" && need(3)) {
            inv.method = QStringLiteral("channel.delete");
            inv.params = {{"channel", args.at(2)}};
            inv.print = simpleOk(QStringLiteral("channel deleted"));
        } else if ((sub == u"mute" || sub == u"unmute") && need(3)) {
            inv.method = QStringLiteral("channel.mute");
            inv.params = {{"channel", args.at(2)}, {"muted", sub == u"mute"}};
            inv.print = simpleOk(
                sub == u"mute" ? QStringLiteral("notifications muted") : QStringLiteral("notifications unmuted"));
        } else {
            usageError = QStringLiteral("unknown channel command");
            return std::nullopt;
        }
    } else if (cmd == u"message") {
        QJsonArray files;
        // The daemon has its own working directory, so paths go over absolute.
        while (const auto f = optionValue(args, QStringLiteral("--attach")))
            files.append(QFileInfo(*f).absoluteFilePath());
        if (sub == u"send" && need(files.isEmpty() ? 4 : 3)) {
            inv.method = QStringLiteral("message.send");
            inv.params = {{"channel", args.at(2)}, {"content", joinRest(args, 3)}};
            if (!files.isEmpty()) {
                inv.params.insert(QStringLiteral("files"), files);
                inv.timeoutMs = 0;
            }
            inv.print = [](const QJsonObject& r) {
                out() << "sent " << r.value("id").toString() << "\n";
                for (const auto& a : r.value("attachments").toArray())
                    out() << "attached " << a.toObject().value("filename").toString() << " ["
                          << a.toObject().value("id").toString() << "]\n";
            };
        } else if (sub == u"history" && need(3)) {
            const auto lim = optionValue(args, QStringLiteral("--limit"));
            inv.method = QStringLiteral("message.history");
            inv.params = {{"channel", args.at(2)}, {"limit", lim ? lim->toInt() : 20}};
            inv.print = printMessages;
        } else if (sub == u"edit" && need(4)) {
            inv.method = QStringLiteral("message.edit");
            inv.params = {{"message", args.at(2)}, {"content", joinRest(args, 3)}};
            inv.print = simpleOk(QStringLiteral("edited"));
        } else if (sub == u"delete" && need(3)) {
            inv.method = QStringLiteral("message.delete");
            inv.params = {{"message", args.at(2)}};
            inv.print = simpleOk(QStringLiteral("deleted"));
        } else if (sub == u"search" && need(4)) {
            inv.method = QStringLiteral("message.search");
            inv.params = {{"channel", args.at(2)}, {"query", joinRest(args, 3)}};
            inv.print = printMessages;
        } else {
            usageError = QStringLiteral("unknown message command");
            return std::nullopt;
        }
    } else if (cmd == u"attachment") {
        const auto name = optionValue(args, QStringLiteral("--name"));
        const auto output = optionValue(args, QStringLiteral("--output"));
        if (sub == u"get" && need(3)) {
            inv.method = QStringLiteral("attachment.download");
            inv.params = {{"attachment", args.at(2)},
                {"to", output ? QFileInfo(*output).absoluteFilePath() : QStringLiteral("downloads")}};
            if (name)
                inv.params.insert(QStringLiteral("filename"), *name);
            inv.timeoutMs = 0;
            inv.print = [](const QJsonObject& r) { out() << r.value("path").toString() << "\n"; };
        } else {
            usageError = QStringLiteral("unknown attachment command");
            return std::nullopt;
        }
    } else if (cmd == u"transfer") {
        if (sub == u"list") {
            inv.method = QStringLiteral("transfer.list");
            inv.print = [](const QJsonObject& r) {
                const auto list = r.value("transfers").toArray();
                if (list.isEmpty())
                    out() << "no transfers in progress\n";
                for (const auto& t : list) {
                    const auto o = t.toObject();
                    out() << o.value("id").toString() << "  " << o.value("direction").toString() << "  "
                          << o.value("name").toString() << "  " << humanSize(o.value("transferred").toDouble())
                          << " / " << humanSize(o.value("total").toDouble()) << "\n";
                }
            };
        } else if (sub == u"cancel" && need(3)) {
            inv.method = QStringLiteral("transfer.cancel");
            inv.params = {{"id", args.at(2)}};
            inv.print = simpleOk(QStringLiteral("cancelled"));
        } else {
            usageError = QStringLiteral("unknown transfer command");
            return std::nullopt;
        }
    } else if (cmd == u"dm" && need(3)) {
        inv.method = QStringLiteral("dm.send");
        inv.params = {{"user", args.at(1)}, {"content", joinRest(args, 2)}};
        inv.print = [](const QJsonObject& r) { out() << "sent " << r.value("id").toString() << "\n"; };
    } else if (cmd == u"presence" && need(2)) {
        inv.method = QStringLiteral("presence.set");
        inv.params = {{"status", args.at(1)}};
        inv.print = simpleOk(QStringLiteral("presence set"));
    } else if (cmd == u"voice") {
        if (sub == u"join" && need(3)) {
            inv.method = QStringLiteral("voice.join");
            inv.params = {{"channel", args.at(2)}};
            inv.print = [](const QJsonObject& r) { out() << "joined " << r.value("channel").toString() << "\n"; };
        } else if (sub == u"leave") {
            inv.method = QStringLiteral("voice.leave");
            inv.print = simpleOk(QStringLiteral("left voice"));
        } else if (sub == u"stats") {
            inv.method = QStringLiteral("voice.stats");
            inv.print = [](const QJsonObject& r) { out() << QJsonDocument(r).toJson(QJsonDocument::Indented); };
        } else if (sub == u"mode" && need(3)) {
            inv.method = QStringLiteral("voice.mode");
            inv.params = {{"mode", args.at(2)}};
            inv.print = simpleOk(QStringLiteral("mode set"));
        } else {
            usageError = QStringLiteral("unknown voice command");
            return std::nullopt;
        }
    } else if (cmd == u"mute" || cmd == u"unmute" || cmd == u"deafen" || cmd == u"undeafen" || cmd == u"toggle-mute"
        || cmd == u"toggle-deafen") {
        inv.method = QStringLiteral("voice.") + QString(cmd).replace(u'-', u'_');
        inv.print = [](const QJsonObject& r) {
            if (r.value("deafened").toBool())
                out() << "deafened (microphone off, voice silenced)\n";
            else
                out() << (r.value("muted").toBool() ? "muted" : "unmuted") << "\n";
        };
    } else if (cmd == u"ptt") {
        if (sub == u"begin" || sub == u"end") {
            inv.method = QStringLiteral("ptt.") + sub;
            inv.print = [](const QJsonObject&) { };
        } else if (sub == u"hold") {
            inv.method = QStringLiteral("ptt.begin");
            inv.holdPtt = true;
        } else {
            usageError = QStringLiteral("ptt takes begin, end or hold");
            return std::nullopt;
        }
    } else if (cmd == u"audio") {
        if (sub == u"devices") {
            inv.method = QStringLiteral("audio.devices");
            inv.print = [](const QJsonObject& r) {
                out() << "inputs:\n  default\n";
                for (const auto& d : r.value("inputs").toArray())
                    out() << "  " << d.toObject().value("id").toString() << "  ("
                          << d.toObject().value("name").toString() << ")\n";
                out() << "outputs:\n  default\n";
                for (const auto& d : r.value("outputs").toArray())
                    out() << "  " << d.toObject().value("id").toString() << "  ("
                          << d.toObject().value("name").toString() << ")\n";
            };
        } else if ((sub == u"input" || sub == u"output") && need(3)) {
            inv.method = QStringLiteral("audio.set");
            inv.params = {{sub, args.at(2)}};
            inv.print = simpleOk(QStringLiteral("audio device set"));
        } else {
            usageError = QStringLiteral("unknown audio command");
            return std::nullopt;
        }
    } else if (cmd == u"volume" && need(3)) {
        inv.method = QStringLiteral("audio.user_volume");
        inv.params = {{"user", args.at(1)}, {"volume", args.at(2).toDouble() / 100.0}};
        inv.print = simpleOk(QStringLiteral("volume set"));
    } else if ((cmd == u"kick" || cmd == u"ban") && need(3)) {
        inv.method = QStringLiteral("moderation.") + cmd;
        inv.params = {{"server", args.at(1)}, {"user", args.at(2)}, {"reason", joinRest(args, 3)}};
        inv.print = simpleOk(cmd == u"kick" ? QStringLiteral("kicked") : QStringLiteral("banned"));
    } else if (cmd == u"unban" && need(3)) {
        inv.method = QStringLiteral("moderation.unban");
        inv.params = {{"server", args.at(1)}, {"user", args.at(2)}};
        inv.print = simpleOk(QStringLiteral("unbanned"));
    } else if (cmd == u"role") {
        if (sub == u"create" && need(4)) {
            QJsonArray perms;
            for (const auto& p : args.mid(4))
                perms.append(p);
            inv.method = QStringLiteral("role.create");
            inv.params = {{"server", args.at(2)}, {"name", args.at(3)}, {"permissions", perms}};
            inv.print = [](const QJsonObject& r) { out() << "created role " << r.value("id").toString() << "\n"; };
        } else if ((sub == u"assign" || sub == u"unassign") && need(5)) {
            inv.method = QStringLiteral("role.assign");
            inv.params
                = {{"server", args.at(2)}, {"user", args.at(3)}, {"role", args.at(4)}, {"add", sub == u"assign"}};
            inv.print = simpleOk(QStringLiteral("roles updated"));
        } else {
            usageError = QStringLiteral("unknown role command");
            return std::nullopt;
        }
    } else if (cmd == u"stream") {
        usageError = QStringLiteral("screen sharing is not available in OmaChat %1").arg(QString::fromLatin1(kVersion));
        return std::nullopt;
    } else {
        usageError = QStringLiteral("unknown command '%1'").arg(cmd);
        return std::nullopt;
    }
    if (inv.method.isEmpty() && usageError.isEmpty())
        usageError = QStringLiteral("missing arguments");
    if (!usageError.isEmpty())
        return std::nullopt;
    return inv;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    const bool json = takeFlag(args, QStringLiteral("--json"));
    if (takeFlag(args, QStringLiteral("--help")) || takeFlag(args, QStringLiteral("-h"))) {
        out() << kUsage;
        return Ok;
    }
    if (takeFlag(args, QStringLiteral("--version"))) {
        out() << "omachatctl " << kVersion << "\n";
        return Ok;
    }
    const QString socket = optionValue(args, QStringLiteral("--socket")).value_or(paths::socketPath());

    QString usageError;
    auto inv = parse(args, usageError);
    if (!inv) {
        err() << "omachatctl: " << usageError << "\n\n" << kUsage;
        err().flush();
        return Usage;
    }

    ipc::IpcClient client;
    int exitCode = Ok;
    auto finish = [&](int code) {
        exitCode = code;
        out().flush();
        err().flush();
        app.exit(code);
    };

    auto emitReply = [&](const ipc::Reply& r) {
        if (!r.ok) {
            if (json) {
                out() << QJsonDocument(QJsonObject{{"ok", false},
                                           {"error", QJsonObject{{"code", r.errorCode}, {"message", r.errorMessage}}}})
                             .toJson(QJsonDocument::Compact)
                      << "\n";
            }
            err() << "omachatctl: " << r.errorCode << ": " << r.errorMessage << "\n";
            return false;
        }
        if (json)
            out() << QJsonDocument(r.result).toJson(QJsonDocument::Compact) << "\n";
        else if (inv->print)
            inv->print(r.result);
        return true;
    };

    QObject::connect(&client, &ipc::IpcClient::connectionFailed, &app, [&](const QString& reason) {
        err() << "omachatctl: cannot reach omachatd at " << socket << " (" << reason << ")\n"
              << "hint: systemctl --user start omachat.service\n";
        finish(NoDaemon);
    });
    QObject::connect(&client, &ipc::IpcClient::eventReceived, &app, [&](const QString& name, const QJsonObject& data) {
        out() << QJsonDocument(QJsonObject{{"event", name}, {"data", data}}).toJson(QJsonDocument::Compact) << "\n";
        out().flush();
    });
    QObject::connect(&client, &ipc::IpcClient::disconnected, &app, [&] {
        if (inv->stream || inv->holdPtt) {
            err() << "omachatctl: daemon connection closed\n";
            finish(Failed);
        }
    });

    auto run = [&] {
        if (inv->method == u"dm.send") {
            client.request(QStringLiteral("dm.open"), {{"user", inv->params.value("user")}}, [&](const ipc::Reply& dm) {
                if (!dm.ok) {
                    finish(emitReply(dm) ? Ok : Failed);
                    return;
                }
                client.request(QStringLiteral("message.send"),
                    {{"channel", dm.result.value("id")}, {"content", inv->params.value("content")}},
                    [&](const ipc::Reply& r) { finish(emitReply(r) ? Ok : Failed); });
            });
            return;
        }
        client.request(
            inv->method, inv->params,
            [&](const ipc::Reply& r) {
                const bool ok = emitReply(r);
                if (!ok) {
                    finish(Failed);
                    return;
                }
                if (inv->stream)
                    return; // keep printing events until interrupted
                if (inv->holdPtt) {
                    err() << "push-to-talk active; close stdin or press Ctrl+C to release\n";
                    return;
                }
                finish(Ok);
            },
            inv->method.startsWith(u"account.") ? 60000 : inv->timeoutMs);
    };

    // Ctrl+C / SIGTERM / stdin EOF end a streaming or held command cleanly
    // (releasing push-to-talk first).
    static int signalPipe[2] = {-1, -1};
    if (::pipe(signalPipe) == 0) {
        std::signal(SIGINT, [](int) { [[maybe_unused]] auto n = ::write(signalPipe[1], "x", 1); });
        std::signal(SIGTERM, [](int) { [[maybe_unused]] auto n = ::write(signalPipe[1], "x", 1); });
    }
    auto release = [&] {
        if (inv->holdPtt && client.isConnected()) {
            client.request(QStringLiteral("ptt.end"), {}, [&](const ipc::Reply&) { finish(Ok); });
            return;
        }
        finish(Ok);
    };
    QSocketNotifier sigNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, &app, release);
    std::unique_ptr<QSocketNotifier> stdinNotifier;
    if (inv->holdPtt) {
        stdinNotifier = std::make_unique<QSocketNotifier>(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(stdinNotifier.get(), &QSocketNotifier::activated, &app, [&] {
            char buf[256];
            if (::read(STDIN_FILENO, buf, sizeof buf) <= 0) {
                stdinNotifier->setEnabled(false);
                release();
            }
        });
    }

    QObject::connect(&client, &ipc::IpcClient::connected, &app, run);
    // Connect from inside the event loop so failures can end it.
    QTimer::singleShot(0, &app, [&] { client.connectToDaemon(socket); });
    app.exec();
    return exitCode;
}
