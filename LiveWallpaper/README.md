# LiveWallpaper

> **Dynamic and immersive live wallpapers for your desktop**

LiveWallpaper is a high-performance desktop utility that renders dynamic, shader-based wallpapers directly onto your Windows desktop background.

---

## 🚀 Features

- **HLSL Shaders**: Uses direct hardware acceleration to render visual effects (`tunnel_wallpaper.hlsl`, `default_wallpaper.hlsl`).
- **Rust Core Module**: Leverages Rust (`live_wallpaper_rust.dll`) for memory-safe, rapid backend operations.
- **Low Resource Usage**: Optimized C++ and Rust implementation to minimize CPU/GPU overhead.

## 🛠️ Tech Stack

- **C++**: Application windowing and rendering setup.
- **Rust**: Core logic DLL.
- **HLSL**: Shader logic for visuals.
- **CMake**: Build configuration system.

## 📦 Getting Started

### Prerequisites

- CMake 3.10+
- MSVC (Visual Studio)
- Rust toolchain (`cargo`)

### Building the Project

1. Run the comprehensive build script:
   ```cmd
   build.bat
   ```
2. The executable `LiveWallpaper.exe` will be generated and ready to use.

Refer to `BUILD_TEST_GUIDE.md` for in-depth testing and development instructions.
