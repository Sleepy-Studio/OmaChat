#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace omachat::permissions {

using Bits = std::uint64_t;

// Permission bits. Values are part of the wire protocol: never renumber.
enum Permission : Bits {
    ViewChannel = Bits{1} << 0,
    SendMessages = Bits{1} << 1,
    ManageMessages = Bits{1} << 2,
    ReadHistory = Bits{1} << 3,
    ConnectVoice = Bits{1} << 4,
    Speak = Bits{1} << 5,
    Stream = Bits{1} << 6,
    CreateInvites = Bits{1} << 7,
    KickMembers = Bits{1} << 8,
    BanMembers = Bits{1} << 9,
    CreateChannel = Bits{1} << 10,
    ManageChannel = Bits{1} << 11,
    ManageRoles = Bits{1} << 12,
    ManageServer = Bits{1} << 13,
    MuteMembers = Bits{1} << 14,
    MoveMembers = Bits{1} << 15,
    PrioritySpeaker = Bits{1} << 16,
    AttachFiles = Bits{1} << 17,
    AddReactions = Bits{1} << 18,
    ManageEmoji = Bits{1} << 19,
    Administrator = Bits{1} << 40,
};

inline constexpr Bits kAll = ((Bits{1} << 20) - 1) | Administrator;

// Built-in role presets created with every new server.
inline constexpr Bits kGuestDefaults = ViewChannel | ReadHistory | ConnectVoice;
inline constexpr Bits kMemberDefaults = ViewChannel | SendMessages | ReadHistory | ConnectVoice | Speak | Stream
    | CreateInvites | AttachFiles | AddReactions;
inline constexpr Bits kModeratorDefaults = kMemberDefaults | ManageMessages | KickMembers | BanMembers | MuteMembers
    | MoveMembers | PrioritySpeaker | ManageEmoji;
inline constexpr Bits kAdminDefaults
    = kModeratorDefaults | Administrator | CreateChannel | ManageChannel | ManageRoles | ManageServer;

struct Override {
    Bits allow = 0;
    Bits deny = 0;
};

// Inputs required to compute a member's effective permissions. Everything is
// explicit so the resolver stays a pure function that is trivial to test.
struct ResolveInput {
    bool isOwner = false;
    Bits defaultRole = 0; // the implicit "everyone" role
    std::span<const Bits> memberRoles; // roles explicitly assigned

    // Channel scope. Leave empty to compute server-level permissions.
    // Each layer is applied in order: category first, then the channel.
    struct Layer {
        Override defaultRole; // override targeting the default role
        std::vector<Override> memberRoles; // overrides targeting the member's roles
        Override user; // override targeting this user
    };
    std::span<const Layer> layers;
    bool channelScope = false;
};

// Precedence (documented in docs/security.md):
//  1. The server owner has every permission.
//  2. base = defaultRole | OR(memberRoles)
//  3. Administrator in base grants every permission; overrides are skipped.
//  4. For each layer (category, then channel):
//       base = (base & ~default.deny) | default.allow
//       base = (base & ~OR(role.deny)) | OR(role.allow)   (role allow beats role deny)
//       base = (base & ~user.deny) | user.allow           (user override wins last)
//  5. In channel scope, lacking ViewChannel yields no permissions at all.
Bits resolve(const ResolveInput& input);

inline constexpr bool has(Bits set, Bits required)
{
    return (set & required) == required;
}

std::string_view name(Bits single);
std::vector<std::string_view> names(Bits set);
Bits fromName(std::string_view name); // 0 if unknown

} // namespace omachat::permissions
