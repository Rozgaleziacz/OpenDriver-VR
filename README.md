# OpenDriver VR Framework 🚀

<img width="1365" height="577" alt="OpenDriver Banner" src="https://github.com/user-attachments/assets/25d4af77-fe2a-4527-aa25-d1b7eeeb96cd" />

![Built with C++](https://img.shields.io/badge/Built%20with-C%2B%2B-blue?style=for-the-badge&logo=c%2B%2B)
![SteamVR](https://img.shields.io/badge/SteamVR-compatible-black?style=for-the-badge)
![Tracking](https://img.shields.io/badge/Tracking-6DOF-success?style=for-the-badge)
![Linux](https://img.shields.io/badge/Linux-Supported-orange?style=for-the-badge)
![Kinect](https://img.shields.io/badge/Kinect-supported-purple?style=for-the-badge)
![Webcam](https://img.shields.io/badge/Webcam-supported-informational?style=for-the-badge)

OpenDriver is a modular, open-source SteamVR (OpenVR) driver architecture designed for DIY creators, experimenters, and hardware developers. It provides a robust core to bridge custom tracking solutions (Kinect, phone sensors, webcams) directly into SteamVR via a clean, high-performance plugin API.

## 🧠 Vision & Concept
OpenDriverVR aims to become a flexible, open platform for experimental VR setups — from cheap DIY builds (using phones as HMDs) to advanced custom tracking research using Kinect or computer vision.

---

## 🧪 Current Status
⚠️ **Work in progress**

- [x] **HMD Driver Core**: Virtual Display support (No DRM lease required).
- [x] **IPC Bridge**: High-speed Unix Socket communication.
- [x] **Plugin System**: Dynamic loading & Hot-Reloading.
- [x] **Input Mapper**: Map arbitrary data to OpenVR inputs.
- [x] **Tracking Core**: 6DOF support.
- [ ] **Video Pipeline**: Low-latency video streaming to external HMDs (Planned).

---

## 🏗 High-Level Architecture
1.  **SteamVR Driver (`driver_opendriver.so`)**: A lightweight binary loaded by SteamVR. It acts as an IPC client receiving device data and pose updates.
2.  **Core Runtime (`opendriver_gui`)**: The central Dashboard handling plugin orchestration and IPC serving.
3.  **Plugins (`.so`)**: Independent modules implementing hardware-specific logic.

---

## 🚀 Key Features
- **🧠 Custom HMD Emulation**: Fully implements `IVRDisplayComponent` and `IVRVirtualDisplay`.
- **🎯 6DOF Tracking**: Smooth position and rotation injection for headsets and trackers.
- **📷 Tracking Agnostic**: Use Kinect, Webcams (AI tracking), or phone IMUs as data sources.
- **🔌 Developer Friendly**: Hot-reload plugins while SteamVR is running.
- **🌐 Network Ready**: Easy integration with external Python/OpenCV apps via UDP.

---

## 🛠 Build & Installation

### Prerequisites
You need a C++17 compiler and the following system packages:

- **CMake** (3.16+)
- **Qt6 Widgets** development libraries
- **Git**

#### On Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-declarative-dev libqt6widgets6 git
```

#### On Arch Linux:
```bash
sudo pacman -S base-devel cmake qt6-base git
```

### Compilation
```bash
git clone https://github.com/rozgaleziacz/OpenDriver-VR.git --recursive
cd OpenDriver-VR
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Driver Installation
To link the driver to your SteamVR installation:
```bash
sudo ./scripts/install_driver.sh
```

---

## 🔌 Plugin Development Guide
Creating a plugin is the easiest way to add your own hardware. Check the [DEVELOPER_GUIDE.md](/docs/DEVELOPER_GUIDE.md) for a deep dive.

### Minimal `IPlugin` Sample
```cpp
#include <opendriver/core/plugin_interface.h>

class MyPlugin : public opendriver::core::IPlugin {
public:
    const char* GetName() const override { return "my_device"; }
    
    bool OnInitialize(opendriver::core::IPluginContext* context) override {
        opendriver::core::Device d;
        d.id = "my_id";
        d.type = opendriver::core::DeviceType::GENERIC_TRACKER;
        context->RegisterDevice(d);
        return true;
    }

    void OnTick(float dt) override {
        context->UpdatePose("my_id", x, y, z, qw, qx, qy, qz);
    }
};
```

---

## 📜 License
This project is licensed under the **MIT License**.

## 🤝 Contributing
Contributions are welcome! Whether it's adding new tracking sources, improving latency, or porting to other platforms, feel free to open a Pull Request.
