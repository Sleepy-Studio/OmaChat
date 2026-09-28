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
