#include <opendriver/core/config_manager.h>

#include <cassert>
#include <filesystem>

int main() {
    using namespace opendriver::core;
    namespace fs = std::filesystem;

    const fs::path temp_file =
        fs::temp_directory_path() / "opendriver_config_manager_test.json";

    ConfigManager cfg;
    assert(cfg.Load(temp_file.string()) == true);

    cfg.SetString("plugins.android_hmd.device_id", "android_hmd_001");
    cfg.SetInt("plugins.android_hmd.udp_port", 6969);
    cfg.SetFloat("plugins.android_hmd.refresh_rate", 90.0f);
    cfg.SetBool("plugins.android_hmd.enabled", true);

    assert(cfg.Save() == true);

    ConfigManager cfg2;
    assert(cfg2.Load(temp_file.string()) == true);
    assert(cfg2.GetString("plugins.android_hmd.device_id", "") == "android_hmd_001");
    assert(cfg2.GetInt("plugins.android_hmd.udp_port", 0) == 6969);
    assert(cfg2.GetFloat("plugins.android_hmd.refresh_rate", 0.0f) == 90.0f);
    assert(cfg2.GetBool("plugins.android_hmd.enabled", false) == true);
    assert(cfg2.GetString("does.not.exist", "fallback") == "fallback");

    fs::remove(temp_file);
    return 0;
}
