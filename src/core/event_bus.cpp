#include <opendriver/core/event_bus.h>
#include <algorithm>

namespace opendriver::core {

void EventBus::Subscribe(EventType event_type, IEventListener* listener) {
    if (!listener) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    auto& list = subscribers[event_type];
    
    // Check if already subscribed
    if (std::find(list.begin(), list.end(), listener) == list.end()) {
        list.push_back(listener);
    }
}

void EventBus::Unsubscribe(EventType event_type, IEventListener* listener) {
    if (!listener) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    auto it = subscribers.find(event_type);
    if (it != subscribers.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), listener), list.end());
    }
}

void EventBus::Publish(const Event& event) {
    std::vector<IEventListener*> listeners_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);

        // Store in cache
        event_cache[event.type] = event;

        auto it = subscribers.find(event.type);
        if (it != subscribers.end()) {
            listeners_snapshot = it->second;
        }
    }

    // Callbacks outside lock to avoid deadlocks/re-entrancy lock inversions.
    for (auto* listener : listeners_snapshot) {
        if (listener) {
            listener->OnEvent(event);
        }
    }
}

bool EventBus::GetLatestEventCopy(EventType event_type, Event& out_event) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = event_cache.find(event_type);
    if (it != event_cache.end()) {
        out_event = it->second;
        return true;
    }
    return false;
}

int EventBus::GetSubscriberCount(EventType event_type) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = subscribers.find(event_type);
    if (it != subscribers.end()) {
        return static_cast<int>(it->second.size());
    }
    return 0;
}

void EventBus::ClearEventCache(EventType event_type) {
    std::lock_guard<std::mutex> lock(mutex);
    event_cache.erase(event_type);
}

} // namespace opendriver::core
