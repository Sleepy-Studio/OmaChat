#include "omachat/core/Permissions.hpp"

#include <array>
#include <utility>

namespace omachat::permissions {
namespace {

constexpr std::array<std::pair<Bits, std::string_view>, 20> kNames{{
    {ViewChannel, "VIEW_CHANNEL"},
    {SendMessages, "SEND_MESSAGES"},
    {ManageMessages, "MANAGE_MESSAGES"},
    {ReadHistory, "READ_HISTORY"},
    {ConnectVoice, "CONNECT_VOICE"},
    {Speak, "SPEAK"},
    {Stream, "STREAM"},
    {CreateInvites, "CREATE_INVITES"},
    {KickMembers, "KICK_MEMBERS"},
    {BanMembers, "BAN_MEMBERS"},
    {CreateChannel, "CREATE_CHANNEL"},
    {ManageChannel, "MANAGE_CHANNEL"},
    {ManageRoles, "MANAGE_ROLES"},
    {ManageServer, "MANAGE_SERVER"},
    {MuteMembers, "MUTE_MEMBERS"},
    {MoveMembers, "MOVE_MEMBERS"},
    {PrioritySpeaker, "PRIORITY_SPEAKER"},
    {AttachFiles, "ATTACH_FILES"},
    {AddReactions, "ADD_REACTIONS"},
    {Administrator, "ADMINISTRATOR"},
}};

Bits apply(Bits base, Override o)
{
    return (base & ~o.deny) | o.allow;
}

} // namespace

Bits resolve(const ResolveInput& input)
{
    if (input.isOwner)
        return kAll;

    Bits base = input.defaultRole;
    for (Bits role : input.memberRoles)
        base |= role;

    if (base & Administrator)
        return kAll;

    if (!input.channelScope)
        return base & kAll;

    for (const auto& layer : input.layers) {
        base = apply(base, layer.defaultRole);
        Override combined;
        for (const Override& o : layer.memberRoles) {
            combined.allow |= o.allow;
            combined.deny |= o.deny;
        }
        base = apply(base, combined);
        base = apply(base, layer.user);
    }

    if (!(base & ViewChannel))
        return 0;
    // Administrator can only come from roles, never from a channel override.
    return base & kAll & ~Administrator;
}

std::string_view name(Bits single)
{
    for (const auto& [bit, n] : kNames) {
        if (bit == single)
            return n;
    }
    return "UNKNOWN";
}

std::vector<std::string_view> names(Bits set)
{
    std::vector<std::string_view> out;
    for (const auto& [bit, n] : kNames) {
        if (set & bit)
            out.push_back(n);
    }
    return out;
}

Bits fromName(std::string_view n)
{
    for (const auto& [bit, candidate] : kNames) {
        if (candidate == n)
            return bit;
    }
    return 0;
}

} // namespace omachat::permissions
