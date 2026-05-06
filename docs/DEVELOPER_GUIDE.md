# OpenDriver Plugin Developer Guide (Step-by-Step)

This guide is a practical path from zero to a working plugin.
For complete API reference, see `docs/PLUGINS_API.md`.

## 1. Goal

Build a plugin that:
1. Loads correctly via `plugin.json`.
2. Registers at least one virtual device.
3. Publishes pose/input data in `OnTick()`.
4. Shuts down cleanly.

## 2. Create Plugin Files

Recommended folder shape:
```text
my_plugin/
  plugin.json
  my_plugin.dll (or .so after build)
```

Minimal `plugin.json`:
```json
{
  "name": "my_plugin",
  "version": "1.0.0",
  "description": "My first OpenDriver plugin",
  "author": "You",
  "enabled": true,
  "entry_point": "my_plugin.dll"
}
```

## 3. Implement `IPlugin`

Start from this template:

```cpp
#include <opendriver/core/plugin_interface.h>
#include <opendriver/core/platform.h>

using namespace opendriver::core;

class MyPlugin : public IPlugin {
public:
    const char* GetName() const override { return "my_plugin"; }
    const char* GetVersion() const override { return "1.0.0"; }
    const char* GetDescription() const override { return "First plugin"; }
    const char* GetAuthor() const override { return "You"; }

    bool OnInitialize(IPluginContext* context) override {
        m_context = context;
        return true;
    }

    void OnShutdown() override {}
    void OnTick(float) override {}
    bool IsActive() const override { return true; }

private:
    IPluginContext* m_context = nullptr;
};

extern "C" {
OD_EXPORT IPlugin* CreatePlugin() { return new MyPlugin(); }
OD_EXPORT void DestroyPlugin(IPlugin* plugin) { delete plugin; }
}
```

## 4. Register a Device

Inside `OnInitialize()`:

```cpp
Device hmd;
hmd.id = "my_hmd_001";
hmd.type = DeviceType::HMD;
hmd.name = "My HMD";
hmd.manufacturer = "MyCompany";
hmd.owner_plugin = GetName();
hmd.connected = true;

hmd.display.width = 1920;
hmd.display.height = 1080;
hmd.display.refresh_rate = 90.0f;
hmd.display.fov_left = 100.0f;
hmd.display.fov_right = 100.0f;

m_context->RegisterDevice(hmd);
```

On shutdown:
```cpp
m_context->UnregisterDevice("my_hmd_001");
```

## 5. Publish Pose in `OnTick()`

Example:

```cpp
m_context->UpdatePose(
    "my_hmd_001",
    x, y, z,
    qw, qx, qy, qz,
    vx, vy, vz,
    avx, avy, avz
);
```

Rules:
- Use meters for position.
- Use valid quaternion orientation.
- Avoid expensive work in `OnTick()`.

## 6. Publish Input

```cpp
m_context->UpdateInput("my_hmd_001", "/input/trigger/value", trigger_value);
m_context->UpdateInput("my_hmd_001", "/input/a/click", button_down ? 1.0f : 0.0f);
```

## 7. Use Logging

```cpp
m_context->LogInfo("[my_plugin] initialized");
m_context->LogWarn("[my_plugin] sensor packet delayed");
m_context->LogError("[my_plugin] connection lost");
```

Best practice:
- Log key state transitions.
- Rate-limit repetitive warnings.

## 8. Add Config Support

Read settings:

```cpp
auto& cfg = m_context->GetConfig();
const float smoothing = cfg.GetFloat("plugins.my_plugin.smoothing", 0.25f);
```

Write settings:

```cpp
cfg.SetFloat("plugins.my_plugin.smoothing", 0.35f);
cfg.Save();
```

Always clamp/validate config values before use.

## 9. Threading Pattern (Recommended)

If you have network/hardware threads:
- thread reads raw data
- writes latest snapshot to shared struct (mutex or atomics)
- `OnTick()` reads snapshot and calls `UpdatePose`/`UpdateInput`

Do not block in `OnTick()` waiting for I/O.

## 10. Event Bus Example

Subscribe:
```cpp
m_context->GetEventBus().Subscribe(EventType::VIDEO_FRAME, this);
```

Handle:
```cpp
void OnEvent(const Event& event) override {
    if (event.type != EventType::VIDEO_FRAME) return;
    // parse std::any payload safely
}
```

Unsubscribe on shutdown.

## 11. Hot Reload State (Optional)

If you need live reload persistence:
```cpp
struct State { float offset; };

void* ExportState() override { return new State{m_offset}; }
void ImportState(void* raw) override {
    if (!raw) return;
    auto* s = static_cast<State*>(raw);
    m_offset = s->offset;
    delete s;
}
```

## 12. Common Failure Modes

- Missing `entry_point` in `plugin.json`.
- Wrong exported names (`CreatePlugin` / `DestroyPlugin`).
- Throwing uncaught exceptions from `OnTick()`.
- Blocking `OnTick()` with socket reads.
- Forgetting `UnregisterDevice` and event unsubscriptions.

## 13. Pre-Release Checklist

- Plugin loads/unloads cleanly.
- No leaks after repeated load/unload.
- Works after runtime restart.
- Handles sensor disconnect/reconnect.
- Logs are actionable and not spammy.
- Config defaults are safe.

## 14. Next Step

After first plugin works, add:
- health counters in `GetStatus()`
- graceful reconnect logic
- minimal integration tests (where possible)

