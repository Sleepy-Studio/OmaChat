#include "text/MarkdownRenderer.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace omachat::client {

QString MarkdownRenderer::escape(const QString& text)
{
    QString out;
    out.reserve(text.size() + text.size() / 8);
    for (QChar c : text) {
        switch (c.unicode()) {
        case '&':
            out += QStringLiteral("&amp;");
            break;
        case '<':
            out += QStringLiteral("&lt;");
            break;
        case '>':
            out += QStringLiteral("&gt;");
            break;
        case '"':
            out += QStringLiteral("&quot;");
            break;
        case '\'':
            out += QStringLiteral("&#39;");
            break;
        default:
            out += c;
        }
    }
    return out;
}

bool MarkdownRenderer::isSafeUrl(const QString& url)
{
    const QUrl u(url, QUrl::StrictMode);
    if (!u.isValid())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == u"https" || scheme == u"http" || scheme == u"omachat";
}

namespace {

// Placeholder tokens use Private Use Area characters that cannot survive
// escaping ambiguity and never appear in normal text.
constexpr char16_t kTokenStart = 0xE000;
constexpr char16_t kTokenEnd = 0xE001;

struct Stash {
    QStringList items;
    QString put(const QString& html)
    {
        items << html;
        return QString(QChar(kTokenStart)) + QString::number(items.size() - 1) + QChar(kTokenEnd);
    }
    QString restore(QString text) const
    {
        static const QRegularExpression re(QStringLiteral("(\\d+)"));
        // Nested stashes (code inside a link, etc.) resolve over passes.
        for (int pass = 0; pass < 3 && text.contains(QChar(kTokenStart)); ++pass) {
            QString out;
            qsizetype last = 0;
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                out += text.mid(last, m.capturedStart() - last);
                const int idx = m.captured(1).toInt();
                out += idx >= 0 && idx < items.size() ? items.at(idx) : QString();
                last = m.capturedEnd();
            }
            out += text.mid(last);
            text = out;
        }
        return text;
    }
};

} // namespace

QString MarkdownRenderer::renderInline(const QString& raw, const QString& selfUsername) const
{
    Stash stash;
    QString text = raw;

    // 1. Inline code: contents are literal.
    static const QRegularExpression codeRe(QStringLiteral("`([^`\\n]+)`"));
    {
        QString out;
        qsizetype last = 0;
        auto it = codeRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            out += text.mid(last, m.capturedStart() - last);
            out += stash.put(QStringLiteral("<code style=\"background-color:%1;color:%2;\">&nbsp;%3&nbsp;</code>")
                    .arg(m_palette.codeBackground.name(), m_palette.codeText.name(), escape(m.captured(1))));
            last = m.capturedEnd();
        }
        out += text.mid(last);
        text = out;
    }

    // 2. URLs (only safe schemes become links).
    static const QRegularExpression urlRe(QStringLiteral(R"(\b((?:https?|omachat)://[^\s<>"'`]+[^\s<>"'`.,;:!?)\]]))"),
        QRegularExpression::CaseInsensitiveOption);
    {
        QString out;
        qsizetype last = 0;
        auto it = urlRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            out += text.mid(last, m.capturedStart() - last);
            const QString url = m.captured(1);
            if (isSafeUrl(url)) {
                const QString encoded = QString::fromLatin1(QUrl(url).toEncoded());
                out += stash.put(QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%3</a>")
                        .arg(escape(encoded), m_palette.link.name(), escape(url)));
            } else {
                out += url;
            }
            last = m.capturedEnd();
        }
        out += text.mid(last);
        text = out;
    }

    // 3. Everything else is escaped before emphasis markers are converted.
    text = escape(text);

    static const QRegularExpression boldRe(QStringLiteral(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)"));
    static const QRegularExpression strikeRe(QStringLiteral(R"(~~(?=\S)(.+?)(?<=\S)~~)"));
    static const QRegularExpression italicRe(QStringLiteral(R"((?<![\w*])\*(?=\S)([^*]+?)(?<=\S)\*(?![\w*]))"));
    static const QRegularExpression italicUnderscoreRe(QStringLiteral(R"((?<![\w_])_(?=\S)([^_]+?)(?<=\S)_(?![\w_]))"));
    text.replace(boldRe, QStringLiteral("<b>\\1</b>"));
    text.replace(strikeRe, QStringLiteral("<s>\\1</s>"));
    text.replace(italicRe, QStringLiteral("<i>\\1</i>"));
    text.replace(italicUnderscoreRe, QStringLiteral("<i>\\1</i>"));

    // Mentions and channel references (after escaping; names are [\w.-]).
    static const QRegularExpression mentionRe(QStringLiteral(R"((^|[^\w@&;])@([a-zA-Z0-9][a-zA-Z0-9_.\-]{1,31}))"));
    {
        QString out;
        qsizetype last = 0;
        auto it = mentionRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            out += text.mid(last, m.capturedStart(2) - 1 - last);
            const bool self = !selfUsername.isEmpty() && m.captured(2).compare(selfUsername, Qt::CaseInsensitive) == 0;
            out += QStringLiteral("<span style=\"color:%1;%2\"><b>@%3</b></span>")
                       .arg(m_palette.mention.name(),
                           self ? QStringLiteral("background-color:%1;").arg(m_palette.mentionBackground.name())
                                : QString(),
                           m.captured(2));
            last = m.capturedEnd();
        }
        out += text.mid(last);
        text = out;
    }
    return stash.restore(text);
}

QString MarkdownRenderer::render(const QString& markdown, const QString& selfUsername) const
{
    QString html;
    const QStringList lines = markdown.split(u'\n');
    bool inCode = false;
    QStringList codeLines;
    QStringList quoteLines;
    QStringList paragraph;

    auto flushParagraph = [&] {
        if (paragraph.isEmpty())
            return;
        QStringList rendered;
        for (const auto& l : paragraph)
            rendered << renderInline(l, selfUsername);
        // Blocks (pre, table) break lines themselves; no trailing <br/>.
        if (!html.isEmpty() && !html.endsWith(QStringLiteral("</pre>")) && !html.endsWith(QStringLiteral("</table>")))
            html += QStringLiteral("<br/>");
        html += rendered.join(QStringLiteral("<br/>"));
        paragraph.clear();
    };
    auto flushQuote = [&] {
        if (quoteLines.isEmpty())
            return;
        QStringList rendered;
        for (const auto& l : quoteLines)
            rendered << renderInline(l, selfUsername);
        html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\"><tr><td style=\"background-color:%1;\" "
                               "width=\"3\"></td><td style=\"padding-left:8px;\">%2</td></tr></table>")
                    .arg(m_palette.quoteBar.name(), rendered.join(QStringLiteral("<br/>")));
        quoteLines.clear();
    };

    for (const QString& line : lines) {
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            if (!inCode) {
                flushParagraph();
                flushQuote();
                inCode = true;
                codeLines.clear();
                // Anything after the fence on the opening line is a language
                // hint; it is not rendered.
            } else {
                inCode = false;
                html += QStringLiteral("<pre style=\"background-color:%1;color:%2;\">%3</pre>")
                            .arg(m_palette.codeBackground.name(), m_palette.codeText.name(),
                                escape(codeLines.join(u'\n')));
            }
            continue;
        }
        if (inCode) {
            codeLines << line;
            continue;
        }
        if (line.startsWith(QStringLiteral("> ")) || line == u">") {
            flushParagraph();
            quoteLines << line.mid(2);
            continue;
        }
        flushQuote();
        paragraph << line;
    }
    if (inCode) {
        // Unterminated fence: still render as code, never as markup.
        html += QStringLiteral("<pre style=\"background-color:%1;color:%2;\">%3</pre>")
                    .arg(m_palette.codeBackground.name(), m_palette.codeText.name(), escape(codeLines.join(u'\n')));
    }
    flushQuote();
    flushParagraph();
    while (html.endsWith(QStringLiteral("<br/>")))
        html.chop(5);
    return html;
}

QString MarkdownRenderer::plainPreview(const QString& markdown, int maxChars)
{
    QString s = markdown;
    s.replace(QStringLiteral("```"), QString());
    static const QRegularExpression markers(QStringLiteral(R"(\*\*|~~|`)"));
    s.remove(markers);
    s = s.simplified();
    if (s.size() > maxChars)
        s = s.left(maxChars - 1) + QChar(0x2026);
    return s;
}

} // namespace omachat::client
