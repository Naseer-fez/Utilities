from __future__ import annotations

import sys
from pathlib import Path

from cx_Freeze import Executable, setup


ROOT = Path(__file__).resolve().parent
PRODUCT_NAME = "Download Manager"
VERSION = "1.1.0"
EXE_NAME = "DownloadManager.exe"
ICON_FILE = "icon.ico" if (ROOT / "icon.ico").exists() else "icon.png"

_candidate_files: list = [
    "icon.png",
    "playlist.csv",
    "musicscript.py",
    "youtube_video_downloader.py",
    ("installer/README.txt", "README.txt"),
]
if (ROOT / "icon.ico").exists():
    _candidate_files.append(("icon.ico", "icon.ico"))

# Only include files that actually exist so the build never fails on
# optional assets (e.g. playlist.csv may not be tracked in git).
include_files = []
for entry in _candidate_files:
    src = entry[0] if isinstance(entry, tuple) else entry
    if (ROOT / src).exists():
        include_files.append(entry)

build_exe_options = {
    "includes": [
        "PySide6.QtCore",
        "PySide6.QtGui",
        "PySide6.QtWidgets",
    ],
    "packages": [
        "argparse",
        "base64",
        "concurrent.futures",
        "ctypes",
        "csv",
        "datetime",
        "gui",
        "hashlib",
        "json",
        "os",
        "pathlib",
        "queue",
        "runpy",
        "shutil",
        "spotdl",
        "subprocess",
        "sys",
        "threading",
        "typing",
        "yt_dlp",
    ],
    "include_files": include_files,
    "excludes": ["tkinter"],
}

bdist_msi_options = {
    "add_to_path": False,
    "all_users": False,
    "initial_target_dir": rf"[LocalAppDataFolder]\Programs\{PRODUCT_NAME}",
    "install_icon": "icon.ico" if (ROOT / "icon.ico").exists() else None,
    "launch_on_finish": True,
    "license_file": "installer/LICENSE.rtf",
    "output_name": f"DownloadManagerSetup-{VERSION}-win64.msi",
    "product_name": PRODUCT_NAME,
    "product_version": VERSION,
    "summary_data": {
        "author": "FEZ NASEER",
        "comments": "Installs Download Manager with a first-run setup wizard.",
        "keywords": "Download Manager;Music;YouTube;Spotify",
    },
    "upgrade_code": "{DB21E8FF-71B4-4676-844E-F8897F49E836}",
    "data": {
        "Shortcut": [
            (
                "DesktopShortcut",
                "DesktopFolder",
                PRODUCT_NAME,
                "TARGETDIR",
                f"[TARGETDIR]{EXE_NAME}",
                None,
                "Open Download Manager",
                None,
                None,
                None,
                None,
                "TARGETDIR",
            )
        ]
    },
}

base = "gui" if sys.platform == "win32" else None

setup(
    name="DownloadManager",
    version=VERSION,
    author="FEZ NASEER",
    description="Download Manager for Spotify and YouTube",
    options={
        "build_exe": build_exe_options,
        "bdist_msi": bdist_msi_options,
    },
    executables=[
        Executable(
            "download_manager_gui.py",
            base=base,
            target_name=EXE_NAME,
            icon=ICON_FILE,
            shortcut_name=PRODUCT_NAME,
            shortcut_dir="ProgramMenuFolder",
        )
    ],
)
