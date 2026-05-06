<img width="1514" height="640" alt="banner (2)" src="https://github.com/user-attachments/assets/5eae77ec-27b1-45f7-9458-d89bedc26eb0" />

# OpenDriver-VR

OpenDriver-VR is a native SteamVR/OpenVR driver stack in modern C++ with:
- a SteamVR-loaded driver (`driver_opendriver.dll`)
- a separate runtime process (`opendriver_runner.exe`)
- a dynamic plugin system for HMD/controllers/trackers/custom devices
- a Qt dashboard UI
- a Windows Media Foundation video path (DX11 -> H.264)

The repository is laid out so it can be registered directly as a SteamVR driver after build.

## Features

### Core runtime
- Native SteamVR/OpenVR driver integration
- Runtime launched by driver (lightweight driver, heavier logic in runner)
- Event bus with publish/subscribe model
- Device registry for dynamic virtual devices
- Bridge IPC between runtime and SteamVR-facing driver code
- Centralized logging and config management

### Plugin system
- Dynamic plugin loading (`.dll`/`.so`) via `plugin.json`
- Plugin lifecycle: initialize, tick, event handling, shutdown
- Runtime load/unload and directory scanning
- Hot-reload support (`ExportState` / `ImportState`)
- Crash isolation path for plugin `OnTick()` exceptions
- Optional plugin-provided UI tabs

### Video pipeline (Windows)
- Media Foundation H.264 encoder path
- DX11 shared texture ingestion
- NV12 conversion fallback path
- Annex-B output handling
- Encoder telemetry logging (attempts/failures/timing snapshots)
- Cooldown/backoff path after repeated encode failures

### UI / Operations
- Qt dashboard (`opendriver_runner.exe`)
- Device and plugin visibility
- Plugin enable/disable/reload flows
- Video settings panel
- Install scripts for SteamVR registration

### Quality and CI
- Unit/integration tests for core and video utility paths
- Plugin crash-path test in loader
- GitHub Actions CI (`build-and-test`, `strict-build`)
- Optional strict compile mode (`OPENDRIVER_STRICT`)

## Repository Layout

```text
OpenDriver-VR/
  driver/
    driver.vrdrivermanifest
    bin/win64/
      driver_opendriver.dll
      opendriver_runner.exe
  include/
  src/
  plugins/
  scripts/
    install_driver.ps1
    install_driver_only.ps1
  docs/
```

## Architecture (High-level)

1. SteamVR loads `driver/driver.vrdrivermanifest`.
2. SteamVR loads `driver/bin/win64/driver_opendriver.dll`.
3. Driver launches `opendriver_runner.exe`.
4. Runtime initializes config/logging/plugins.
5. Plugins register devices and push pose/input updates.
6. Bridge IPC synchronizes runtime state with driver-facing side.

## Windows Build

Requirements:

- Visual Studio 2022 with `Desktop development with C++`
- CMake 3.16+
- Qt6 for MSVC x64

Example configure/build:

```powershell
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DQt6_DIR="C:\Qt\6.x\msvc2022_64\lib\cmake\Qt6"
cmake --build build --config Release --parallel
```

Build output is copied into `driver/bin/win64/`.

Useful options:

```powershell
# Disable GUI (for CI/headless)
cmake -B build -A x64 -DOPENDRIVER_BUILD_GUI=OFF

# Disable Linux x264/FFmpeg path toggles in multi-platform CI configs
cmake -B build -A x64 -DOPENDRIVER_ENABLE_LINUX_VIDEO=OFF

# Treat warnings as errors (core/test quality gate)
cmake -B build -A x64 -DOPENDRIVER_STRICT=ON -DBUILD_TESTING=ON
```

## SteamVR Installation

### Driver only

Registers only the native SteamVR driver and does not deploy any plugins:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install_driver_only.ps1
```

Build first, then install in one step:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install_driver_only.ps1 -BuildRelease
```

### Driver with plugins

If you want the older full install flow that also deploys plugin payloads:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install_driver.ps1
```

Both scripts register the `driver` folder with SteamVR through `vrpathreg.exe`.

## Runtime and Plugins

The runtime scans plugins in:

```text
%APPDATA%\opendriver\plugins
```

If the folder is empty, the runtime still starts normally. Plugins are optional.

## Creating Plugins

Plugins are shared libraries exporting `CreatePlugin` and `DestroyPlugin`, discovered by `plugin.json`.

Minimal plugin shape:

```cpp
#include <opendriver/core/plugin_interface.h>

using namespace opendriver::core;

class MyPlugin : public IPlugin {
public:
    const char* GetName() const override { return "my_plugin"; }
    const char* GetVersion() const override { return "1.0.0"; }
    const char* GetDescription() const override { return "Example plugin"; }
    const char* GetAuthor() const override { return "You"; }

    bool OnInitialize(IPluginContext* context) override { return true; }
    void OnShutdown() override {}
    void OnTick(float delta_time) override {}
    void OnEvent(const Event& event) override {}
    bool IsActive() const override { return true; }
};
```

Detailed docs:

- [docs/DEVELOPER_GUIDE.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/DEVELOPER_GUIDE.md)
- [docs/PLUGINS_API.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/PLUGINS_API.md)

## Testing

Configure with tests:

```powershell
cmake -B build -A x64 -DBUILD_TESTING=ON -DOPENDRIVER_BUILD_GUI=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Troubleshooting

- SteamVR logs: `%LOCALAPPDATA%\Steam\logs`
- OpenDriver log: `%APPDATA%\opendriver\opendriver.log`
- If SteamVR does not load the driver, verify `driver/driver.vrdrivermanifest` exists and the `driver` folder was registered successfully.
- If the runner fails to start, verify `driver/bin/win64/` still contains `opendriver_runner.exe`, Qt DLLs, and `platforms/qwindows.dll`.

## Documentation

- [docs/DEVELOPER_GUIDE.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/DEVELOPER_GUIDE.md)
- [docs/PLUGINS_API.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/PLUGINS_API.md)
- [docs/CHANGELOG.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/CHANGELOG.md)
- [docs/OpenDriver1.2.md](/c:/Users/Tomasz/Desktop/OpenDriver-VR/docs/OpenDriver1.2.md)
