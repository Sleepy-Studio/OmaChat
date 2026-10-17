#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace omachat::portable {
// Matches desktop secretstream framing: header + 64 KiB chunks + final tag.
// Caller supplies private temporary paths. Partial outputs are removed on failure.
inline constexpr std::uint64_t kMaxFileBytes = 4096ULL * 1024 * 1024;
std::optional<std::string> encryptFile(
    const std::string& input, const std::string& output, std::uint64_t limit = kMaxFileBytes);
bool decryptFile(
    const std::string& input, const std::string& output, const std::string& key, std::uint64_t limit = kMaxFileBytes);
} // namespace omachat::portable
