#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace omachat::daemon {

enum class NotificationLevel { Default, All, Mentions, None };

constexpr std::string_view notificationLevelName(NotificationLevel level)
{
    switch (level) {
    case NotificationLevel::Default:
        return "default";
    case NotificationLevel::All:
        return "all";
    case NotificationLevel::Mentions:
        return "mentions";
    case NotificationLevel::None:
        return "none";
    }
    return {};
}

constexpr std::optional<NotificationLevel> parseNotificationLevel(std::string_view name)
{
    for (auto level :
        {NotificationLevel::Default, NotificationLevel::All, NotificationLevel::Mentions, NotificationLevel::None}) {
        if (notificationLevelName(level) == name)
            return level;
    }
    return std::nullopt;
}

struct ChannelPreferences {
    NotificationLevel notificationLevel = NotificationLevel::Default;
    // UTC epoch milliseconds: absent = unmuted; zero = until changed.
    std::optional<std::int64_t> muteUntilMs;

    bool muted(std::int64_t nowMs) const { return muteUntilMs && (*muteUntilMs == 0 || nowMs < *muteUntilMs); }
    bool operator==(const ChannelPreferences&) const = default;
};

struct NotificationContext {
    bool enabled = true;
    bool channelAccessible = true;
    bool selfAuthored = false;
    bool doNotDisturb = false;
    bool focusedConversation = false;
    bool directMessage = false; // includes group DMs
    bool individualMention = false; // includes locally decoded encrypted mentions
    bool directMessagesEnabled = true;
    bool communityEnabled = true;
};

// Pure policy: no wall clock, UI state, account lookup or notification side effects.
inline bool shouldNotify(const ChannelPreferences& preferences, const NotificationContext& context, std::int64_t nowMs)
{
    if (!context.enabled || !context.channelAccessible || context.selfAuthored || context.doNotDisturb
        || context.focusedConversation || preferences.muted(nowMs))
        return false;
    if (context.directMessage ? !context.directMessagesEnabled : !context.communityEnabled)
        return false;
    switch (preferences.notificationLevel) {
    case NotificationLevel::Default:
        return context.directMessage || context.individualMention;
    case NotificationLevel::All:
        return true;
    case NotificationLevel::Mentions:
        return context.individualMention;
    case NotificationLevel::None:
        return false;
    }
    return false;
}

} // namespace omachat::daemon
