# Utilities Repository

> **A centralized collection of developer tools, utilities, and automation scripts.**

Welcome to the Utilities repository! This project serves as a monorepo for various specialized applications designed to improve workflow, enhance desktop customization, and streamline everyday tasks.

---

## 📂 Included Projects

This repository hosts several diverse tools across different tech stacks (Python, C++, Rust, web technologies). Each project operates independently and contains its own README for specific setup and execution instructions.

| Project | Description | Primary Tech Stack |
|---------|-------------|--------------------|
| **[DevControl](./DevControl)** | Telemetry and API monitoring engine with AI log extraction. | Python, SQLite, C++ |
| **[DownloadManager](./DownloadManager)** | Advanced audio and video downloader for Spotify and YouTube. | Python, PySide6 |
| **[FileFinder](./FileFinder)** | Blazingly fast desktop file lookup utility. | C++ |
| **[Focus](./Focus)** | Productivity application to manage deep work sessions. | Python, C++ |
| **[LiveWallpaper](./LiveWallpaper)** | High-performance, shader-based dynamic wallpaper renderer. | C++, Rust, HLSL |
| **[MusicPlayer](./MusicPlayer)** | Custom local audio player with an NSIS-packaged installer. | Python, NSIS |
| **[PdfEditor](./PdfEditor)** | Toolset for straightforward PDF manipulation. | Python |
| **[QuickFinder](./QuickFinder)** | Fast and lightweight desktop search utility. | C++ |
| **[RagStudy](./RagStudy)** | AI-powered Retrieval-Augmented Generation application for studying. | Python, Web Tech |
| **[Reminder](./Reminder)** | Fast, memory-safe task and event scheduler. | Rust |
| **[Tracker](./Tracker)** | Lightweight utility for tracking events and system metrics. | Rust |

## 🚀 Getting Started

Each project folder has its own `README.md` and build instructions. Navigate to the desired utility to view setup, dependencies, and execution commands.

For example, to explore the Live Wallpaper app:
```bash
cd LiveWallpaper
# Read the project's README.md
```

## 🛠️ Global Requirements

While each project has its own dependencies, common tools used across this repository include:
- **Python 3.10+**
- **Rust Toolchain (`cargo`)**
- **C++ Compiler (MSVC / MinGW) & CMake**
- **ffmpeg** (for media-related projects)

## 🤝 Contribution Guidelines

- Follow the coding standards specific to the language of the tool.
- Ensure all new features or bug fixes include an updated `README.md` inside their respective directories.
- Ensure your changes adhere to the universal `.gitignore` rules at the root level.
