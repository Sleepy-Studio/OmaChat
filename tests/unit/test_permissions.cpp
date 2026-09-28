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
