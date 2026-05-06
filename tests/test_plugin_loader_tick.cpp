#include <opendriver/core/plugin_loader.h>
#include <opendriver/core/event_bus.h>
#include <opendriver/core/config_manager.h>
#include <opendriver/core/device_registry.h>

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class PluginErrorListener final : public opendriver::core::IEventListener {
public:
    void OnEvent(const opendriver::core::Event& event) override {
        ++count;
        if (event.data.has_value()) {
            try {
                const auto& data =
                    std::any_cast<const opendriver::core::PluginErrorData&>(event.data);
                last_plugin = data.plugin_name;
                last_error_type = data.error_type;
            } catch (...) {
            }
        }
    }

    int count = 0;
    std::string last_plugin;
    std::string last_error_type;
};

class TestContext final : public opendriver::core::IPluginContext {
public:
    opendriver::core::EventBus& GetEventBus() override { return event_bus; }
    opendriver::core::ConfigManager& GetConfig() override { return config; }
    void Log(int, const char*) override {}
    void RegisterDevice(const opendriver::core::Device& device) override { registry.Register(device); }
    const opendriver::core::Device* GetDevice(const char* device_id) const override { return registry.Get(device_id); }
    void UnregisterDevice(const char* device_id) override { registry.Unregister(device_id); }
    void UnregisterDevicesByPlugin(const char* plugin_name) override { registry.UnregisterByPlugin(plugin_name); }
    void UpdateInput(const char*, const char*, float) override {}
    void UpdatePose(const char*, double, double, double, double, double, double, double,
                    double, double, double, double, double, double) override {}
    opendriver::core::IPlugin* GetPlugin(const char*) override { return nullptr; }
    void PostToMainThread(std::function<void()>) override {}
    void RequestShowEncoderSettings() override {}

    opendriver::core::EventBus event_bus;
    opendriver::core::ConfigManager config;
    opendriver::core::DeviceRegistry registry;
};

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace opendriver::core;

    TestContext context;
    PluginLoader loader(&context);
    PluginErrorListener listener;
    context.event_bus.Subscribe(EventType::PLUGIN_ERROR, &listener);

    const fs::path plugin_path = fs::current_path() / "Release" / "crash_tick_plugin.dll";
    assert(fs::exists(plugin_path));
    assert(loader.Load(plugin_path.string()));
    assert(loader.GetCount() == 1);

    loader.TickAll(0.011f);

    assert(loader.GetCount() == 0);
    assert(listener.count >= 1);
    assert(listener.last_plugin == "crash_tick_plugin");
    assert(!listener.last_error_type.empty());

    return 0;
}
