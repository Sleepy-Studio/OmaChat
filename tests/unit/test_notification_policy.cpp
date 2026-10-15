#include "notifications/NotificationPolicy.hpp"

#include <gtest/gtest.h>

using namespace omachat::daemon;

TEST(NotificationPolicy, LevelsAndGlobalMasterSwitches)
{
    for (const auto level :
        {NotificationLevel::Default, NotificationLevel::All, NotificationLevel::Mentions, NotificationLevel::None}) {
        EXPECT_EQ(parseNotificationLevel(notificationLevelName(level)), level);
        for (bool dm : {false, true}) {
            for (bool mention : {false, true}) {
                for (bool dmEnabled : {false, true}) {
                    for (bool communityEnabled : {false, true}) {
                        SCOPED_TRACE(::testing::Message()
                            << notificationLevelName(level) << " dm=" << dm << " mention=" << mention
                            << " dmEnabled=" << dmEnabled << " communityEnabled=" << communityEnabled);
                        NotificationContext context;
                        context.directMessage = dm;
                        context.individualMention = mention;
                        context.directMessagesEnabled = dmEnabled;
                        context.communityEnabled = communityEnabled;
                        const bool eligible = level == NotificationLevel::All
                            || (level == NotificationLevel::Default && (dm || mention))
                            || (level == NotificationLevel::Mentions && mention);
                        EXPECT_EQ(
                            shouldNotify({level, {}}, context, 100), eligible && (dm ? dmEnabled : communityEnabled));
                    }
                }
            }
        }
    }
    EXPECT_FALSE(parseNotificationLevel("ALL"));
    EXPECT_FALSE(parseNotificationLevel(""));
    EXPECT_FALSE(parseNotificationLevel("unknown"));
}

TEST(NotificationPolicy, SuppressorsApplyToEveryLevelAndConversationKind)
{
    for (const auto level : {NotificationLevel::Default, NotificationLevel::All, NotificationLevel::Mentions}) {
        for (bool dm : {false, true}) {
            NotificationContext context;
            context.individualMention = true;
            context.directMessage = dm;
            ASSERT_TRUE(shouldNotify({level, {}}, context, 100));
            for (auto member : {&NotificationContext::enabled, &NotificationContext::channelAccessible}) {
                auto blocked = context;
                blocked.*member = false;
                EXPECT_FALSE(shouldNotify({level, {}}, blocked, 100));
            }
            for (auto member : {&NotificationContext::selfAuthored, &NotificationContext::doNotDisturb,
                     &NotificationContext::focusedConversation}) {
                auto blocked = context;
                blocked.*member = true;
                EXPECT_FALSE(shouldNotify({level, {}}, blocked, 100));
            }
            EXPECT_FALSE(shouldNotify({level, 0}, context, 100));
            EXPECT_FALSE(shouldNotify({level, 101}, context, 100));
        }
    }
}

TEST(NotificationPolicy, ExpiryUsesInjectedTimeWithoutChangingUnderlyingLevel)
{
    ChannelPreferences preferences{NotificationLevel::All, 1000};
    NotificationContext context;
    EXPECT_FALSE(shouldNotify(preferences, context, 999));
    EXPECT_TRUE(shouldNotify(preferences, context, 1000));
    EXPECT_TRUE(shouldNotify(preferences, context, 1001));
    EXPECT_TRUE(shouldNotify(preferences, context, 90000)) << "forward clock / resume";
    EXPECT_FALSE(shouldNotify(preferences, context, 999)) << "backward clock";
    EXPECT_EQ(preferences.muteUntilMs, 1000);
    EXPECT_EQ(preferences.notificationLevel, NotificationLevel::All);
    preferences.notificationLevel = NotificationLevel::None;
    EXPECT_FALSE(shouldNotify(preferences, context, 1001));
    preferences.muteUntilMs = 0;
    EXPECT_TRUE(preferences.muted(INT64_MAX));
    preferences.muteUntilMs.reset();
    EXPECT_FALSE(preferences.muted(0));
}
