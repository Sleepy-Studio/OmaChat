#pragma once

#include "network.pb.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace omachat::e2e {

// End-to-end encryption for direct and group conversations (libsodium).
//
//   body       = E2EBody (text and attachment keys)
//   K          = random 32-byte content key
//   ciphertext = XChaCha20-Poly1305(K, nonce, body,
//                  ad = "omachat-e2e-v1" | channel id | author id | sender key)
//   per device = crypto_box(K | BLAKE2b-256(nonce | ciphertext), recipient, sender)
//
// crypto_box authenticates the sending device to each recipient; the hash
// in the box stops a participant (who learns K) from swapping the body under
// someone else's key wraps. There is no forward secrecy: a stolen device key
// opens every message ever sent to that device.

inline constexpr std::size_t kKeyBytes = 32;

struct Identity {
    std::array<std::uint8_t, kKeyBytes> publicKey{};
    std::array<std::uint8_t, kKeyBytes> secretKey{};

    static Identity generate();
    // Rebuilds the pair from a stored secret key.
    static std::optional<Identity> fromSecret(const QByteArray& secret);
    QByteArray publicBytes() const;
    QByteArray secretBytes() const;
};

struct Context {
    std::uint64_t channelId = 0;
    std::uint64_t authorId = 0;
};

// Seals `body` for every device key in `recipients` (duplicates ignored;
// include the sender's own key to read your sent messages later).
std::optional<std::string> seal(
    const proto::E2EBody& body, const Context& ctx, const Identity& sender, const std::vector<QByteArray>& recipients);

struct Opened {
    proto::E2EBody body;
    QByteArray senderKey; // the caller checks it belongs to the author
};
// nullopt: not addressed to this device, tampered with, or wrong context.
std::optional<Opened> open(const std::string& payload, const Context& ctx, const Identity& me);

// Attachments: crypto_secretstream in 64 KiB chunks, header first. The key
// travels inside the sealed body.
bool encryptFile(const QString& in, const QString& out, QByteArray* key, QString* error);
bool decryptFile(const QString& in, const QString& out, const QByteArray& key, QString* error);
std::uint64_t encryptedSize(std::uint64_t plainSize);

// Twelve groups of five digits that both people see identically when their
// devices hold the keys they think they do. Order of arguments is irrelevant.
QString safetyNumber(
    std::uint64_t userA, std::vector<QByteArray> keysA, std::uint64_t userB, std::vector<QByteArray> keysB);

} // namespace omachat::e2e
