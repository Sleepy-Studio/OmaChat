#include "auth/OAuthProviders.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace omachat::server::auth {
namespace {

const OAuthProviderMeta kProviders[] = {
    {
        proto::OAUTH_PROVIDER_DISCORD,
        QStringLiteral("discord"),
        QStringLiteral("https://discord.com/oauth2/authorize"),
        QStringLiteral("https://discord.com/api/oauth2/token"),
        QStringLiteral("https://discord.com/api/users/@me"),
        QStringLiteral("identify"),
    },
    {
        proto::OAUTH_PROVIDER_GITHUB,
        QStringLiteral("github"),
        QStringLiteral("https://github.com/login/oauth/authorize"),
        QStringLiteral("https://github.com/login/oauth/access_token"),
        QStringLiteral("https://api.github.com/user"),
        QStringLiteral("read:user"),
    },
    {
        proto::OAUTH_PROVIDER_GOOGLE,
        QStringLiteral("google"),
        QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"),
        QStringLiteral("https://oauth2.googleapis.com/token"),
        QStringLiteral("https://openidconnect.googleapis.com/v1/userinfo"),
        QStringLiteral("openid profile"),
    },
};

bool validBio(const QString& bio)
{
    if (bio.size() > 300)
        return false;
    for (QChar c : bio) {
        if ((c.category() == QChar::Other_Control && c != u'\n') || c.category() == QChar::Other_Format)
            return false;
    }
    return true;
}

std::optional<QString> avatarUrl(const QJsonObject& obj)
{
    if (!obj.contains(QStringLiteral("avatar_url")))
        return std::nullopt;
    if (obj.value(QStringLiteral("avatar_url")).isNull())
        return QString();
    const QString value = obj.value(QStringLiteral("avatar_url")).toString();
    const QUrl url(value);
    if (value.size() > 2048 || !url.isValid() || url.scheme() != u"https" || url.host().isEmpty()
        || !url.userInfo().isEmpty())
        return std::nullopt;
    return value;
}

} // namespace

std::optional<OAuthProfile> parseProfile(proto::OAuthProvider provider, const QJsonObject& obj)
{
    OAuthProfile p;
    switch (provider) {
    case proto::OAUTH_PROVIDER_DISCORD:
        p.id = obj.value(QStringLiteral("id")).toString();
        p.username = obj.value(QStringLiteral("username")).toString();
        p.displayName = obj.value(QStringLiteral("global_name")).toString();
        if (obj.contains(QStringLiteral("avatar"))) {
            const QString hash = obj.value(QStringLiteral("avatar")).toString();
            static const QRegularExpression safeId(QStringLiteral("^[0-9]+$"));
            static const QRegularExpression safeHash(QStringLiteral("^(a_)?[a-fA-F0-9]+$"));
            if (hash.isEmpty())
                p.avatarUrl = QString();
            else if (safeId.match(p.id).hasMatch() && safeHash.match(hash).hasMatch())
                p.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.png?size=256").arg(p.id, hash);
        }
        break;
    case proto::OAUTH_PROVIDER_GITHUB:
        // GitHub's `id` is a JSON number.
        if (!obj.value(QStringLiteral("id")).isDouble())
            return std::nullopt;
        {
            const double id = obj.value(QStringLiteral("id")).toDouble();
            if (!std::isfinite(id) || id < 1 || id > 9007199254740991.0 || std::floor(id) != id)
                return std::nullopt;
            p.id = QString::number(static_cast<quint64>(id));
        }
        p.username = obj.value(QStringLiteral("login")).toString();
        p.displayName = obj.value(QStringLiteral("name")).toString();
        p.avatarUrl = avatarUrl(obj);
        if (obj.contains(QStringLiteral("bio"))) {
            const QString bio = obj.value(QStringLiteral("bio")).toString().trimmed();
            if (validBio(bio))
                p.bio = bio;
        }
        break;
    case proto::OAUTH_PROVIDER_GOOGLE:
        p.id = obj.value(QStringLiteral("sub")).toString();
        p.username = obj.value(QStringLiteral("name")).toString();
        if (p.username.isEmpty())
            p.username = obj.value(QStringLiteral("email")).toString().section(u'@', 0, 0);
        p.displayName = p.username;
        break;
    default:
        return std::nullopt;
    }
    if (p.id.isEmpty() || p.username.isEmpty())
        return std::nullopt;
    p.displayName
        = validation::displayName(p.displayName).value_or(validation::displayName(p.username).value_or(QString()));
    return p;
}

const OAuthProviderMeta* metaFor(proto::OAuthProvider provider)
{
    for (const auto& m : kProviders) {
        if (m.provider == provider)
            return &m;
    }
    return nullptr;
}

const OAuthProviderMeta* metaByName(const QString& name)
{
    for (const auto& m : kProviders) {
        if (m.name.compare(name, Qt::CaseInsensitive) == 0)
            return &m;
    }
    return nullptr;
}

void exchangeAndFetchProfile(QNetworkAccessManager& net, const OAuthProviderMeta& meta,
    const OAuthProviderSettings& settings, const QString& code, const QString& codeVerifier, const QString& redirectUri,
    std::function<void(std::optional<OAuthProfile>, QString error)> done)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    body.addQueryItem(QStringLiteral("client_id"), settings.clientId);
    body.addQueryItem(QStringLiteral("client_secret"), settings.clientSecret);
    body.addQueryItem(QStringLiteral("code_verifier"), codeVerifier);

    QNetworkRequest req{QUrl(meta.tokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");
    // client_secret_basic (RFC 6749 §2.3.1): required by Discord's token
    // endpoint specifically (it 401s on credentials sent only in the body,
    // even though GitHub's and Google's accept that). Sending both is a
    // harmless superset that works everywhere.
    const QByteArray basicAuth = (settings.clientId + u':' + settings.clientSecret).toUtf8().toBase64();
    req.setRawHeader("Authorization", "Basic " + basicAuth);

    auto* tokenReply = net.post(req, body.query(QUrl::FullyEncoded).toUtf8());
    QObject::connect(tokenReply, &QNetworkReply::finished, tokenReply, [&net, &meta, tokenReply, done] {
        tokenReply->deleteLater();
        if (tokenReply->error() != QNetworkReply::NoError) {
            // An HTTP-level error (4xx/5xx) still has a body worth logging;
            // only a genuine transport failure (DNS, TLS, connection
            // refused) won't. Providers put the real reason here
            // (invalid_client, invalid_grant, ...), which Qt's generic
            // errorString() never captures.
            const auto httpStatus = tokenReply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const QString responseBody = QString::fromUtf8(tokenReply->readAll());
            OMA_WARN("oauth", "token exchange failed", {"provider", meta.name}, {"error", tokenReply->errorString()},
                {"http_status", httpStatus.isValid() ? httpStatus.toInt() : -1}, {"body", responseBody.left(500)});
            done(std::nullopt, QStringLiteral("could not reach %1").arg(meta.name));
            return;
        }
        const auto doc = QJsonDocument::fromJson(tokenReply->readAll());
        const QString accessToken = doc.object().value(QStringLiteral("access_token")).toString();
        if (accessToken.isEmpty()) {
            done(std::nullopt, QStringLiteral("%1 did not return an access token").arg(meta.name));
            return;
        }

        QNetworkRequest profileReq{QUrl(meta.profileUrl)};
        profileReq.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
        // GitHub's API requires a User-Agent on every request.
        profileReq.setRawHeader("User-Agent", "OmaChat-Server");
        auto* profileReply = net.get(profileReq);
        QObject::connect(profileReply, &QNetworkReply::finished, profileReply, [&meta, profileReply, done] {
            profileReply->deleteLater();
            if (profileReply->error() != QNetworkReply::NoError) {
                OMA_WARN(
                    "oauth", "profile fetch failed", {"provider", meta.name}, {"error", profileReply->errorString()});
                done(std::nullopt, QStringLiteral("could not read your %1 profile").arg(meta.name));
                return;
            }
            const auto obj = QJsonDocument::fromJson(profileReply->readAll()).object();
            const auto profile = parseProfile(meta.provider, obj);
            if (!profile) {
                done(std::nullopt, QStringLiteral("%1 profile response was not understood").arg(meta.name));
                return;
            }
            done(profile, QString());
        });
    });
}

} // namespace omachat::server::auth
