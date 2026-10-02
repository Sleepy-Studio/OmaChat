#include "crypto/E2E.hpp"
#include "omachat/crypto/FileCrypto.hpp"
#include "omachat/crypto/MessageCrypto.hpp"

#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace omachat::e2e {

namespace {
constexpr std::size_t kChunk = 64 * 1024;
portable::Identity nativeIdentity(const Identity& id)
{
    portable::Identity out;
    out.publicKey = id.publicKey;
    out.secretKey = id.secretKey;
    return out;
}
} // namespace
Identity Identity::generate()
{
    Identity id;
    crypto_box_keypair(id.publicKey.data(), id.secretKey.data());
    return id;
}

std::optional<Identity> Identity::fromSecret(const QByteArray& secret)
{
    if (secret.size() != static_cast<qsizetype>(kKeyBytes))
        return std::nullopt;
    Identity id;
    std::memcpy(id.secretKey.data(), secret.constData(), kKeyBytes);
    if (crypto_scalarmult_base(id.publicKey.data(), id.secretKey.data()) != 0)
        return std::nullopt;
    return id;
}

QByteArray Identity::publicBytes() const
{
    return QByteArray(reinterpret_cast<const char*>(publicKey.data()), kKeyBytes);
}

QByteArray Identity::secretBytes() const
{
    return QByteArray(reinterpret_cast<const char*>(secretKey.data()), kKeyBytes);
}

std::optional<std::string> seal(
    const proto::E2EBody& body, const Context& ctx, const Identity& sender, const std::vector<QByteArray>& recipients)
{
    std::vector<std::string> keys;
    for (const auto& key : recipients)
        keys.push_back(key.toStdString());
    const auto sealed
        = portable::seal(body.SerializeAsString(), {ctx.channelId, ctx.authorId}, nativeIdentity(sender), keys);
    if (!sealed)
        return std::nullopt;
    proto::E2EPayload p;
    p.set_version(1);
    p.set_sender_key(sealed->sender);
    p.set_nonce(sealed->nonce);
    p.set_ciphertext(sealed->ciphertext);
    for (const auto& wrap : sealed->wraps) {
        auto* w = p.add_wraps();
        w->set_recipient_key(wrap.recipient);
        w->set_nonce(wrap.nonce);
        w->set_box(wrap.box);
    }
    return p.SerializeAsString();
}
std::optional<Opened> open(const std::string& payload, const Context& ctx, const Identity& me)
{
    if (payload.size() > portable::kMaxBody + 32768)
        return std::nullopt;
    proto::E2EPayload p;
    if (!p.ParseFromString(payload) || p.version() != 1)
        return std::nullopt;
    portable::Payload wire{p.sender_key(), p.nonce(), p.ciphertext(), {}};
    for (const auto& w : p.wraps())
        wire.wraps.push_back({w.recipient_key(), w.nonce(), w.box()});
    auto plain = portable::open(wire, {ctx.channelId, ctx.authorId}, nativeIdentity(me));
    if (!plain)
        return std::nullopt;
    Opened out;
    const bool parsed = out.body.ParseFromString(*plain);
    sodium_memzero(plain->data(), plain->size());
    if (!parsed)
        return std::nullopt;
    out.senderKey = QByteArray::fromStdString(p.sender_key());
    return out;
}

std::uint64_t encryptedSize(std::uint64_t plainSize)
{
    const std::uint64_t chunks = std::max<std::uint64_t>(1, (plainSize + kChunk - 1) / kChunk);
    return crypto_secretstream_xchacha20poly1305_HEADERBYTES + plainSize
        + chunks * crypto_secretstream_xchacha20poly1305_ABYTES;
}

bool encryptFile(const QString& in, const QString& out, QByteArray* key, QString* error)
{
    // Keep Qt's atomic destination replacement. The portable core only sees a
    // private staging file; failure must not truncate an existing destination.
    QTemporaryFile staging(out + QStringLiteral(".XXXXXX"));
    if (!staging.open()) {
        *error = staging.errorString();
        return false;
    }
    staging.close();
    auto generated = portable::encryptFile(
        QFile::encodeName(in).toStdString(), QFile::encodeName(staging.fileName()).toStdString());
    if (!generated) {
        *error = QStringLiteral("could not encrypt the attachment");
        return false;
    }
    QSaveFile destination(out);
    QFile source(staging.fileName());
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        sodium_memzero(generated->data(), generated->size());
        *error = QStringLiteral("could not open attachment output");
        return false;
    }
    while (!source.atEnd()) {
        auto bytes = source.read(65536);
        if (bytes.isEmpty() || destination.write(bytes) != bytes.size()) {
            sodium_memzero(generated->data(), generated->size());
            *error = QStringLiteral("could not write attachment output");
            return false;
        }
    }
    if (!destination.commit()) {
        sodium_memzero(generated->data(), generated->size());
        *error = destination.errorString();
        return false;
    }
    *key = QByteArray::fromStdString(*generated);
    sodium_memzero(generated->data(), generated->size());
    return true;
}
bool decryptFile(const QString& in, const QString& out, const QByteArray& key, QString* error)
{
    QTemporaryFile staging(out + QStringLiteral(".XXXXXX"));
    if (!staging.open()) {
        *error = staging.errorString();
        return false;
    }
    staging.close();
    auto secret = key.toStdString();
    const bool decrypted = portable::decryptFile(
        QFile::encodeName(in).toStdString(), QFile::encodeName(staging.fileName()).toStdString(), secret);
    sodium_memzero(secret.data(), secret.size());
    if (!decrypted) {
        *error = QStringLiteral("the file was altered, incomplete, too large, or has the wrong key");
        return false;
    }
    QSaveFile destination(out);
    QFile source(staging.fileName());
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("could not open attachment output");
        return false;
    }
    while (!source.atEnd()) {
        auto bytes = source.read(65536);
        if (bytes.isEmpty() || destination.write(bytes) != bytes.size()) {
            *error = QStringLiteral("could not write attachment output");
            return false;
        }
    }
    if (!destination.commit()) {
        *error = destination.errorString();
        return false;
    }
    return true;
}

QString safetyNumber(
    std::uint64_t userA, std::vector<QByteArray> keysA, std::uint64_t userB, std::vector<QByteArray> keysB)
{
    std::vector<std::string> a, b;
    for (const auto& key : keysA)
        a.push_back(key.toStdString());
    for (const auto& key : keysB)
        b.push_back(key.toStdString());
    return QString::fromStdString(portable::safetyNumber(userA, std::move(a), userB, std::move(b)));
}

} // namespace omachat::e2e
