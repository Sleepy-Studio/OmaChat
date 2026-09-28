#include "core/State.hpp"

#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::server;
using namespace omachat::permissions;

namespace {

// owner=1, admin=2, member=3, guest=4 ; guest role is default
State makeState()
{
    ServerRecord s;
    s.id = 100;
    s.name = QStringLiteral("S");
    s.ownerId = 1;
    s.roles[10] = RoleRecord{10, 100, QStringLiteral("Guest"), kGuestDefaults, 0, 0, true};
    s.roles[11] = RoleRecord{11, 100, QStringLiteral("Member"), kMemberDefaults, 1, 0, false};
    s.roles[13] = RoleRecord{13, 100, QStringLiteral("Admin"), kAdminDefaults, 3, 0, false};
    s.members[1] = MemberRecord{100, 1, 0, {11}};
    s.members[2] = MemberRecord{100, 2, 0, {11, 13}};
    s.members[3] = MemberRecord{100, 3, 0, {11}};
    s.members[4] = MemberRecord{100, 4, 0, {}};
    State st;
    std::vector<UserRecord> users;
    for (Id u = 1; u <= 5; ++u)
        users.push_back(UserRecord{u, QStringLiteral("u%1").arg(u), QStringLiteral("U%1").arg(u), {}, {}, 0});
    std::vector<ChannelRecord> channels{
        ChannelRecord{200, 100, QStringLiteral("general"), ChannelKind::Text, 0, 0, {}, {}},
        ChannelRecord{201, 100, QStringLiteral("staff"), ChannelKind::Text, 0, 1, {}, {}},
        ChannelRecord{300, 0, QString(), ChannelKind::Dm, 0, 0, {}, {3, 4}},
    };
    // #staff: hidden from the default role, visible to admins only.
    std::vector<OverrideRecord> overrides{OverrideRecord{201, 0, 10, 0, ViewChannel},
        OverrideRecord{201, 0, 11, 0, ViewChannel}, OverrideRecord{201, 0, 13, ViewChannel, 0}};
    st.load(users, {s}, channels, overrides);
    return st;
}

} // namespace

TEST(ServerState, RoleBasedAccess)
{
    State st = makeState();
    EXPECT_TRUE(st.can(200, 3, SendMessages));
    EXPECT_FALSE(st.can(200, 4, SendMessages)) << "guests cannot write";
    EXPECT_TRUE(st.can(200, 4, ViewChannel));
    EXPECT_FALSE(st.can(200, 5, ViewChannel)) << "non-members see nothing";
}

TEST(ServerState, OverridesHidePrivateChannels)
{
    State st = makeState();
    EXPECT_FALSE(st.can(201, 3, ViewChannel));
    EXPECT_TRUE(st.can(201, 2, ViewChannel)) << "admin has Administrator anyway";
    EXPECT_TRUE(st.can(201, 1, ViewChannel)) << "owner always";
    const auto audience = st.channelAudience(201);
    EXPECT_EQ(audience.size(), 2u);
}

TEST(ServerState, DmVisibilityIsParticipantsOnly)
{
    State st = makeState();
    EXPECT_TRUE(st.can(300, 3, SendMessages));
    EXPECT_FALSE(st.can(300, 1, ViewChannel)) << "even the server owner cannot read DMs";
    EXPECT_EQ(st.findDm(4, 3).value_or(0), 300u);
}

TEST(ServerState, RanksAndCacheInvalidation)
{
    State st = makeState();
    EXPECT_GT(st.rank(100, 1), st.rank(100, 2));
    EXPECT_GT(st.rank(100, 2), st.rank(100, 3));
    EXPECT_FALSE(st.can(200, 4, SendMessages));
    st.setMemberRole(100, 4, 11, true); // promote guest to member
    EXPECT_TRUE(st.can(200, 4, SendMessages)) << "permission cache must be invalidated";
    st.putOverride(OverrideRecord{200, 1, 4, 0, SendMessages});
    EXPECT_FALSE(st.can(200, 4, SendMessages)) << "user override denies";
    st.removeOverride(200, 1, 4);
    EXPECT_TRUE(st.can(200, 4, SendMessages));
}

TEST(ServerState, DeletingRoleRemovesItsOverridesAndAssignments)
{
    State st = makeState();
    st.removeRole(100, 13);
    EXPECT_FALSE(st.can(201, 2, ViewChannel)) << "admin lost the role and its allow override";
    EXPECT_FALSE(st.member(100, 2)->roles.contains(13));
}

TEST(ServerState, AudienceOfSharesServersAndDms)
{
    State st = makeState();
    const auto a = st.audienceOf(4);
    EXPECT_TRUE(a.contains(3));
    EXPECT_TRUE(a.contains(1));
    EXPECT_FALSE(a.contains(4));
    EXPECT_FALSE(a.contains(5));
    EXPECT_EQ(st.userByName(QStringLiteral("u3"))->id, 3u);
}
