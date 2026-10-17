#include "MessageCrypto.hpp"
#include <algorithm>
#include <cstring>
#include <sodium.h>
#include <stdexcept>
namespace omachat::portable {
namespace {

constexpr char kDomain[] = "omachat-e2e-v1";
constexpr std::size_t kHashBytes = 32;
struct Wipe {
    void* data;
    std::size_t size;
    ~Wipe() { sodium_memzero(data, size); }
};

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
    if (sodium_init() < 0)
        throw std::runtime_error("sodium initialization failed");
    crypto_box_keypair(id.publicKey.data(), id.secretKey.data());
    return id;
}

std::optional<Identity> Identity::fromSecret(const std::string& secret)
{
    if (sodium_init() < 0 || secret.size() != kKeyBytes)
        return std::nullopt;
    Identity id;
    std::memcpy(id.secretKey.data(), secret.data(), kKeyBytes);
    if (crypto_scalarmult_base(id.publicKey.data(), id.secretKey.data()) != 0)
        return std::nullopt;
    return id;
}

std::string Identity::publicBytes() const
{
    return std::string(reinterpret_cast<const char*>(publicKey.data()), kKeyBytes);
}

Identity::~Identity()
{
    sodium_memzero(secretKey.data(), secretKey.size());
}

std::optional<Payload> seal(
    const std::string& body, Context ctx, const Identity& sender, const std::vector<std::string>& recipients)
{
    if (recipients.empty() || recipients.size() > kMaxDevices || body.size() > kMaxBody || sodium_init() < 0)
        return std::nullopt;
    std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key{};
    Wipe wipeKey{key.data(), key.size()};
    crypto_aead_xchacha20poly1305_ietf_keygen(key.data());

    Payload p;
    p.sender = sender.publicBytes();
    std::string nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, '\0');
    randombytes_buf(nonce.data(), nonce.size());
    p.nonce = nonce;

    const std::string& plain = body;
    const std::string ad = associatedData(ctx, p.sender);
    std::string ct(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long ctLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char*>(ct.data()), &ctLen,
        reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(), nullptr,
        reinterpret_cast<const unsigned char*>(nonce.data()), key.data());
    ct.resize(ctLen);
    p.ciphertext = ct;

    // What each device receives: the content key and the body's hash.
    std::array<std::uint8_t, kKeyBytes + kHashBytes> secret{};
    Wipe wipeSecret{secret.data(), secret.size()};
    std::memcpy(secret.data(), key.data(), kKeyBytes);
    const auto hash = bodyHash(nonce, ct);
    std::memcpy(secret.data() + kKeyBytes, hash.data(), kHashBytes);

    std::vector<std::string> seen;
    for (const std::string& r : recipients) {
        if (r.size() != kKeyBytes || std::ranges::find(seen, r) != seen.end())
            continue;
        seen.push_back(r);
        Wrap w;
        w.recipient = r;
        std::string wn(crypto_box_NONCEBYTES, '\0');
        randombytes_buf(wn.data(), wn.size());
        w.nonce = wn;
        std::string box(secret.size() + crypto_box_MACBYTES, '\0');
        if (crypto_box_easy(reinterpret_cast<unsigned char*>(box.data()), secret.data(), secret.size(),
                reinterpret_cast<const unsigned char*>(wn.data()), reinterpret_cast<const unsigned char*>(r.data()),
                sender.secretKey.data())
            != 0)
            return std::nullopt;
        w.box = box;
        p.wraps.push_back(std::move(w));
    }
    sodium_memzero(key.data(), key.size());
    sodium_memzero(secret.data(), secret.size());
    if (p.wraps.empty())
        return std::nullopt;
    return p;
}

std::optional<std::string> open(const Payload& p, Context ctx, const Identity& me)
{
    if (sodium_init() < 0 || p.sender.size() != kKeyBytes || p.nonce.size() != 24 || p.ciphertext.size() < 16
        || p.ciphertext.size() > kMaxBody + 16 || p.wraps.empty() || p.wraps.size() > kMaxDevices)
        return std::nullopt;
    const std::string mine = me.publicBytes();
    const auto wrap = std::ranges::find_if(p.wraps, [&](const Wrap& w) { return w.recipient == mine; });
    if (wrap == p.wraps.end() || wrap->nonce.size() != crypto_box_NONCEBYTES
        || wrap->box.size() != kKeyBytes + kHashBytes + crypto_box_MACBYTES)
        return std::nullopt;

    std::array<std::uint8_t, kKeyBytes + kHashBytes> secret{};
    Wipe wipeSecret{secret.data(), secret.size()};
    if (crypto_box_open_easy(secret.data(), reinterpret_cast<const unsigned char*>(wrap->box.data()), wrap->box.size(),
            reinterpret_cast<const unsigned char*>(wrap->nonce.data()),
            reinterpret_cast<const unsigned char*>(p.sender.data()), me.secretKey.data())
        != 0)
        return std::nullopt;
    const auto hash = bodyHash(p.nonce, p.ciphertext);
    if (sodium_memcmp(secret.data() + kKeyBytes, hash.data(), kHashBytes) != 0) {
        sodium_memzero(secret.data(), secret.size());
        return std::nullopt; // the body was swapped under genuine key wraps
    }
    const std::string ad = associatedData(ctx, p.sender);
    std::string plain(p.ciphertext.size(), '\0');
    unsigned long long plainLen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &plainLen,
        nullptr, reinterpret_cast<const unsigned char*>(p.ciphertext.data()), p.ciphertext.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        reinterpret_cast<const unsigned char*>(p.nonce.data()), secret.data());
    sodium_memzero(secret.data(), secret.size());
    if (rc != 0) {
        sodium_memzero(plain.data(), plain.size());
        return std::nullopt;
    }
    plain.resize(plainLen);
    return plain;
}

std::string safetyNumber(
    std::uint64_t userA, std::vector<std::string> keysA, std::uint64_t userB, std::vector<std::string> keysB)
{
    auto half = [](std::uint64_t user, std::vector<std::string> keys) {
        if (keys.size() > kMaxDevices)
            throw std::runtime_error("too many keys");
        std::ranges::sort(keys);
        std::string input("omachat-safety-v1");
        put64(input, user);
        for (const auto& key : keys) {
            if (key.size() != 32)
                throw std::runtime_error("invalid key");
            input += key;
        }
        std::array<unsigned char, 32> h{};
        if (sodium_init() < 0)
            throw std::runtime_error("sodium initialization failed");
        crypto_generichash(
            h.data(), h.size(), reinterpret_cast<const unsigned char*>(input.data()), input.size(), nullptr, 0);
        for (int i = 0; i < 4096; ++i)
            crypto_generichash(h.data(), h.size(), h.data(), h.size(), nullptr, 0);
        std::string result;
        for (int g = 0; g < 6; ++g) {
            std::uint64_t v = 0;
            for (int b = 0; b < 5; ++b)
                v = (v << 8) | h[static_cast<std::size_t>(g * 5 + b)];
            auto digits = std::to_string(v % 100000);
            if (g)
                result += ' ';
            result += std::string(5 - digits.size(), '0') + digits;
        }
        return result;
    };
    auto a = half(userA, std::move(keysA)), b = half(userB, std::move(keysB));
    if (userB < userA)
        std::swap(a, b);
    return a + " " + b;
}
} // namespace omachat::portable
