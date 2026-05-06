#include <opendriver/core/plugin_interface.h>
#include <opendriver/core/platform.h>

#include <stdexcept>
#include <string>

using namespace opendriver::core;

namespace {

class CrashTickPlugin final : public IPlugin {
public:
    const char* GetName() const override { return "crash_tick_plugin"; }
    const char* GetVersion() const override { return "1.0.0"; }
    const char* GetDescription() const override { return "Throws on OnTick for loader test."; }
    const char* GetAuthor() const override { return "tests"; }

    bool OnInitialize(IPluginContext* context) override {
        m_context = context;
        return true;
    }

    void OnShutdown() override {}

    void OnTick(float /*delta_time*/) override {
        throw std::runtime_error("intentional test crash");
    }

    bool IsActive() const override { return true; }

private:
    IPluginContext* m_context = nullptr;
};

} // namespace

extern "C" {

OD_EXPORT IPlugin* CreatePlugin() {
    return new CrashTickPlugin();
}

OD_EXPORT void DestroyPlugin(IPlugin* plugin) {
    delete plugin;
}

}
