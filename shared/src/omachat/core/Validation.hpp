#pragma once

#include <QString>

#include <optional>

namespace omachat::validation {

inline constexpr int kMaxMessageLength = 4000;
inline constexpr int kMaxTopicLength = 512;
inline constexpr int kMinPasswordLength = 8;
inline constexpr int kMaxPasswordLength = 1024;

// Each normalizer returns the canonical form, or std::nullopt when the input
// can never be valid. Server-side code must validate every client-supplied
// field through these; clients use them only for early feedback.

// 2..32 chars of [a-z0-9_.-], lowercased; must start with a letter or digit.
std::optional<QString> username(const QString& input);

// 1..64 chars, trimmed, no control characters.
std::optional<QString> displayName(const QString& input);

// 1..100 chars, trimmed, no control characters.
std::optional<QString> serverName(const QString& input);

// 1..64 chars. Text/voice channel names are trimmed; control chars rejected.
std::optional<QString> channelName(const QString& input);

// 1..32 chars, custom role name.
std::optional<QString> roleName(const QString& input);

// Message body: trimmed of trailing whitespace, 1..kMaxMessageLength chars,
// control characters other than \n and \t removed.
std::optional<QString> messageContent(const QString& input);

std::optional<QString> topic(const QString& input);

bool passwordAcceptable(const QString& password);

// Attachment filename: path separators become '_', control characters are
// dropped, 1..200 chars after trimming; "." and ".." are rejected.
std::optional<QString> filename(const QString& input);

// Lowercased "type/subtype" (parameters dropped). Anything malformed is
// reported as application/octet-stream rather than rejected.
QString mimeType(const QString& input);

// Short reaction token: 1..32 chars, no whitespace or control chars.
std::optional<QString> reaction(const QString& input);
std::optional<QString> emojiName(const QString& input); // custom emoji shortcode, e.g. "party_parrot"

} // namespace omachat::validation
