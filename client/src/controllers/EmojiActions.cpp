// Unicode emoji catalog (bundled resource) and recent-emoji tracking for the
// picker and composer ":shortcode:" completion.

#include "controllers/AppController.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omachat::client {

namespace {
constexpr int kMaxRecentEmoji = 24;
}

QVariantList AppController::emojiCatalog() const
{
    if (!m_emojiCatalog.isEmpty())
        return m_emojiCatalog;

    QFile file(QStringLiteral(":/qt/qml/OmaChat/data/emoji.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    for (const auto& v : doc.array())
        m_emojiCatalog << v.toObject().toVariantMap();
    return m_emojiCatalog;
}

QVariantList AppController::recentEmoji() const
{
    QVariantList out;
    for (const auto& glyph : m_recentEmoji)
        out << glyph;
    return out;
}

void AppController::noteEmojiUsed(const QString& glyph)
{
    m_recentEmoji.removeAll(glyph);
    m_recentEmoji.prepend(glyph);
    while (m_recentEmoji.size() > kMaxRecentEmoji)
        m_recentEmoji.removeLast();
}

QVariantList AppController::serverEmoji() const
{
    if (homeSelected())
        return {};
    QVariantList out;
    for (const auto& e : m_emojiById) {
        if (e.value(QStringLiteral("server_id")).toString() == m_selectedServer)
            out << e.toVariantMap();
    }
    return out;
}

bool AppController::canManageEmoji() const
{
    return !homeSelected() && serverPermission("MANAGE_EMOJI");
}

void AppController::createServerEmoji(const QString& name, const QUrl& fileUrl)
{
    if (homeSelected() || m_selectedChannel.isEmpty())
        return;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    call(QStringLiteral("emoji.create"),
        {{"server", m_selectedServer}, {"channel", m_selectedChannel}, {"name", name}, {"file", path}});
}

void AppController::deleteServerEmoji(const QString& emojiId)
{
    if (homeSelected())
        return;
    call(QStringLiteral("emoji.delete"), {{"server", m_selectedServer}, {"emoji", emojiId}});
}

QVariantMap AppController::customEmojiByName(const QString& name) const
{
    for (const auto& e : m_emojiById) {
        if (e.value(QStringLiteral("server_id")).toString() == m_selectedServer
            && e.value(QStringLiteral("name")).toString() == name)
            return e.toVariantMap();
    }
    return {};
}

} // namespace omachat::client
