#pragma once

#include <QColor>
#include <QString>

namespace omachat::client {

// Renders OmaChat's markdown subset to the small HTML subset understood by
// Qt's rich-text engine. Every character of user content is HTML-escaped
// first; only tags generated here ever reach the renderer, so raw HTML in
// messages is displayed literally and can never inject markup.
//
// Supported: **bold**, *italic*, ~~strike~~, `inline code`, ```code blocks```,
// > quotes, http(s)/omachat URLs, and @mentions / #channels highlighting.
struct MarkdownPalette {
    QColor codeBackground{0x22, 0x22, 0x22};
    QColor codeText{0xdd, 0xdd, 0xdd};
    QColor link{0x6c, 0xb6, 0xff};
    QColor quoteBar{0x55, 0x55, 0x55};
    QColor mention{0xe6, 0x8e, 0x0d};
    QColor mentionBackground{0x3a, 0x2c, 0x16};
};

class MarkdownRenderer {
public:
    using Palette = MarkdownPalette;

    explicit MarkdownRenderer(Palette p = {})
        : m_palette(p)
    {
    }
    void setPalette(const Palette& p) { m_palette = p; }

    QString render(const QString& markdown, const QString& selfUsername = {}) const;

    // Plain-text preview (markup stripped), for replies and notifications.
    static QString plainPreview(const QString& markdown, int maxChars = 120);

    static QString escape(const QString& text);
    static bool isSafeUrl(const QString& url);

private:
    QString renderInline(const QString& text, const QString& selfUsername) const;
    Palette m_palette;
};

} // namespace omachat::client
