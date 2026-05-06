#include <opendriver/core/event_bus.h>

#include <cassert>
#include <string>

namespace {

class SelfUnsubscribingListener final : public opendriver::core::IEventListener {
public:
    explicit SelfUnsubscribingListener(opendriver::core::EventBus& bus) : m_bus(bus) {}

    void OnEvent(const opendriver::core::Event&) override {
        ++calls;
        m_bus.Unsubscribe(opendriver::core::EventType::LOG_INFO, this);
    }

    int calls = 0;

private:
    opendriver::core::EventBus& m_bus;
};

class CountingListener final : public opendriver::core::IEventListener {
public:
    void OnEvent(const opendriver::core::Event& event) override {
        ++calls;
        if (event.data.has_value()) {
            payload = std::any_cast<std::string>(event.data);
        }
    }

    int calls = 0;
    std::string payload;
};

} // namespace

int main() {
    using namespace opendriver::core;

    EventBus bus;
    SelfUnsubscribingListener self_unsub(bus);
    CountingListener counter;

    bus.Subscribe(EventType::LOG_INFO, &self_unsub);
    bus.Subscribe(EventType::LOG_INFO, &counter);

    Event e1(EventType::LOG_INFO, "test");
    e1.data = std::string("first");
    bus.Publish(e1);

    assert(self_unsub.calls == 1);
    assert(counter.calls == 1);
    assert(counter.payload == "first");
    assert(bus.GetSubscriberCount(EventType::LOG_INFO) == 1);

    Event e2(EventType::LOG_INFO, "test");
    e2.data = std::string("second");
    bus.Publish(e2);

    assert(self_unsub.calls == 1);
    assert(counter.calls == 2);
    assert(counter.payload == "second");

    Event latest;
    assert(bus.GetLatestEventCopy(EventType::LOG_INFO, latest));
    assert(std::any_cast<std::string>(latest.data) == "second");

    bus.ClearEventCache(EventType::LOG_INFO);
    assert(!bus.GetLatestEventCopy(EventType::LOG_INFO, latest));

    return 0;
}
