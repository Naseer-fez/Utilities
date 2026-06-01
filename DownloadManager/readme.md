# DownloadManager

> **A comprehensive desktop app (GUI + CLI) to download media from Spotify and YouTube.**

DownloadManager provides a smooth experience for grabbing Spotify playlists and YouTube videos. It features a modern dark-themed GUI and a robust CLI mode for headless bulk downloads.

---

## 🚀 Features

- **Spotify Playlist Downloader**: Use [spotDL](https://github.com/spotDL/spotify-downloader) to download tracks via CSV.
- **YouTube Video Downloader**: Leverage [yt-dlp](https://github.com/yt-dlp/yt-dlp) for multiple video formats.
- **Modern GUI**: Real-time progress, setup wizard, and robust tabbed settings.
- **CLI & Auto-Retry**: Automated batch downloads and retries (`musicscript.py`, `filerunn.py`).
- **Standalone Installer**: Package into `.msi` or `.zip` with `cx_Freeze`.

## 🛠️ Tech Stack

- **Python**: Core logic.
- **PySide6**: User interface.
- **spotDL / yt-dlp**: Media downloading engines.

## 📦 Quick Start

### Prerequisites
- Python 3.10+
- **ffmpeg** (Must be added to system PATH)

### Installation
```bash
cd DownloadManager
pip install -r requirements.txt
python download_manager_gui.py
```
Or use the batch launcher: `launch_download_manager_gui.bat`

---

## 🔑 Authentication Notes (Spotify)

### Best Option: Client Credentials
Save a Spotify **Client ID** and **Client Secret** in the GUI under Settings > Spotify Credentials. These are long-lived and prevent constant token refreshing.

### Fallback: Auth Tokens
1. Paste your Spotify auth token in the GUI, or set the `SPOTIFY_AUTH_TOKEN` environment variable.
2. Run from CLI:
   ```powershell
   python musicscript.py --workers 2 --limit 40
   ```
   Or to auto-loop until complete:
   ```powershell
   python filerunn.py
   ```

**Auth Priority Order:**
1. GUI saved `SPOTIFY_CLIENT_ID` / `SPOTIFY_CLIENT_SECRET`
2. `download_manager_credentials.json`
3. `SPOTIFY_AUTH_TOKEN` env variable
4. Cached token from `spotify_auth_token.txt`

If an auth token expires, the downloader will fallback to spotDL's default client credentials to ensure the batch completes without user intervention.

---

> **Disclaimer**: Use this app only for media you own, created yourself, or are otherwise allowed to download.