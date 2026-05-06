# OpenDriver-VR — Developer Guide

> **Version 0.2.0** · Last updated: May 2026

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Module Map](#module-map)
3. [Video Pipeline (Windows)](#video-pipeline-windows)
4. [Event Bus System](#event-bus-system)
5. [Plugin System](#plugin-system)
6. [Threading Model](#threading-model)
7. [IPC Bridge](#ipc-bridge)
8. [HMDek Plugin (Android HMD)](#hmdek-plugin-android-hmd)
9. [Configuration Schema](#configuration-schema)
10. [Build & Install](#build--install)
11. [Troubleshooting](#troubleshooting)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         SteamVR Runtime                         │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  driver_opendriver.dll                                  │   │
│   │  ├─ COpenDriverDevice  (generic tracked device)         │   │
│   │  ├─ COpenDriverHMD     (HMD + IVRVirtualDisplay)       │   │
│   │  │   ├─ Media Foundation H.264 encoder (mf_encoder)     │   │
│   │  │   ├─ VideoEncoderLoop  (high-priority thread)        │   │
│   │  │   └─ VideoSenderLoop   (IPC sender thread)           │   │
│   │  └─ CServerDriverProvider                               │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │ Named Pipe IPC                     │
│   ┌────────────────────────▼────────────────────────────────┐   │
│   │  opendriver_runner.exe                                  │   │
│   │  ├─ Runtime (singleton)                                 │   │
│   │  │   ├─ EventBus                                        │   │
│   │  │   ├─ DeviceRegistry                                  │   │
│   │  │   ├─ ConfigManager                                   │   │
│   │  │   ├─ PluginLoader                                    │   │
│   │  │   └─ Bridge ──────── IPC ◄──► SteamVR driver         │   │
│   │  ├─ Qt Dashboard (MainWindow)                           │   │
│   │  └─ Plugins (DLL)                                       │   │
│   │       └─ android_hmd_plugin.dll                         │   │
│   │           ├─ UdpReceiver  (pose tracking from phone)    │   │
│   │           ├─ TcpVideoSender (H.264 stream to phone)     │   │
│   │           └─ PoseProcessor (smoothing / deadzone)        │   │
│   └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                            │ UDP / TCP
                ┌───────────▼────────────┐
                │    Android Phone App   │
                │  (OpenDriver PhoneHMD) │
                └────────────────────────┘
```

### Data Flow Summary

1. **SteamVR** renders frames and hands them to `COpenDriverHMD::Present()`.
2. The driver's **encoder thread** converts the DX11 texture to H.264 via Media Foundation.
3. Encoded NAL units are sent over **Named Pipe IPC** to `opendriver_runner.exe`.
4. The **Bridge** receives the packet and publishes a `VIDEO_FRAME` event on the **EventBus**.
5. The **HMDek plugin** subscribes to `VIDEO_FRAME`, deserializes the data, and streams it to the **Android phone** over TCP.
6. The Android phone sends **gyroscope/orientation data** back via UDP.
7. The plugin processes the pose and publishes `POSE_UPDATE` on the EventBus.
8. The Bridge relays `POSE_UPDATE` back to the driver via IPC.
9. The driver calls `VRServerDriverHost()->TrackedDevicePoseUpdated()`.

---

## Module Map

| Module | Path | Purpose |
|--------|------|---------|
| **opendriver_core** | `src/core/` | Static library: EventBus, DeviceRegistry, ConfigManager, PluginLoader, Runtime, IPC, Logger |
| **driver_opendriver** | `src/driver/` | SteamVR driver DLL: device management, virtual display, H.264 encoding |
| **opendriver_runner** | `src/main.cpp` + `src/ui/` | Qt dashboard executable: runtime host, plugin management UI |
| **android_hmd_plugin** | `plugins/hmdek/` | Plugin DLL: Android phone → VR HMD bridge (UDP tracking + TCP video) |
| **mf_encoder** | `src/driver/video/` | Media Foundation hardware H.264/H.265 encoder |

### Key Headers (Public API)

| Header | Defines |
|--------|---------|
| `plugin_interface.h` | `IPlugin`, `IPluginContext` — everything a plugin author needs |
| `event_bus.h` | `EventType`, `Event`, `IEventListener`, `EventBus` |
| `bridge.h` | `VideoFrameData` (with cross-DLL safe serialization), `Bridge` |
| `device_registry.h` | `Device`, `DeviceType`, `InputType`, `DeviceRegistry` |
| `iui_provider.h` | `IUIProvider` — interface for plugins that provide Qt UI tabs |

---

## Video Pipeline (Windows)

### Encoding Path

```
SteamVR Compositor
  │  Present(SharedTextureHandle)
  ▼
COpenDriverHMD::Present()
  │  Stores texture handle, wakes encoder thread
  ▼
VideoEncoderLoop() [THREAD_PRIORITY_HIGHEST]
  │  1. Opens shared DX11 texture
  │  2. Creates staging texture + NV12 conversion
  │  3. Feeds to Media Foundation Transform (MFT)
  │  4. Collects H.264 Annex-B output
  │  5. Stores in m_pending_packet
  ▼
VideoSenderLoop() [THREAD_PRIORITY_ABOVE_NORMAL]
  │  Sends packet via IPC (Named Pipe)
  ▼
Bridge::ReceiveLoop()
  │  Receives VIDEO_PACKET
  │  Creates VideoFrameData, serializes to vector<uint8_t>
  │  Publishes EVENT_TYPE::VIDEO_FRAME
  ▼
Plugin::OnEvent(VIDEO_FRAME)
  │  Deserializes VideoFrameData from vector<uint8_t>
  │  Extracts SPS/PPS sequence headers
  │  Enqueues to TcpVideoSender
  ▼
TcpVideoSender::SendLoop()
  │  Writes "ODV1" framed packets over TCP
  ▼
Android MediaCodec decoder
```

### Adaptive Bitrate

The driver implements adaptive bitrate control in `MaybeAdaptBitrate()`:

- **Congestion detection**: avg send time > 2ms, or >5% slow/overwritten frames
- **Down-ramp**: Immediately reduce bitrate by 10%, clamp to `min_bitrate_mbps`
- **Up-ramp**: After 3 consecutive good windows, increase by 5%, clamp to `max_bitrate_mbps`
- **Reconfiguration**: Sets `m_reconfigure_requested = true`, encoder thread will reinitialize MFT

### Cross-DLL std::any Issue (CRITICAL)

`VideoFrameData` is defined in `bridge.h` and compiled into both the runner executable and plugin DLLs. On MSVC, each module gets its own RTTI for user-defined types, causing `std::any_cast<VideoFrameData>` to fail silently (returns `nullptr`) across DLL boundaries.

**Solution**: Serialize `VideoFrameData` to a flat `std::vector<uint8_t>` before storing in `std::any`. Standard library types share RTTI across modules when using the dynamic CRT (`/MD`). See `VideoFrameData::Serialize()` / `Deserialize()`.

---

## Event Bus System

The `EventBus` is the central communication backbone. All inter-component communication flows through it.

### Event Types

| Type | Hex | Payload | Direction |
|------|-----|---------|-----------|
| `CORE_STARTUP` | 0x0000 | none | Core → Plugins |
| `CORE_SHUTDOWN` | 0x0001 | none | Core → Plugins |
| `PLUGIN_LOADED` | 0x1000 | `string` (name) | Core → UI |
| `PLUGIN_UNLOADED` | 0x1001 | `string` (name) | Core → UI |
| `PLUGIN_ERROR` | 0x1002 | `PluginErrorData` | Core → UI |
| `LOG_*` | 0x1010-1015 | `LogMessage` | Any → UI |
| `DEVICE_CONNECTED` | 0x3000 | `string` (device_id) | Plugin → Bridge |
| `DEVICE_DISCONNECTED` | 0x3001 | `string` (device_id) | Plugin → Bridge |
| `POSE_UPDATE` | 0x3002 | `IPCPoseData` | Plugin → Bridge |
| `INPUT_UPDATE` | 0x3003 | `IPCInputUpdate` | Plugin → Bridge |
| `VIDEO_FRAME` | 0x4000 | `vector<uint8_t>` (serialized) | Bridge → Plugin |
| `UI_SHOW_ENCODER_SETTINGS` | 0x5000 | none | Plugin → UI |

### Usage Pattern

```cpp
// Subscribe in OnInitialize:
context->GetEventBus().Subscribe(EventType::VIDEO_FRAME, this);

// Handle in OnEvent:
void MyPlugin::OnEvent(const Event& event) {
    if (event.type == EventType::VIDEO_FRAME) {
        const auto* raw = std::any_cast<std::vector<uint8_t>>(&event.data);
        VideoFrameData frame;
        VideoFrameData::Deserialize(*raw, frame);
        // ... process frame.nal_data
    }
}

// Publish:
Event evt(EventType::POSE_UPDATE, "my_plugin");
evt.data = pose_data;
context->GetEventBus().Publish(evt);
```

### Thread Safety

- `Subscribe`/`Unsubscribe`: mutex-protected
- `Publish`: takes a snapshot of listeners under lock, then invokes callbacks outside lock (avoids deadlocks)
- Callbacks may be invoked from **any thread** — plugins must handle their own synchronization

---

## Plugin System

### Lifecycle

```
PluginLoader::Load(path)
  ├─ LoadLibrary / dlopen
  ├─ Resolve CreatePlugin / DestroyPlugin
  ├─ plugin->OnInitialize(context)
  │   ├─ Subscribe to events
  │   ├─ Register devices
  │   └─ Start background threads
  ├─ plugin->OnTick(dt)  [called every frame]
  └─ plugin->OnShutdown()
      ├─ Unsubscribe events
      ├─ Unregister devices
      └─ Stop threads
```

### Plugin Directory Structure

```
%APPDATA%/opendriver/plugins/
  └─ hmdek/
      ├─ plugin.json          ← Plugin manifest
      └─ android_hmd_plugin.dll
```

### plugin.json Format

```json
{
    "name": "android_hmd",
    "version": "1.0.0",
    "description": "Android phone as VR HMD",
    "author": "OpenDriver Team",
    "entry_point": "android_hmd_plugin.dll",
    "min_core_version": "0.2.0"
}
```

### Implementing a Plugin

1. Include `<opendriver/core/plugin_interface.h>`
2. Implement `IPlugin` (override at minimum: `GetName`, `GetVersion`, `GetDescription`, `GetAuthor`, `OnInitialize`, `OnShutdown`, `IsActive`)
3. Export factory functions:

```cpp
extern "C" __declspec(dllexport) opendriver::core::IPlugin* CreatePlugin() {
    return new MyPlugin();
}
extern "C" __declspec(dllexport) void DestroyPlugin(opendriver::core::IPlugin* p) {
    delete p;
}
```

### Hot-Reload Support

Plugins may implement `ExportState()` / `ImportState(void*)` to preserve state across reloads. The loader calls `ExportState()` before unloading and `ImportState()` after re-initialization.

---

## Threading Model

| Thread | Owner | Priority | Purpose |
|--------|-------|----------|---------|
| **Main / UI** | `opendriver_runner` | Normal | Qt event loop, Runtime::Tick(), plugin OnTick() |
| **Bridge Receive** | `Bridge` | Normal | IPC message polling (2ms sleep), VIDEO_FRAME publishing |
| **Video Encoder** | `COpenDriverHMD` | Highest | DX11 texture → H.264 encoding |
| **Video Sender** | `COpenDriverHMD` | Above Normal | IPC send of encoded packets |
| **UDP Tracking** | `UdpReceiver` | Normal | Receive gyroscope data from phone |
| **TCP Video** | `TcpVideoSender` | Normal | Stream H.264 frames to phone |

### Thread Safety Rules

1. **EventBus callbacks** can fire on any thread — never assume UI thread
2. Use `context->PostToMainThread(fn)` to schedule work on the UI/main thread
3. `DeviceRegistry` is internally mutex-protected
4. Plugin `OnTick()` is always called from the main thread

---

## IPC Bridge

The Bridge uses **Named Pipes** (Windows) for communication between the SteamVR driver process and the runner process.

### Message Format

```
IPCMessage {
    IPCMessageType type;      // uint32_t enum
    vector<uint8_t> data;     // payload
}
```

### Message Types

| Type | Direction | Payload |
|------|-----------|---------|
| `DEVICE_ADDED` | Runner → Driver | JSON device descriptor |
| `DEVICE_REMOVED` | Runner → Driver | JSON device ID |
| `POSE_UPDATE` | Runner → Driver | `IPCPoseData` (binary) |
| `INPUT_UPDATE` | Runner → Driver | `IPCInputUpdate` (binary) |
| `VIDEO_PACKET` | Driver → Runner | Raw H.264 NAL units |
| `HAPTIC_EVENT` | Driver → Runner | `IPCHapticEvent` (binary) |

---

## HMDek Plugin (Android HMD)

### Network Protocol

**Tracking (UDP, port configurable)**:
- Phone sends orientation packets (quaternion + angular velocity)
- `UdpReceiver` parses and passes to `PoseProcessor`
- `PoseProcessor` applies deadzone filtering and EMA smoothing

**Video (TCP, port configurable)**:
- `TcpVideoSender` accepts one client connection
- Frame format: `"ODV1"` magic (4B) + frame size (4B, little-endian) + H.264 NAL data
- SPS/PPS sequence headers are cached and resent on new client connection and periodically
- Frame queue limited to `MAX_QUEUE = 3`; non-keyframe drops on overflow

### TCP Frame Wire Format

```
┌──────────┬───────────┬──────────────────────┐
│ "ODV1"   │ size (LE) │   H.264 NAL units    │
│ 4 bytes  │ 4 bytes   │   variable length     │
└──────────┴───────────┴──────────────────────┘
```

### PoseProcessor

Applies these transformations to raw phone orientation:
1. **Deadzone filter**: Ignores rotational changes below threshold (reduces jitter)
2. **EMA smoothing**: Exponential moving average on quaternion components
3. **Angular velocity calculation**: Derived from consecutive quaternion differences

---

## Configuration Schema

Configuration is stored in `%APPDATA%/opendriver/config.json`:

```json
{
    "video_encoding": {
        "profile": "90_stable",
        "adaptive_bitrate": true,
        "bitrate_mbps": 30,
        "min_bitrate_mbps": 12,
        "max_bitrate_mbps": 80,
        "preset": "ultrafast"
    },
    "plugins": {
        "android_hmd": {
            "tcp_port": 9944,
            "udp_port": 9943,
            "smoothing_factor": 0.3,
            "deadzone_degrees": 0.15
        }
    }
}
```

### Performance Profiles

| Profile | Min Bitrate | Max Bitrate | Default Bitrate |
|---------|-------------|-------------|-----------------|
| `90_stable` | 8 Mbps | 80 Mbps | 30 Mbps |
| `120_fast` | 20 Mbps | 100 Mbps | 45 Mbps |

---

## Build & Install

### Prerequisites

- Visual Studio 2022 with C++ Desktop workload
- CMake 3.16+
- Qt6 for MSVC x64

### Build Commands

```powershell
# Configure
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release `
      -DQt6_DIR="C:\Qt\6.x\msvc2022_64\lib\cmake\Qt6"

# Build
cmake --build build --config Release --parallel

# Build plugin (from plugins/hmdek/)
cmake -B build -A x64 -DWITH_QT_UI=ON `
      -DQt6_DIR="C:\Qt\6.x\msvc2022_64\lib\cmake\Qt6"
cmake --build build --config Release
```

### Install to SteamVR

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install_driver.ps1
```

### Run Tests

```powershell
cmake -B build -A x64 -DBUILD_TESTING=ON -DOPENDRIVER_BUILD_GUI=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

---

## Troubleshooting

### Video not reaching Android phone

1. Check `opendriver.log` for `VIDEO_FRAME received` messages from the plugin
2. If no messages: the `std::any_cast` deserialization may be failing — ensure both core and plugin are built with the serialization fix
3. If messages appear but phone is black: check TCP connection status in plugin logs
4. Verify Android app is connecting to the correct IP/port

### SteamVR driver not loading

1. Verify `driver/driver.vrdrivermanifest` exists
2. Check `driver/bin/win64/driver_opendriver.dll` exists
3. Run `vrpathreg.exe show` to verify registration
4. Check SteamVR logs at `%LOCALAPPDATA%/Steam/logs/`

### Plugin not appearing in dashboard

1. Verify plugin directory structure: `%APPDATA%/opendriver/plugins/<name>/plugin.json`
2. Check `plugin.json` has valid `entry_point` field
3. Check `opendriver.log` for plugin loading errors
4. Ensure DLL dependencies (Qt DLLs, CRT) are available

### HMD tracking jittery

1. Increase `deadzone_degrees` in config (default 0.15)
2. Decrease `smoothing_factor` (lower = more smoothing, default 0.3)
3. Check UDP packet loss — phone should be on same local network
