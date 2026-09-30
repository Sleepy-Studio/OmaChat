#include "omachat/core/Validation.hpp"

#include <QRegularExpression>

namespace omachat::validation {
namespace {

bool hasControl(const QString& s, bool allowNewlines)
{
    for (QChar c : s) {
        if (allowNewlines && (c == u'\n' || c == u'\t'))
            continue;
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format || c == QChar::LineSeparator
            || c == QChar::ParagraphSeparator)
            return true;
    }
    return false;
}

std::optional<QString> boundedLabel(const QString& input, int maxLen)
{
    const QString s = input.trimmed();
    if (s.isEmpty() || s.size() > maxLen || hasControl(s, false))
        return std::nullopt;
    return s;
}

} // namespace

std::optional<QString> username(const QString& input)
{
    const QString s = input.trimmed().toLower();
    if (s.size() < 2 || s.size() > 32)
        return std::nullopt;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool alnum = (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9');
        if (i == 0 && !alnum)
            return std::nullopt;
        if (!alnum && c != u'_' && c != u'.' && c != u'-')
            return std::nullopt;
    }
    return s;
}

std::optional<QString> displayName(const QString& input)
{
    return boundedLabel(input, 64);
}

std::optional<QString> serverName(const QString& input)
{
    return boundedLabel(input, 100);
}

std::optional<QString> channelName(const QString& input)
{
    return boundedLabel(input, 64);
}

std::optional<QString> roleName(const QString& input)
{
    return boundedLabel(input, 32);
}

std::optional<QString> messageContent(const QString& input)
{
    QString out;
    out.reserve(input.size());
    for (QChar c : input) {
        if (c == u'\r')
            continue;
        if (c == u'\n' || c == u'\t' || !(c.category() == QChar::Other_Control)) {
            out.append(c);
        }
    }
    // Trim only trailing whitespace; leading indentation matters in code.
    while (!out.isEmpty() && out.back().isSpace())
        out.chop(1);
    while (!out.isEmpty() && out.front() == u'\n')
        out.remove(0, 1);
    if (out.trimmed().isEmpty() || out.size() > kMaxMessageLength)
        return std::nullopt;
    return out;
}

std::optional<QString> topic(const QString& input)
{
    const QString s = input.trimmed();
    if (s.size() > kMaxTopicLength || hasControl(s, false))
        return std::nullopt;
    return s;
}

std::optional<QString> channelDescription(const QString& input)
{
    const QString s = input.trimmed();
    if (s.size() > kMaxChannelDescriptionLength || hasControl(s, true))
        return std::nullopt;
    return s;
}

bool passwordAcceptable(const QString& password)
{
    const auto bytes = password.toUtf8().size();
    return password.size() >= kMinPasswordLength && bytes <= kMaxPasswordLength;
}

std::optional<QString> reaction(const QString& input)
{
    const QString s = input.trimmed();
    if (s.isEmpty() || s.size() > 32 || hasControl(s, false))
        return std::nullopt;
    for (QChar c : s) {
        if (c.isSpace())
            return std::nullopt;
    }
    return s;
}

std::optional<QString> emojiName(const QString& input)
{
    const QString s = input.trimmed().toLower();
    if (s.size() < 2 || s.size() > 32)
        return std::nullopt;
    for (QChar c : s) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u'_')
            return std::nullopt;
    }
    return s;
}

std::optional<QString> filename(const QString& input)
{
    QString out;
    out.reserve(input.size());
    for (QChar c : input) {
        if (c == u'/' || c == u'\\')
            out.append(u'_');
        else if (c.category() != QChar::Other_Control && c.category() != QChar::Other_Format)
            out.append(c);
    }
    out = out.trimmed();
    if (out.isEmpty() || out.size() > 200 || out == u"." || out == u"..")
        return std::nullopt;
    return out;
}

QString mimeType(const QString& input)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^[a-z0-9][a-z0-9!#$&^_.+\-]{0,63}/[a-z0-9][a-z0-9!#$&^_.+\-]{0,63}$)"));
    const QString base = input.section(u';', 0, 0).trimmed().toLower();
    return re.match(base).hasMatch() ? base : QStringLiteral("application/octet-stream");
}

} // namespace omachat::validation
