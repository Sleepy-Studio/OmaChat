#include "text/MarkdownRenderer.hpp"

#include <gtest/gtest.h>

using omachat::client::MarkdownRenderer;

namespace {
std::string render(const char* md, const char* self = "")
{
    return MarkdownRenderer().render(QString::fromUtf8(md), QString::fromUtf8(self)).toStdString();
}
bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}
} // namespace

TEST(Markdown, RawHtmlIsNeverInterpreted)
{
    const auto out = render("<script>alert(1)</script><b>x</b><img src=x onerror=y>");
    EXPECT_FALSE(contains(out, "<script"));
    EXPECT_FALSE(contains(out, "<img"));
    EXPECT_FALSE(contains(out, "<b>x</b>"));
    EXPECT_TRUE(contains(out, "&lt;script&gt;"));
    EXPECT_TRUE(contains(out, "&lt;b&gt;x&lt;/b&gt;"));
}

TEST(Markdown, EmphasisSubset)
{
    EXPECT_TRUE(contains(render("**bold**"), "<b>bold</b>"));
    EXPECT_TRUE(contains(render("*italic*"), "<i>italic</i>"));
    EXPECT_TRUE(contains(render("~~gone~~"), "<s>gone</s>"));
    EXPECT_FALSE(contains(render("2 * 3 * 4"), "<i>")) << "spaced asterisks are arithmetic";
    EXPECT_FALSE(contains(render("snake_case_name"), "<i>"));
}

TEST(Markdown, InlineCodeIsLiteral)
{
    const auto out = render("run `**not bold** <tag>`");
    EXPECT_TRUE(contains(out, "<code"));
    EXPECT_TRUE(contains(out, "**not bold**"));
    EXPECT_TRUE(contains(out, "&lt;tag&gt;"));
    EXPECT_FALSE(contains(out, "<b>"));
}

TEST(Markdown, CodeBlocksEscapeAndSurviveUnterminated)
{
    const auto out = render("```\n<div>**x**</div>\n```");
    EXPECT_TRUE(contains(out, "<pre"));
    EXPECT_TRUE(contains(out, "&lt;div&gt;**x**&lt;/div&gt;"));
    const auto open = render("```\nstill <code>");
    EXPECT_TRUE(contains(open, "<pre"));
    EXPECT_TRUE(contains(open, "&lt;code&gt;"));
}

TEST(Markdown, OnlySafeSchemesBecomeLinks)
{
    EXPECT_TRUE(contains(render("see https://example.org/a?b=1&c=2"), "<a href=\"https://example.org/a?b=1&amp;c=2\""));
    EXPECT_TRUE(contains(render("omachat://invite/abc123"), "href=\"omachat://invite/abc123\""));
    EXPECT_FALSE(contains(render("javascript:alert(1)"), "<a"));
    EXPECT_FALSE(contains(render("file:///etc/passwd"), "<a"));
    EXPECT_FALSE(contains(render("data:text/html,<b>"), "<a"));
}

// A share link posted in a channel has to render as a clickable link, or it
// sits there as an unclickable wall of base64 and sharing looks broken.
//
// This is the behavioural half of check-omatubelink.py: that check compares the
// two scheme lists statically, this proves a real link actually comes out as an
// <a href>. Both exist because the scheme lives in two places (isSafeUrl and
// the urlRe alternation) and a mismatch between them is silent.
//
// `render` takes a const char*, so text is composed inline rather than with
// QString::arg -- the helper is UTF-8 in and std::string out.
TEST(Markdown, OmatubeShareLinksBecomeClickable)
{
    // A real link, produced by `omatube share dQw4w9WgXcQ --kind audio`.
    EXPECT_TRUE(contains(
        render("look at this omatube://s/eyJpZCI6ImRxVzR3OVcWWNBIiwia2kiOiJhdWRpbyIsIm4iOiJ0aGUgZ29vZCBiaXQifQ"),
        "<a href=\"omatube://s/"));

    // Case-insensitive like every other scheme. QUrl::toEncoded() normalises the
    // scheme to lower case in the href, so assert the normalised form -- the
    // link still works either way, and that is what the click actually uses.
    EXPECT_TRUE(contains(render("OMATUBE://s/abc"), "href=\"omatube://s/abc\""));

    // A lookalike must NOT become a link: the allowlist is an exact scheme
    // list, not a prefix match.
    EXPECT_FALSE(contains(render("omatube-evil://s/abc"), "<a"));
    EXPECT_FALSE(contains(render("xomatube://s/abc"), "<a"));
    EXPECT_FALSE(contains(render("javascript:alert(1)"), "<a"));

    // Note what is deliberately NOT asserted: that a string merely *containing*
    // "omatube://" refuses to linkify. It does linkify, and that is correct --
    // the scheme after the last "//" is the only thing isSafeUrl looks at, so
    // "javascript:alert(1)//omatube://s/x" yields one safe omatube link and one
    // inert run of text. That is the same behaviour http(s) has always had.
}

TEST(Markdown, UrlsCannotBreakOutOfAttributes)
{
    const auto out = render("https://evil.example/\"onmouseover=\"x");
    // The quote ends the URL; nothing after it lands inside the attribute.
    EXPECT_FALSE(contains(out, "onmouseover=\"x\""));
    EXPECT_TRUE(contains(out, "href=\"https://evil.example/\""));
}

TEST(Markdown, QuotesAndMentions)
{
    EXPECT_TRUE(contains(render("> quoted"), "<table"));
    const auto mine = render("hi @alice and @bob", "alice");
    EXPECT_TRUE(contains(mine, "@alice"));
    EXPECT_TRUE(contains(mine, "background-color")) << "self mention is highlighted";
    EXPECT_FALSE(contains(render("mail me@example.org"), "<span")) << "emails are not mentions";
}

TEST(Markdown, PlainPreviewStripsMarkup)
{
    EXPECT_EQ(MarkdownRenderer::plainPreview(QStringLiteral("**hi** `x`")).toStdString(), "hi x");
    EXPECT_EQ(MarkdownRenderer::plainPreview(QString(200, u'a'), 10).size(), 10);
}
