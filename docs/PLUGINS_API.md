# OpenDriver Plugins API (Detailed)

This document is the technical reference for writing OpenDriver plugins.
It focuses on practical, production-safe plugin development for the current architecture in this repository.

## 1. Plugin Model in OpenDriver

OpenDriver loads plugins as dynamic libraries:
- Windows: `.dll`
- Linux: `.so`

At runtime, `PluginLoader` scans plugin directories and loads libraries defined by `plugin.json`.
The core interacts with your plugin through the `IPlugin` and `IPluginContext` interfaces.

Key properties of the model:
- Plugins are optional.
- Plugins can be loaded/unloaded at runtime.
- Plugin crashes in `OnTick()` are isolated and cause plugin disable/unload (not full runtime crash).
- Hot-reload is supported through `ExportState()` / `ImportState()`.

## 2. Required Exports and Metadata

Every plugin library must export:
- `CreatePlugin()`
- `DestroyPlugin()`

Example:
```cpp
extern "C" {
OD_EXPORT opendriver::core::IPlugin* CreatePlugin() {
    return new MyPlugin();
}

OD_EXPORT void DestroyPlugin(opendriver::core::IPlugin* plugin) {
    delete plugin;
}
}
```

Your `IPlugin` implementation must provide metadata:
- `GetName()`: stable ID-like name, lowercase, no spaces recommended.
- `GetVersion()`: semantic version string.
- `GetDescription()`
- `GetAuthor()`

`GetName()` must be stable over time. It is used by loader logic and state flows.

## 3. `plugin.json` Contract

`PluginLoader::LoadDirectory()` expects plugin directories with `plugin.json`.

Important fields:
- `entry_point` (required): filename of your compiled library.
- `enabled` (optional, default `true`)
- `name`, `version`, `description`, `author` (used by UI listing/metadata)

Minimal example:
```json
{
  "name": "my_tracker",
  "version": "1.0.0",
  "description": "Example tracker plugin",
  "author": "Your Team",
  "enabled": true,
  "entry_point": "my_tracker.dll"
}
```

If `entry_point` is missing, loader logs an error and skips the plugin.

## 4. Lifecycle (Exact Order)

Typical flow:
1. Dynamic library load.
2. Resolve `CreatePlugin`/`DestroyPlugin`.
3. `CreatePlugin()` returns instance.
4. `OnInitialize(IPluginContext*)`.
5. Per-frame `OnTick(float delta_time)` while active.
6. Optional `OnEvent(const Event&)` for subscribed events.
7. On shutdown/unload: `OnShutdown()`.
8. `DestroyPlugin()`.

On `OnTick()` exception:
- Core publishes `PLUGIN_ERROR`.
- Plugin is scheduled for unload.

## 5. `IPlugin` Methods (What to Do in Each)

### `OnInitialize(IPluginContext*)`
Use for:
- Saving context pointer.
- Registering devices.
- Reading initial config values.
- Starting sockets/threads/services.
- Subscribing to events.

Return `false` only for hard failures (plugin should not run).

### `OnTick(float delta_time)`
Use for:
- Polling latest device/sensor state.
- Processing filters/smoothing.
- Calling `UpdatePose` / `UpdateInput`.
- Light diagnostics sampling.

Keep it fast and non-blocking.

### `OnEvent(const Event&)`
Use for asynchronous reactions:
- control events
- runtime/UI requests
- plugin-to-plugin communication via event bus

### `OnShutdown()`
Must:
- stop all threads cleanly
- close sockets/handles
- unsubscribe from events
- release owned resources

Never rely on process exit to clean up.

### Hot-reload hooks
- `ExportState()`: return heap-allocated state pointer if needed.
- `ImportState(void*)`: restore and free state.

If you allocate in `ExportState()`, always free in `ImportState()`.

## 6. `IPluginContext` Surface

Main APIs:
- `GetEventBus()`
- `GetConfig()`
- `Log(...)`, `LogInfo(...)`, etc.
- `RegisterDevice(...)`, `UnregisterDevice(...)`, `UnregisterDevicesByPlugin(...)`
- `UpdatePose(...)`
- `UpdateInput(...)`
- `GetPlugin(...)`
- `PostToMainThread(...)`
- `RequestShowEncoderSettings()`

### Device registration
Register once in `OnInitialize()`.
Use a stable `Device.id` and correct `DeviceType`.

### Pose and input updates
Call frequently from `OnTick()` with current values.
Use physically meaningful units and valid quaternions.

### Logging
Prefer context logging over `std::cout`.
Keep log volume bounded in hot paths.

### Config
Read defaults defensively.
Validate all user values before applying.

### Main-thread handoff
Use `PostToMainThread()` for operations requiring runtime-thread affinity.

## 7. Threading and Concurrency Rules

Plugin code is often multi-threaded (network + tick loop). Follow these rules:

1. `OnTick()` must not block on long I/O.
2. Use lock-free or short-lock data transfer from worker threads to tick thread.
3. If a worker thread mutates shared state, guard with mutex/atomic.
4. Never call plugin unload logic from worker threads.
5. Keep event handlers re-entrant-safe.

Recommended pattern:
- worker thread writes latest snapshot into guarded struct
- `OnTick()` reads snapshot and publishes pose/input

## 8. Error Handling Strategy

Use 3 classes of errors:
- Recoverable transient (log warning, continue).
- Recoverable degraded (disable a feature path, continue plugin).
- Fatal init/runtime (return false in init or `IsActive() == false`).

Do not throw exceptions out of `OnTick()` in production plugin code.
If you use exceptions internally, catch inside plugin and convert to controlled state.

## 9. Event Bus Usage

Subscribe in `OnInitialize()`, unsubscribe in `OnShutdown()`.

Typical flow:
```cpp
context->GetEventBus().Subscribe(opendriver::core::EventType::VIDEO_FRAME, this);
```

Event payload is stored in `std::any`.
Always validate/cast carefully.

## 10. Build and Packaging

### In-repo build
Create plugin target in this repo and copy output into plugin directory used by runtime.

### Standalone build
A plugin can be built in a separate repository with copied OpenDriver headers.
You generally do not link against OpenDriver core library directly; ABI surface is interface-based.

### Packaging checklist
- library file
- `plugin.json`
- any required assets/config defaults
- version bump

## 11. Minimal Plugin Skeleton

```cpp
#include <opendriver/core/plugin_interface.h>
#include <opendriver/core/platform.h>

class MyPlugin : public opendriver::core::IPlugin {
public:
    const char* GetName() const override { return "my_plugin"; }
    const char* GetVersion() const override { return "1.0.0"; }
    const char* GetDescription() const override { return "Example plugin"; }
    const char* GetAuthor() const override { return "Team"; }

    bool OnInitialize(opendriver::core::IPluginContext* context) override {
        m_context = context;
        return true;
    }

    void OnShutdown() override {}
    void OnTick(float) override {}
    bool IsActive() const override { return true; }

private:
    opendriver::core::IPluginContext* m_context = nullptr;
};

extern "C" {
OD_EXPORT opendriver::core::IPlugin* CreatePlugin() { return new MyPlugin(); }
OD_EXPORT void DestroyPlugin(opendriver::core::IPlugin* plugin) { delete plugin; }
}
```

## 12. Production Readiness Checklist

- Stable `GetName()`
- `OnInitialize()` validates required resources
- No blocking calls in `OnTick()`
- Worker threads are stoppable and joined in `OnShutdown()`
- Event subscriptions are balanced (subscribe/unsubscribe)
- Config values validated and clamped
- Pose quaternion normalized (or validated)
- Log rate-limited for hot loops
- Hot-reload state ownership is explicit and leak-free
- Plugin handles missing device/network gracefully

## 13. Debugging Tips

- Use plugin-specific log prefixes.
- Add periodic (not per-frame) status summaries.
- Expose useful counters in `GetStatus()`.
- Reproduce failures with deterministic input playback where possible.

---

If you are starting from scratch, use `docs/DEVELOPER_GUIDE.md` as the step-by-step tutorial and this file as the reference.
