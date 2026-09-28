#include "auth/Credentials.hpp"

#include <sodium.h>

#include <array>

namespace omachat::server::auth {

std::optional<QString> hashPassword(const QString& password)
{
    const QByteArray pw = password.toUtf8();
    std::array<char, crypto_pwhash_STRBYTES> out{};
    if (crypto_pwhash_str_alg(out.data(), pw.constData(), static_cast<unsigned long long>(pw.size()),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13)
        != 0)
        return std::nullopt; // out of memory
    return QString::fromLatin1(out.data());
}

bool verifyPassword(const QString& password, const QString& encodedHash)
{
    const QByteArray pw = password.toUtf8();
    const QByteArray hash = encodedHash.toLatin1();
    if (!hash.startsWith("$argon2id$"))
        return false;
    return crypto_pwhash_str_verify(hash.constData(), pw.constData(), static_cast<unsigned long long>(pw.size())) == 0;
}

const QString& dummyHash()
{
    static const QString hash = hashPassword(QStringLiteral("omachat-timing-equalizer")).value_or(QString());
    return hash;
}

QString randomToken()
{
    std::array<unsigned char, 32> raw{};
    randombytes_buf(raw.data(), raw.size());
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(raw.data()), raw.size())
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray tokenDigest(const QString& token)
{
    const QByteArray t = token.toLatin1();
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(t.constData()),
        static_cast<unsigned long long>(t.size()));
    return QByteArray(reinterpret_cast<const char*>(digest.data()), digest.size());
}

QString inviteToken()
{
    std::array<unsigned char, 9> raw{};
    randombytes_buf(raw.data(), raw.size());
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(raw.data()), raw.size())
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace omachat::server::auth
