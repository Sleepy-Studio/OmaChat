#include "omachat/core/Permissions.hpp"

#include <array>
#include <utility>

namespace omachat::permissions {
namespace {

constexpr std::array<std::pair<Bits, std::string_view>, 21> kNames{{
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
    {ManageEmoji, "MANAGE_EMOJI"},
    {Administrator, "ADMINISTRATOR"},
}};

Bits apply(Bits base, Override o)
{
    return (base & ~o.deny) | o.allow;
}

} // namespace

namespace {
Bits evaluate(const ResolveInput& input, Explanation* result)
{
    if (result)
        for (const auto& [bit, n] : kNames) {
            (void)n;
            result->permissions.push_back({bit, false, "not_granted", {}});
        }
    auto mark = [&](Bits touched, std::string_view source) {
        if (!result)
            return;
        for (auto& entry : result->permissions)
            if (entry.permission & touched) {
                entry.source = source;
                entry.roles.clear();
            }
    };
    auto contributors = [&](Bits touched, const auto& ids, const auto& masks) {
        if (!result)
            return;
        for (auto& entry : result->permissions) {
            if (!(entry.permission & touched))
                continue;
            for (std::size_t i = 0; i < ids.size() && i < masks.size(); ++i)
                if (ids[i] && (masks[i] & entry.permission))
                    entry.roles.push_back(ids[i]);
        }
    };
    Bits base = input.defaultRole;
    mark(base, "default_role");
    for (Bits role : input.memberRoles) {
        base |= role;
        mark(role, "member_roles");
    }
    if (result) {
        for (auto& entry : result->permissions) {
            if ((input.defaultRole & entry.permission) && input.defaultRoleId)
                entry.roles.push_back(input.defaultRoleId);
        }
        contributors(kAll, input.memberRoleIds, input.memberRoles);
    }
    if (input.isOwner || (base & Administrator)) {
        base = kAll;
        mark(kAll, input.isOwner ? "owner" : "administrator");
        if (result && !input.isOwner) {
            for (auto& entry : result->permissions) {
                if ((input.defaultRole & Administrator) && input.defaultRoleId)
                    entry.roles.push_back(input.defaultRoleId);
                for (std::size_t i = 0; i < input.memberRoleIds.size() && i < input.memberRoles.size(); ++i)
                    if (input.memberRoles[i] & Administrator)
                        entry.roles.push_back(input.memberRoleIds[i]);
            }
        }
    } else if (input.channelScope) {
        auto applyLayer = [&](Override o, std::string_view source) {
            base = apply(base, o);
            mark(o.allow | o.deny, source);
        };
        for (const auto& layer : input.layers) {
            applyLayer(layer.defaultRole, layer.category ? "category_everyone" : "channel_everyone");
            if (result && input.defaultRoleId)
                for (auto& entry : result->permissions)
                    if (entry.permission & (layer.defaultRole.allow | layer.defaultRole.deny))
                        entry.roles.push_back(input.defaultRoleId);
            Override combined;
            for (const auto& o : layer.memberRoles) {
                combined.allow |= o.allow;
                combined.deny |= o.deny;
            }
            applyLayer(combined, layer.category ? "category_roles" : "channel_roles");
            if (result)
                for (auto& entry : result->permissions)
                    for (std::size_t i = 0; i < layer.memberRoleIds.size() && i < layer.memberRoles.size(); ++i) {
                        const auto& o = layer.memberRoles[i];
                        const Bits winning = (combined.allow & entry.permission) ? o.allow : o.deny;
                        if ((combined.allow | combined.deny) & entry.permission)
                            if (winning & entry.permission)
                                entry.roles.push_back(layer.memberRoleIds[i]);
                    }
            applyLayer(layer.user, layer.category ? "category_user" : "channel_user");
        }
        if (!(base & ViewChannel)) {
            base = 0;
            // Retain the ViewChannel denial's source; other permissions are
            // blocked by the visibility gate regardless of their earlier grants.
            mark(kAll & ~ViewChannel, "view_channel_required");
        } else {
            base &= ~Administrator;
            mark(Administrator, "server_roles_only");
        }
    }
    base &= kAll;
    if (result) {
        result->effective = base;
        for (auto& entry : result->permissions)
            entry.allowed = has(base, entry.permission);
    }
    return base;
}
} // namespace

Explanation explain(const ResolveInput& input)
{
    Explanation result;
    evaluate(input, &result);
    return result;
}

Bits resolve(const ResolveInput& input)
{
    return evaluate(input, nullptr);
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
