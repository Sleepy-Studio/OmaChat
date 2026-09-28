#include "auth/Credentials.hpp"
#include "auth/OAuthProviders.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

#include <QRandomGenerator>

#include <algorithm>

namespace omachat::server {

namespace {

const OAuthProviderSettings* settingsFor(const ServerConfig& config, proto::OAuthProvider provider)
{
    switch (provider) {
    case proto::OAUTH_PROVIDER_DISCORD:
        return &config.oauthDiscord;
    case proto::OAUTH_PROVIDER_GITHUB:
        return &config.oauthGithub;
    case proto::OAUTH_PROVIDER_GOOGLE:
        return &config.oauthGoogle;
    default:
        return nullptr;
    }
}

} // namespace

QString ChatServer::uniqueUsernameFrom(const QString& suggestion)
{
    QString base = validation::username(suggestion).value_or(QString());
    if (base.isEmpty())
        base = QStringLiteral("user");
    if (!m_store.userByName(base))
        return base;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const QString candidate
            = base.left(32 - 5) + QString::number(QRandomGenerator::global()->bounded(10000, 99999));
        if (!m_store.userByName(candidate))
            return candidate;
    }
    // Astronomically unlikely, but never loop forever.
    return base.left(20) + auth::randomToken().left(10);
}

void ChatServer::handleOAuthLogin(Session& s, std::uint64_t rid, const proto::OAuthLoginRequest& m)
{
    const auto* meta = auth::metaFor(m.provider());
    const auto* settings = settingsFor(m_config, m.provider());
    if (!meta || !settings || !settings->enabled) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("that sign-in method is not enabled here"));
        return;
    }
    auto& ipBucket = m_authByIp.emplace(s.peer.toString(), TokenBucket(10, 10.0 / 60.0)).first->second;
    if (!ipBucket.tryConsume()) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many attempts"), ipBucket.retryAfterMs());
        return;
    }

    s.authInFlight = true;
    const quint64 connId = s.connId;
    const QString providerName = meta->name;
    auth::exchangeAndFetchProfile(m_net, *meta, *settings, QString::fromStdString(m.code()),
        QString::fromStdString(m.code_verifier()), QString::fromStdString(m.redirect_uri()),
        [this, connId, rid, providerName](std::optional<auth::OAuthProfile> profile, QString error) {
            Session* sp = sessionFor(connId);
            if (!sp)
                return;
            sp->authInFlight = false;
            if (!profile) {
                OMA_INFO("oauth", "login failed", {"provider", providerName}, {"error", error});
                replyError(*sp, rid, proto::ERROR_AUTHENTICATION,
                    error.isEmpty() ? QStringLiteral("sign-in failed") : error);
                return;
            }

            if (const auto identity = m_store.oauthIdentity(providerName, profile->id)) {
                if (!m_state.user(identity->userId)) {
                    replyError(*sp, rid, proto::ERROR_INTERNAL, QStringLiteral("linked account no longer exists"));
                    return;
                }
                const QString refresh = auth::randomToken();
                const SessionRecord session{m_ids.next(), identity->userId, auth::tokenDigest(refresh),
                    now() + std::int64_t(m_config.refreshTokenDays) * 86400000};
                m_store.insertSession(session);
                OMA_INFO("oauth", "login", {"provider", providerName}, {"user", identity->userId});
                completeAuth(*sp, rid, identity->userId, session.id, refresh);
                return;
            }

            // No account linked to this provider identity yet: register one.
            // The display name comes from the provider profile; the username
            // is derived from it and made unique, since providers don't
            // guarantee OmaChat's stricter charset or uniqueness.
            const QString displayName = validation::displayName(profile->username).value_or(providerName);
            const QString username = uniqueUsernameFrom(profile->username);
            UserRecord user{m_ids.next(), username, displayName, QString(), QString(), now()};
            if (!m_store.insertUser(user)) {
                replyError(*sp, rid, proto::ERROR_CONFLICT, QStringLiteral("could not create an account"));
                return;
            }
            const OAuthIdentityRecord identity{
                m_ids.next(), user.id, providerName, profile->id, profile->username, now()};
            if (!m_store.insertOAuthIdentity(identity)) {
                replyError(*sp, rid, proto::ERROR_INTERNAL, QStringLiteral("could not link your account"));
                return;
            }
            m_state.putUser(user);
            OMA_INFO("oauth", "user registered", {"provider", providerName}, {"user", user.id});

            const QString refresh = auth::randomToken();
            const SessionRecord session{m_ids.next(), user.id, auth::tokenDigest(refresh),
                now() + std::int64_t(m_config.refreshTokenDays) * 86400000};
            m_store.insertSession(session);
            completeAuth(*sp, rid, user.id, session.id, refresh);
        });
}

void ChatServer::handleOAuthLink(Session& s, std::uint64_t rid, const proto::OAuthLinkRequest& m)
{
    const auto* meta = auth::metaFor(m.provider());
    const auto* settings = settingsFor(m_config, m.provider());
    if (!meta || !settings || !settings->enabled) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("that sign-in method is not enabled here"));
        return;
    }
    auto& ipBucket = m_authByIp.emplace(s.peer.toString(), TokenBucket(10, 10.0 / 60.0)).first->second;
    if (!ipBucket.tryConsume()) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many attempts"), ipBucket.retryAfterMs());
        return;
    }

    const quint64 connId = s.connId;
    const Id userId = s.userId;
    const QString providerName = meta->name;
    auth::exchangeAndFetchProfile(m_net, *meta, *settings, QString::fromStdString(m.code()),
        QString::fromStdString(m.code_verifier()), QString::fromStdString(m.redirect_uri()),
        [this, connId, rid, userId, providerName](std::optional<auth::OAuthProfile> profile, QString error) {
            Session* sp = sessionFor(connId);
            if (!sp || sp->userId != userId) // the session logged out or switched account mid-flight
                return;
            if (!profile) {
                OMA_INFO("oauth", "link failed", {"provider", providerName}, {"user", userId}, {"error", error});
                replyError(*sp, rid, proto::ERROR_BAD_REQUEST, error.isEmpty() ? QStringLiteral("linking failed") : error);
                return;
            }
            if (const auto existing = m_store.oauthIdentity(providerName, profile->id)) {
                if (existing->userId != userId) {
                    replyError(*sp, rid, proto::ERROR_CONFLICT,
                        QStringLiteral("that %1 account is already linked to a different OmaChat account")
                            .arg(providerName));
                    return;
                }
                replyOk(*sp, rid); // already linked to this same account: nothing to do
                return;
            }
            const OAuthIdentityRecord identity{
                m_ids.next(), userId, providerName, profile->id, profile->username, now()};
            if (!m_store.insertOAuthIdentity(identity)) {
                replyError(*sp, rid, proto::ERROR_INTERNAL, QStringLiteral("could not link your account"));
                return;
            }
            OMA_INFO("oauth", "linked", {"provider", providerName}, {"user", userId});
            replyOk(*sp, rid);
        });
}

void ChatServer::handleOAuthUnlink(Session& s, std::uint64_t rid, const proto::OAuthUnlinkRequest& m)
{
    const auto* meta = auth::metaFor(m.provider());
    if (!meta) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("unknown sign-in provider"));
        return;
    }
    const auto identities = m_store.oauthIdentitiesForUser(s.userId);
    const bool linked = std::any_of(
        identities.begin(), identities.end(), [&](const auto& i) { return i.provider == meta->name; });
    if (!linked) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("that provider is not linked to your account"));
        return;
    }
    // Never leave an account with no way to sign back in.
    if (identities.size() <= 1 && !m_store.hasPassword(s.userId)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("set a password or link another sign-in method first, so you don't lose access"));
        return;
    }
    if (!m_store.deleteOAuthIdentity(s.userId, meta->name)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not unlink"));
        return;
    }
    OMA_INFO("oauth", "unlinked", {"provider", meta->name}, {"user", s.userId});
    replyOk(s, rid);
}

void ChatServer::handleListOAuthIdentities(Session& s, std::uint64_t rid)
{
    proto::Envelope env;
    auto* list = env.mutable_oauth_identity_list();
    for (const auto& identity : m_store.oauthIdentitiesForUser(s.userId)) {
        const auto* meta = auth::metaByName(identity.provider);
        if (!meta)
            continue;
        auto* out = list->add_identities();
        out->set_provider(meta->provider);
        out->set_provider_username(identity.providerUsername.toStdString());
        out->set_linked_at(identity.linkedAt);
    }
    reply(s, rid, std::move(env));
}

} // namespace omachat::server
