#pragma once

#include "config/ServerConfig.hpp"
#include "network.pb.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace omachat::server::auth {

// Static metadata for a supported OAuth login provider: where to send the
// user to authorize, where the server exchanges the code, and where it reads
// back a profile once it has an access token.
struct OAuthProviderMeta {
    proto::OAuthProvider provider = proto::OAUTH_PROVIDER_UNSPECIFIED;
    QString name; // lowercase, stable: matches OAuthIdentityRecord::provider
    QString authorizeUrl;
    QString tokenUrl;
    QString profileUrl;
    QString scope;
};

// Returns nullptr if `provider` is not one OmaChat supports.
const OAuthProviderMeta* metaFor(proto::OAuthProvider provider);
const OAuthProviderMeta* metaByName(const QString& name);

// A profile pulled from the provider after a successful token exchange.
// `id` is the provider's own stable user id: never re-derive identity from
// a mutable field like username or email.
struct OAuthProfile {
    QString id;
    QString username;
};

// Exchanges an authorization code for an access token, then fetches the
// provider profile. Runs entirely over HTTPS via QNetworkAccessManager;
// `done` is invoked on the calling thread's event loop, exactly once.
// `code_verifier`/`redirect_uri` must match what the client sent the
// provider during authorization (PKCE, RFC 7636).
void exchangeAndFetchProfile(QNetworkAccessManager& net, const OAuthProviderMeta& meta,
    const OAuthProviderSettings& settings, const QString& code, const QString& codeVerifier,
    const QString& redirectUri, std::function<void(std::optional<OAuthProfile>, QString error)> done);

} // namespace omachat::server::auth
