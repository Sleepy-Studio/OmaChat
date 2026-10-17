#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace omachat::portable {
inline constexpr std::size_t kKeyBytes = 32;
inline constexpr std::size_t kMaxBody = 1024 * 1024;
inline constexpr std::size_t kMaxDevices = 256;
struct Identity {
    std::array<std::uint8_t, 32> publicKey{}, secretKey{};
    ~Identity();
    static Identity generate();
    static std::optional<Identity> fromSecret(const std::string& secret);
    std::string publicBytes() const;
};
struct Context {
    std::uint64_t channelId = 0, authorId = 0;
};
struct Wrap {
    std::string recipient, nonce, box;
};
struct Payload {
    std::string sender, nonce, ciphertext;
    std::vector<Wrap> wraps;
};
// Serialized protobuf bodies remain owned by the caller; no Qt/protobuf/JNI here.
std::optional<Payload> seal(
    const std::string& body, Context ctx, const Identity& sender, const std::vector<std::string>& recipients);
std::optional<std::string> open(const Payload& payload, Context ctx, const Identity& me);
std::string safetyNumber(
    std::uint64_t userA, std::vector<std::string> keysA, std::uint64_t userB, std::vector<std::string> keysB);
} // namespace omachat::portable
