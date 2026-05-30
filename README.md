# Utilities

A collection of useful tools and scripts.

---

## 📥 DownloadManager

A desktop app (GUI + CLI) to download music from Spotify playlists and YouTube videos. Built with Python and PySide6.

### Features

- **Spotify Playlist Downloader** — Provide a Spotify playlist CSV and download all tracks as MP3 using [spotDL](https://github.com/spotDL/spotify-downloader)
- **YouTube Video Downloader** — Download YouTube videos in various formats using [yt-dlp](https://github.com/yt-dlp/yt-dlp)
- **Modern GUI** — Tabbed interface with a setup wizard, dark theme, and real-time progress
- **CLI Mode** — Run headless via `musicscript.py` for batch/automated downloads
- **Auto-Retry** — `filerunn.py` loops batches until the entire playlist is downloaded
- **Build as Installer** — Package into a standalone `.msi` or portable `.zip` with `cx_Freeze`

### Prerequisites

- **Python 3.10+**
- **ffmpeg** — Must be on your PATH ([download](https://ffmpeg.org/download.html))

### Quick Start

```bash
# 1. Clone the repo
git clone https://github.com/Naseer-fez/Utilities-.git
cd Utilities-/DownloadManager

# 2. Install dependencies
pip install -r requirements.txt

# 3. Run the GUI
python download_manager_gui.py
```

Or use the batch launcher:

```
launch_download_manager_gui.bat
```

### CLI Usage

```bash
# Download up to 40 songs with 2 workers
python musicscript.py --workers 2 --limit 40

# Auto-loop until playlist is complete
python filerunn.py
```

### Project Structure

```
DownloadManager/
├── download_manager_gui.py   # GUI entry point
├── musicscript.py             # Core Spotify playlist downloader
├── youtube_video_downloader.py# YouTube downloader
├── filerunn.py                # Auto-retry batch runner
├── requirements.txt           # Python dependencies
├── setup.py                   # cx_Freeze build config
├── build_installer.ps1        # PowerShell script to build MSI/ZIP
├── launch_download_manager_gui.bat
├── icon.png
├── readme.md                  # Detailed auth & usage notes
├── installer/                 # Installer assets
│   ├── LICENSE.rtf
│   └── README.txt
└── gui/                       # PySide6 GUI modules
    ├── __init__.py
    ├── backend.py
    ├── main_window.py
    ├── settings_tab.py
    ├── setup_wizard.py
    ├── spotify_tab.py
    ├── theme.py
    └── youtube_tab.py
```

### Building the Installer

```powershell
# Requires cx_Freeze, PySide6, spotdl, yt-dlp, Pillow
cd DownloadManager
.\build_installer.ps1
```

---

> **Disclaimer**: Use this app only for media you own, created yourself, or are otherwise allowed to download.
