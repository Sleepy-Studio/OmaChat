#include "omachat/core/Permissions.hpp"

#include <gtest/gtest.h>

using namespace omachat::permissions;

namespace {
Bits resolveServer(bool owner, Bits defaults, std::vector<Bits> roles)
{
    ResolveInput in;
    in.isOwner = owner;
    in.defaultRole = defaults;
    in.memberRoles = roles;
    return resolve(in);
}

Bits resolveChannel(Bits defaults, std::vector<Bits> roles, std::vector<ResolveInput::Layer> layers)
{
    ResolveInput in;
    in.defaultRole = defaults;
    in.memberRoles = roles;
    in.layers = layers;
    in.channelScope = true;
    return resolve(in);
}
} // namespace

TEST(Permissions, OwnerHasEverything)
{
    EXPECT_EQ(resolveServer(true, 0, {}), kAll);
}

TEST(Permissions, RolesAreUnioned)
{
    const Bits p = resolveServer(false, ViewChannel, {SendMessages, Speak});
    EXPECT_TRUE(has(p, ViewChannel | SendMessages | Speak));
    EXPECT_FALSE(has(p, KickMembers));
}

TEST(Permissions, AdministratorGrantsAllAndSkipsOverrides)
{
    ResolveInput::Layer deny;
    deny.defaultRole.deny = ViewChannel | SendMessages;
    EXPECT_EQ(resolveChannel(0, {Administrator}, {deny}), kAll);
}

TEST(Permissions, ChannelDenyOnDefaultRole)
{
    ResolveInput::Layer layer;
    layer.defaultRole.deny = SendMessages;
    const Bits p = resolveChannel(kMemberDefaults, {}, {layer});
    EXPECT_TRUE(has(p, ViewChannel));
    EXPECT_FALSE(has(p, SendMessages));
}

TEST(Permissions, RoleAllowBeatsRoleDenyAtSameLevel)
{
    ResolveInput::Layer layer;
    layer.memberRoles = {Override{0, SendMessages}, Override{SendMessages, 0}};
    const Bits p = resolveChannel(ViewChannel | SendMessages, {}, {layer});
    EXPECT_TRUE(has(p, SendMessages));
}

TEST(Permissions, UserOverrideWinsLast)
{
    ResolveInput::Layer layer;
    layer.memberRoles = {Override{SendMessages, 0}};
    layer.user = Override{0, SendMessages};
    const Bits p = resolveChannel(ViewChannel, {}, {layer});
    EXPECT_FALSE(has(p, SendMessages));
}

TEST(Permissions, CategoryThenChannelLayering)
{
    ResolveInput::Layer category;
    category.defaultRole.deny = ViewChannel; // private category
    ResolveInput::Layer channel;
    channel.user.allow = ViewChannel; // but this user may see the channel
    EXPECT_TRUE(has(resolveChannel(kMemberDefaults, {}, {category, channel}), ViewChannel));
    EXPECT_EQ(resolveChannel(kMemberDefaults, {}, {category}), 0u);
}

TEST(Permissions, NoViewMeansNothing)
{
    ResolveInput::Layer layer;
    layer.defaultRole.deny = ViewChannel;
    EXPECT_EQ(resolveChannel(kMemberDefaults, {}, {layer}), 0u);
}

TEST(Permissions, OverridesCannotGrantAdministrator)
{
    ResolveInput::Layer layer;
    layer.user.allow = Administrator | ViewChannel;
    const Bits p = resolveChannel(0, {}, {layer});
    EXPECT_FALSE(p & Administrator);
    EXPECT_TRUE(has(p, ViewChannel));
}

TEST(Permissions, NamesRoundTrip)
{
    for (auto n : names(kAll))
        EXPECT_NE(fromName(n), 0u) << n;
    EXPECT_EQ(fromName("NOT_A_PERMISSION"), 0u);
    EXPECT_EQ(fromName("SEND_MESSAGES"), SendMessages);
}

TEST(Permissions, PresetsAreNested)
{
    EXPECT_EQ(kGuestDefaults & kMemberDefaults, kGuestDefaults);
    EXPECT_EQ(kMemberDefaults & kModeratorDefaults, kMemberDefaults);
    EXPECT_EQ(kModeratorDefaults & kAdminDefaults, kModeratorDefaults);
    EXPECT_FALSE(has(kGuestDefaults, SendMessages));
}

TEST(Permissions, NamedContributorsFollowWinningOverrideAndClearAtUserAndVisibilityLayers)
{
    const std::vector<Bits> roles{ManageMessages, ManageMessages};
    const std::vector<std::uint64_t> ids{10, 20};
    ResolveInput::Layer layer;
    layer.memberRoles = {{0, ManageMessages}, {ManageMessages, 0}};
    layer.memberRoleIds = ids;
    std::vector<ResolveInput::Layer> layers{layer};
    ResolveInput input;
    input.defaultRole = ViewChannel;
    input.defaultRoleId = 1;
    input.memberRoles = roles;
    input.memberRoleIds = ids;
    input.layers = layers;
    input.channelScope = true;
    auto row = [](const Explanation& e, Bits bit) {
        for (const auto& entry : e.permissions)
            if (entry.permission == bit)
                return entry;
        return PermissionSource{};
    };
    auto result = explain(input);
    EXPECT_EQ(result.effective, resolve(input));
    EXPECT_EQ(row(result, ManageMessages).roles, std::vector<std::uint64_t>{20});
    EXPECT_EQ(row(result, ViewChannel).roles, std::vector<std::uint64_t>{1});
    layers[0].memberRoles[1] = {0, ManageMessages};
    result = explain(input);
    EXPECT_EQ(row(result, ManageMessages).roles, ids);
    EXPECT_FALSE(row(result, ManageMessages).allowed);
    layers[0].user = {ManageMessages, 0};
    result = explain(input);
    EXPECT_TRUE(row(result, ManageMessages).roles.empty());
    EXPECT_TRUE(row(result, ManageMessages).allowed);
    layers[0].user = {0, ViewChannel};
    result = explain(input);
    EXPECT_EQ(result.effective, 0u);
    EXPECT_TRUE(row(result, ManageMessages).roles.empty());
}

TEST(Permissions, AdministratorContributorsIdentifyOnlyAdministratorRoles)
{
    const std::vector<Bits> roles{ManageMessages, Administrator, Administrator};
    const std::vector<std::uint64_t> ids{10, 20, 30};
    ResolveInput input;
    input.memberRoles = roles;
    input.memberRoleIds = ids;
    auto result = explain(input);
    for (const auto& entry : result.permissions)
        EXPECT_EQ(entry.roles, (std::vector<std::uint64_t>{20, 30}));
    input.isOwner = true;
    result = explain(input);
    for (const auto& entry : result.permissions)
        EXPECT_TRUE(entry.roles.empty());
}
