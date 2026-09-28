#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace omachat::server::auth {

// Argon2id via libsodium's crypto_pwhash_str (self-describing PHC string).
// These are CPU/memory heavy (~64 MiB, tens of ms): call them off the event
// loop thread.
std::optional<QString> hashPassword(const QString& password);
bool verifyPassword(const QString& password, const QString& encodedHash);

// A hash computed with the dummy password, used to equalize timing when a
// login names a user that does not exist.
const QString& dummyHash();

// 256-bit random token encoded as unpadded base64url.
QString randomToken();

// SHA-256 of the token, used as the at-rest lookup key for refresh tokens.
QByteArray tokenDigest(const QString& token);

// Short, URL-safe invite token (~72 bits of entropy).
QString inviteToken();

} // namespace omachat::server::auth
