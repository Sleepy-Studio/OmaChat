#include "crypto/E2E.hpp"

#include <QFile>
#include <QSaveFile>

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace omachat::e2e {

namespace {

constexpr char kDomain[] = "omachat-e2e-v1";
constexpr std::size_t kHashBytes = 32;
constexpr std::size_t kChunk = 64 * 1024;

void put64(std::string& out, std::uint64_t v)
{
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
}

std::string associatedData(const Context& ctx, const std::string& senderKey)
{
    std::string ad(kDomain);
    put64(ad, ctx.channelId);
    put64(ad, ctx.authorId);
    ad += senderKey;
    return ad;
}

std::array<std::uint8_t, kHashBytes> bodyHash(const std::string& nonce, const std::string& ciphertext)
{
    std::array<std::uint8_t, kHashBytes> h{};
    crypto_generichash_state st;
    crypto_generichash_init(&st, nullptr, 0, h.size());
    crypto_generichash_update(&st, reinterpret_cast<const unsigned char*>(nonce.data()), nonce.size());
    crypto_generichash_update(&st, reinterpret_cast<const unsigned char*>(ciphertext.data()), ciphertext.size());
    crypto_generichash_final(&st, h.data(), h.size());
    return h;
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
    if (recipients.empty())
        return std::nullopt;
    std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key{};
    crypto_aead_xchacha20poly1305_ietf_keygen(key.data());

    proto::E2EPayload p;
    p.set_version(1);
    p.set_sender_key(sender.publicBytes().toStdString());
    std::string nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, '\0');
    randombytes_buf(nonce.data(), nonce.size());
    p.set_nonce(nonce);

    const std::string plain = body.SerializeAsString();
    const std::string ad = associatedData(ctx, p.sender_key());
    std::string ct(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long ctLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char*>(ct.data()), &ctLen,
        reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(), nullptr,
        reinterpret_cast<const unsigned char*>(nonce.data()), key.data());
    ct.resize(ctLen);
    p.set_ciphertext(ct);

    // What each device receives: the content key and the body's hash.
    std::array<std::uint8_t, kKeyBytes + kHashBytes> secret{};
    std::memcpy(secret.data(), key.data(), kKeyBytes);
    const auto hash = bodyHash(nonce, ct);
    std::memcpy(secret.data() + kKeyBytes, hash.data(), kHashBytes);

    std::vector<QByteArray> seen;
    for (const QByteArray& r : recipients) {
        if (r.size() != static_cast<qsizetype>(kKeyBytes) || std::ranges::find(seen, r) != seen.end())
            continue;
        seen.push_back(r);
        auto* w = p.add_wraps();
        w->set_recipient_key(r.toStdString());
        std::string wn(crypto_box_NONCEBYTES, '\0');
        randombytes_buf(wn.data(), wn.size());
        w->set_nonce(wn);
        std::string box(secret.size() + crypto_box_MACBYTES, '\0');
        if (crypto_box_easy(reinterpret_cast<unsigned char*>(box.data()), secret.data(), secret.size(),
                reinterpret_cast<const unsigned char*>(wn.data()),
                reinterpret_cast<const unsigned char*>(r.constData()), sender.secretKey.data())
            != 0)
            return std::nullopt;
        w->set_box(box);
    }
    sodium_memzero(key.data(), key.size());
    sodium_memzero(secret.data(), secret.size());
    if (p.wraps_size() == 0)
        return std::nullopt;
    return p.SerializeAsString();
}

std::optional<Opened> open(const std::string& payload, const Context& ctx, const Identity& me)
{
    proto::E2EPayload p;
    if (!p.ParseFromString(payload) || p.version() != 1 || p.sender_key().size() != kKeyBytes
        || p.nonce().size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
        || p.ciphertext().size() < crypto_aead_xchacha20poly1305_ietf_ABYTES)
        return std::nullopt;
    const std::string mine = me.publicBytes().toStdString();
    const auto wrap
        = std::ranges::find_if(p.wraps(), [&](const proto::E2EKeyWrap& w) { return w.recipient_key() == mine; });
    if (wrap == p.wraps().end() || wrap->nonce().size() != crypto_box_NONCEBYTES
        || wrap->box().size() != kKeyBytes + kHashBytes + crypto_box_MACBYTES)
        return std::nullopt;

    std::array<std::uint8_t, kKeyBytes + kHashBytes> secret{};
    if (crypto_box_open_easy(secret.data(), reinterpret_cast<const unsigned char*>(wrap->box().data()),
            wrap->box().size(), reinterpret_cast<const unsigned char*>(wrap->nonce().data()),
            reinterpret_cast<const unsigned char*>(p.sender_key().data()), me.secretKey.data())
        != 0)
        return std::nullopt;
    const auto hash = bodyHash(p.nonce(), p.ciphertext());
    if (sodium_memcmp(secret.data() + kKeyBytes, hash.data(), kHashBytes) != 0) {
        sodium_memzero(secret.data(), secret.size());
        return std::nullopt; // the body was swapped under genuine key wraps
    }
    const std::string ad = associatedData(ctx, p.sender_key());
    std::string plain(p.ciphertext().size(), '\0');
    unsigned long long plainLen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &plainLen,
        nullptr, reinterpret_cast<const unsigned char*>(p.ciphertext().data()), p.ciphertext().size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        reinterpret_cast<const unsigned char*>(p.nonce().data()), secret.data());
    sodium_memzero(secret.data(), secret.size());
    if (rc != 0)
        return std::nullopt;
    plain.resize(plainLen);
    Opened out;
    if (!out.body.ParseFromString(plain))
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

bool encryptFile(const QString& inPath, const QString& outPath, QByteArray* keyOut, QString* error)
{
    QFile in(inPath);
    QSaveFile out(outPath);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        *error = in.isOpen() ? out.errorString() : in.errorString();
        return false;
    }
    QByteArray key(crypto_secretstream_xchacha20poly1305_KEYBYTES, Qt::Uninitialized);
    crypto_secretstream_xchacha20poly1305_keygen(reinterpret_cast<unsigned char*>(key.data()));
    crypto_secretstream_xchacha20poly1305_state st;
    unsigned char header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
    crypto_secretstream_xchacha20poly1305_init_push(
        &st, header, reinterpret_cast<const unsigned char*>(key.constData()));
    out.write(reinterpret_cast<const char*>(header), sizeof header);
    std::vector<unsigned char> cbuf(kChunk + crypto_secretstream_xchacha20poly1305_ABYTES);
    const qint64 total = in.size();
    qint64 done = 0;
    do {
        const QByteArray chunk = in.read(static_cast<qint64>(kChunk));
        done += chunk.size();
        const bool last = done >= total || chunk.isEmpty();
        unsigned long long clen = 0;
        crypto_secretstream_xchacha20poly1305_push(&st, cbuf.data(), &clen,
            reinterpret_cast<const unsigned char*>(chunk.constData()), static_cast<unsigned long long>(chunk.size()),
            nullptr, 0,
            last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
        if (out.write(reinterpret_cast<const char*>(cbuf.data()), static_cast<qint64>(clen))
            != static_cast<qint64>(clen)) {
            *error = out.errorString();
            return false;
        }
        if (last)
            break;
    } while (true);
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }
    *keyOut = key;
    return true;
}

bool decryptFile(const QString& inPath, const QString& outPath, const QByteArray& key, QString* error)
{
    if (key.size() != static_cast<qsizetype>(crypto_secretstream_xchacha20poly1305_KEYBYTES)) {
        *error = QStringLiteral("missing file key");
        return false;
    }
    QFile in(inPath);
    QSaveFile out(outPath);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        *error = in.isOpen() ? out.errorString() : in.errorString();
        return false;
    }
    const QByteArray header = in.read(crypto_secretstream_xchacha20poly1305_HEADERBYTES);
    crypto_secretstream_xchacha20poly1305_state st;
    if (header.size() != static_cast<qsizetype>(crypto_secretstream_xchacha20poly1305_HEADERBYTES)
        || crypto_secretstream_xchacha20poly1305_init_pull(&st,
               reinterpret_cast<const unsigned char*>(header.constData()),
               reinterpret_cast<const unsigned char*>(key.constData()))
            != 0) {
        *error = QStringLiteral("the file is not an encrypted attachment");
        return false;
    }
    std::vector<unsigned char> pbuf(kChunk);
    for (;;) {
        const QByteArray chunk = in.read(static_cast<qint64>(kChunk + crypto_secretstream_xchacha20poly1305_ABYTES));
        unsigned long long plen = 0;
        unsigned char tag = 0;
        if (chunk.isEmpty()
            || crypto_secretstream_xchacha20poly1305_pull(&st, pbuf.data(), &plen, &tag,
                   reinterpret_cast<const unsigned char*>(chunk.constData()),
                   static_cast<unsigned long long>(chunk.size()), nullptr, 0)
                != 0) {
            *error = QStringLiteral("the file was altered or is incomplete");
            return false;
        }
        out.write(reinterpret_cast<const char*>(pbuf.data()), static_cast<qint64>(plen));
        if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
            if (!in.atEnd()) {
                *error = QStringLiteral("unexpected data after the end of the file");
                return false;
            }
            break;
        }
    }
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}

QString safetyNumber(
    std::uint64_t userA, std::vector<QByteArray> keysA, std::uint64_t userB, std::vector<QByteArray> keysB)
{
    auto half = [](std::uint64_t user, std::vector<QByteArray> keys) {
        std::ranges::sort(keys);
        std::string input("omachat-safety-v1");
        put64(input, user);
        for (const auto& k : keys)
            input.append(k.constData(), static_cast<std::size_t>(k.size()));
        std::array<std::uint8_t, 32> h{};
        crypto_generichash(
            h.data(), h.size(), reinterpret_cast<const unsigned char*>(input.data()), input.size(), nullptr, 0);
        // Iterate so guessing keys that give a chosen number is expensive.
        for (int i = 0; i < 4096; ++i)
            crypto_generichash(h.data(), h.size(), h.data(), h.size(), nullptr, 0);
        QStringList groups;
        for (int g = 0; g < 6; ++g) {
            std::uint64_t v = 0;
            for (int b = 0; b < 5; ++b)
                v = (v << 8) | h[static_cast<std::size_t>(g * 5 + b)];
            groups << QStringLiteral("%1").arg(v % 100000, 5, 10, QLatin1Char('0'));
        }
        return groups;
    };
    QStringList a = half(userA, std::move(keysA)), b = half(userB, std::move(keysB));
    if (userB < userA)
        std::swap(a, b);
    return (a + b).join(u' ');
}

} // namespace omachat::e2e
