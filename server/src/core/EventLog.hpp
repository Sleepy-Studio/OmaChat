#pragma once

#include "network.pb.h"
#include "storage/Records.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <vector>

namespace omachat::server {

// Bounded ring of recently published events, used to resume sessions after a
// brief disconnect without a full resynchronization.
class EventLog {
public:
    explicit EventLog(std::size_t capacity = 20000)
        : m_capacity(capacity)
    {
    }

    // Assigns the next sequence number and records the event.
    std::uint64_t append(proto::Event& event, std::vector<Id> recipients, std::map<Id, unsigned> mentionReasons = {})
    {
        event.set_sequence(m_next++);
        std::ranges::sort(recipients);
        m_entries.push_back(Entry{event, std::move(recipients), std::move(mentionReasons)});
        while (m_entries.size() > m_capacity)
            m_entries.pop_front();
        return event.sequence();
    }

    static proto::Event project(proto::Event event, Id viewer, const std::map<Id, unsigned>& reasons)
    {
        proto::ChatMessage* message = event.has_message_create() ? event.mutable_message_create()
            : event.has_message_update()                         ? event.mutable_message_update()
                                                                 : nullptr;
        if (message && message->has_mention_classification()) {
            const auto found = reasons.find(viewer);
            message->mutable_mention_classification()->set_reason_mask(found == reasons.end() ? 0 : found->second);
        }
        return event;
    }

    std::uint64_t lastSequence() const { return m_next - 1; }

    // Calls `deliver` for each event after `lastSeen` addressed to `user`.
    // Returns false when events after `lastSeen` were already evicted (or the
    // client claims a sequence from the future), meaning a full sync is needed.
    bool replay(Id user, std::uint64_t lastSeen, const std::function<void(const proto::Event&)>& deliver) const
    {
        if (lastSeen > lastSequence())
            return false;
        if (lastSeen == lastSequence())
            return true;
        if (m_entries.empty() || m_entries.front().event.sequence() > lastSeen + 1)
            return false;
        for (const Entry& e : m_entries) {
            if (e.event.sequence() <= lastSeen)
                continue;
            if (std::ranges::binary_search(e.recipients, user))
                deliver(project(e.event, user, e.mentionReasons));
        }
        return true;
    }

private:
    struct Entry {
        proto::Event event;
        std::vector<Id> recipients;
        std::map<Id, unsigned> mentionReasons;
    };
    std::size_t m_capacity;
    std::deque<Entry> m_entries;
    std::uint64_t m_next = 1;
};

} // namespace omachat::server
