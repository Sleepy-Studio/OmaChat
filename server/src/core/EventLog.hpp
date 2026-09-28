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
    std::uint64_t append(proto::Event& event, std::vector<Id> recipients)
    {
        event.set_sequence(m_next++);
        std::ranges::sort(recipients);
        m_entries.push_back(Entry{event, std::move(recipients)});
        while (m_entries.size() > m_capacity)
            m_entries.pop_front();
        return event.sequence();
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
                deliver(e.event);
        }
        return true;
    }

private:
    struct Entry {
        proto::Event event;
        std::vector<Id> recipients;
    };
    std::size_t m_capacity;
    std::deque<Entry> m_entries;
    std::uint64_t m_next = 1;
};

} // namespace omachat::server
