#include "omachat/core/Log.hpp"

#include <QDateTime>
#include <QtGlobal>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace omachat::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_writeMutex;
std::string g_component = "omachat";
bool g_journal = false;

const char* levelName(Level l)
{
    switch (l) {
    case Level::Trace:
        return "trace";
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warning";
    case Level::Error:
        return "error";
    case Level::Critical:
        return "critical";
    case Level::Off:
        return "off";
    }
    return "info";
}

// sd-daemon(3) priority prefixes so journald keeps the severity.
const char* journalPrefix(Level l)
{
    switch (l) {
    case Level::Trace:
    case Level::Debug:
        return "<7>";
    case Level::Info:
        return "<6>";
    case Level::Warning:
        return "<4>";
    case Level::Error:
        return "<3>";
    case Level::Critical:
        return "<2>";
    case Level::Off:
        return "<6>";
    }
    return "<6>";
}

void appendQuoted(std::string& out, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    bool needsQuotes = utf8.isEmpty();
    for (char c : utf8) {
        if (c == ' ' || c == '"' || c == '=' || static_cast<unsigned char>(c) < 0x20) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        out.append(utf8.constData(), static_cast<size_t>(utf8.size()));
        return;
    }
    out.push_back('"');
    for (char c : utf8) {
        switch (c) {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        case '\n':
            out.append("\\n");
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out.push_back('?');
            else
                out.push_back(c);
        }
    }
    out.push_back('"');
}

void qtHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Level l = Level::Info;
    switch (type) {
    case QtDebugMsg:
        l = Level::Debug;
        break;
    case QtInfoMsg:
        l = Level::Info;
        break;
    case QtWarningMsg:
        l = Level::Warning;
        break;
    case QtCriticalMsg:
        l = Level::Error;
        break;
    case QtFatalMsg:
        l = Level::Critical;
        break;
    }
    if (!enabled(l))
        return;
    const char* category = ctx.category ? ctx.category : "qt";
    write(l, category, msg.toStdString());
}

} // namespace

void setLevel(Level l)
{
    g_level.store(l, std::memory_order_relaxed);
}

Level level()
{
    return g_level.load(std::memory_order_relaxed);
}

std::optional<Level> parseLevel(std::string_view name)
{
    if (name == "trace")
        return Level::Trace;
    if (name == "debug")
        return Level::Debug;
    if (name == "info")
        return Level::Info;
    if (name == "warning" || name == "warn")
        return Level::Warning;
    if (name == "error")
        return Level::Error;
    if (name == "critical")
        return Level::Critical;
    if (name == "off")
        return Level::Off;
    return std::nullopt;
}

void initialize(std::string_view component, Level l)
{
    g_component = std::string(component);
    g_journal = std::getenv("JOURNAL_STREAM") != nullptr;
    if (const char* env = std::getenv("OMACHAT_LOG_LEVEL")) {
        if (auto parsed = parseLevel(env))
            l = *parsed;
    }
    setLevel(l);
    qInstallMessageHandler(qtHandler);
}

void write(Level l, std::string_view category, std::string_view message, std::initializer_list<Field> fields)
{
    std::string line;
    line.reserve(160);
    if (g_journal) {
        line.append(journalPrefix(l));
    } else {
        line.append(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString());
        line.push_back(' ');
        line.append(g_component);
        line.push_back(' ');
    }
    line.append("level=");
    line.append(levelName(l));
    line.append(" cat=");
    line.append(category);
    line.append(" msg=");
    appendQuoted(line, QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size())));
    for (const Field& f : fields) {
        line.push_back(' ');
        line.append(f.key);
        line.push_back('=');
        appendQuoted(line, f.value);
    }
    line.push_back('\n');

    std::lock_guard lock(g_writeMutex);
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (l >= Level::Warning)
        std::fflush(stderr);
}

} // namespace omachat::log
