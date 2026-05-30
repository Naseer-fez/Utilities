"""Shared backend helpers: config, credentials, DPAPI, process management."""
from __future__ import annotations

import base64
import ctypes
import ctypes.wintypes
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

BASE_DIR = Path(__file__).resolve().parent.parent
MUSIC_SCRIPT = BASE_DIR / "musicscript.py"
YOUTUBE_SCRIPT = BASE_DIR / "youtube_video_downloader.py"
LEGACY_CONFIG_PATH = BASE_DIR / "download_manager_config.json"
LEGACY_CREDENTIALS_PATH = BASE_DIR / "download_manager_credentials.json"
LEGACY_SPOTIFY_HISTORY_PATH = BASE_DIR / "download_history.json"
LEGACY_SPOTIFY_FAILED_CSV = BASE_DIR / "failed.csv"
LEGACY_YOUTUBE_HISTORY_PATH = BASE_DIR / "youtube_download_history.json"
LEGACY_YOUTUBE_FAILED_CSV = BASE_DIR / "youtube_failed.csv"


def _user_data_dir() -> Path:
    if os.name == "nt":
        root = os.environ.get("LOCALAPPDATA")
        if root:
            return Path(root) / "Download Manager"
        return Path.home() / "AppData" / "Local" / "Download Manager"
    return Path.home() / ".download-manager"


def _default_download_dir(folder_name: str) -> Path:
    base = Path.home() / folder_name
    if not base.exists():
        base = Path.home()
    return base / "Download Manager"


USER_DATA_DIR = _user_data_dir()
CONFIG_PATH = USER_DATA_DIR / "download_manager_config.json"
CREDENTIALS_PATH = USER_DATA_DIR / "download_manager_credentials.json"
GUI_YOUTUBE_INPUT = USER_DATA_DIR / "gui_youtube_urls.txt"
SPOTIFY_HISTORY_PATH = USER_DATA_DIR / "download_history.json"
SPOTIFY_FAILED_CSV = USER_DATA_DIR / "failed.csv"
SPOTIFY_ERROR_DIR = USER_DATA_DIR / "spotdl_errors"
YOUTUBE_HISTORY_PATH = USER_DATA_DIR / "youtube_download_history.json"
YOUTUBE_FAILED_CSV = USER_DATA_DIR / "youtube_failed.csv"
YOUTUBE_ERROR_DIR = USER_DATA_DIR / "youtube_errors"
YOUTUBE_ARCHIVE_FILE = USER_DATA_DIR / "youtube_archive.txt"
DEFAULT_MUSIC_OUTPUT = _default_download_dir("Music")
DEFAULT_VIDEO_OUTPUT = _default_download_dir("Videos")
APP_NAME = "Download Manager"
SECRET_KEYS = {"spotify_auth_token", "spotify_client_id", "spotify_client_secret"}


class SecretBox(ctypes.Structure):
    _fields_ = [
        ("cbData", ctypes.wintypes.DWORD),
        ("pbData", ctypes.POINTER(ctypes.c_char)),
    ]


def is_windows() -> bool:
    return os.name == "nt"


def dpapi_protect(value: str) -> dict[str, str]:
    if not value:
        return {"encoding": "plain", "value": ""}
    if not is_windows():
        return {"encoding": "plain", "value": value}
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    raw = value.encode("utf-8")
    buffer = ctypes.create_string_buffer(raw)
    in_blob = SecretBox(len(raw), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_char)))
    out_blob = SecretBox()
    if not crypt32.CryptProtectData(ctypes.byref(in_blob), None, None, None, None, 0, ctypes.byref(out_blob)):
        return {"encoding": "plain", "value": value}
    try:
        protected = ctypes.string_at(out_blob.pbData, out_blob.cbData)
        return {"encoding": "dpapi", "value": base64.b64encode(protected).decode("ascii")}
    finally:
        kernel32.LocalFree(out_blob.pbData)


def dpapi_unprotect(payload: dict[str, str]) -> str:
    encoding = payload.get("encoding", "plain")
    value = payload.get("value", "")
    if not value:
        return ""
    if encoding != "dpapi" or not is_windows():
        return value
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    raw = base64.b64decode(value)
    buffer = ctypes.create_string_buffer(raw)
    in_blob = SecretBox(len(raw), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_char)))
    out_blob = SecretBox()
    if not crypt32.CryptUnprotectData(ctypes.byref(in_blob), None, None, None, None, 0, ctypes.byref(out_blob)):
        return ""
    try:
        return ctypes.string_at(out_blob.pbData, out_blob.cbData).decode("utf-8")
    finally:
        kernel32.LocalFree(out_blob.pbData)


def read_json(path: Path, default: dict) -> dict:
    if not path.exists():
        return default
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else default
    except (OSError, json.JSONDecodeError):
        return default


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    with temp.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False, sort_keys=True)
        f.write("\n")
    os.replace(temp, path)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass


def migrate_legacy_user_files() -> None:
    for source, target in (
        (LEGACY_CONFIG_PATH, CONFIG_PATH),
        (LEGACY_CREDENTIALS_PATH, CREDENTIALS_PATH),
        (LEGACY_SPOTIFY_HISTORY_PATH, SPOTIFY_HISTORY_PATH),
        (LEGACY_SPOTIFY_FAILED_CSV, SPOTIFY_FAILED_CSV),
        (LEGACY_YOUTUBE_HISTORY_PATH, YOUTUBE_HISTORY_PATH),
        (LEGACY_YOUTUBE_FAILED_CSV, YOUTUBE_FAILED_CSV),
    ):
        if not source.exists() or target.exists():
            continue
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        except OSError:
            pass


def default_config() -> dict[str, Any]:
    return {
        "setup_wizard_completed": False,
        "spotify_csv": str(BASE_DIR / "playlist.csv"),
        "spotify_output": str(DEFAULT_MUSIC_OUTPUT),
        "spotify_workers": 2,
        "spotify_limit": "",
        "spotify_format": "mp3",
        "spotify_bitrate": "320k",
        "spotify_retries": 2,
        "spotify_timeout": 20,
        "spotify_preload": False,
        "spotify_use_cookies": False,
        "spotify_cookie_file": str(USER_DATA_DIR / "cookies.txt"),
        "youtube_input_file": "",
        "youtube_urls": "",
        "youtube_output": str(DEFAULT_VIDEO_OUTPUT),
        "youtube_workers": 3,
        "youtube_limit": "",
        "youtube_quality": "1080",
        "youtube_fragments": 4,
        "youtube_retries": 2,
        "youtube_timeout": 60,
        "youtube_allow_playlists": False,
        "youtube_subs": False,
        "youtube_use_cookies": False,
        "youtube_cookie_file": str(USER_DATA_DIR / "cookies.txt"),
        "youtube_browser_cookies": "",
        "youtube_rate_limit": "",
    }


def load_config() -> dict[str, Any]:
    migrate_legacy_user_files()
    config = default_config()
    saved = read_json(CONFIG_PATH, {})
    if not saved and LEGACY_CONFIG_PATH.exists():
        saved = read_json(LEGACY_CONFIG_PATH, {})
    config.update(saved)
    return config


def mask_secret(value: str) -> str:
    if not value:
        return "not saved"
    if len(value) <= 8:
        return "*" * len(value)
    return f"{value[:4]}...{value[-4:]}"


def history_counts(path: Path) -> tuple[int, int]:
    data = read_json(path, {})
    s = data.get("success", {})
    f = data.get("failed", {})
    return (len(s) if isinstance(s, dict) else 0, len(f) if isinstance(f, dict) else 0)


def load_credentials() -> dict[str, str]:
    migrate_legacy_user_files()
    raw = read_json(CREDENTIALS_PATH, {})
    creds: dict[str, str] = {}
    for key in SECRET_KEYS:
        item = raw.get(key, {})
        if isinstance(item, dict):
            creds[key] = dpapi_unprotect(item)
        elif isinstance(item, str):
            creds[key] = item
        else:
            creds[key] = ""
    return creds


def save_credentials(token: str, client_id: str, client_secret: str) -> None:
    payload: dict[str, Any] = {
        "version": 1,
        "storage": "Windows DPAPI" if is_windows() else "plain local JSON",
    }
    for key, val in {
        "spotify_auth_token": token.strip(),
        "spotify_client_id": client_id.strip(),
        "spotify_client_secret": client_secret.strip(),
    }.items():
        payload[key] = dpapi_protect(val)
    write_json(CREDENTIALS_PATH, payload)


@dataclass
class ProcessState:
    mode: str
    process: subprocess.Popen
    started_at: float
    total: int = 0
    completed: int = 0
    ok: int = 0
    failed: int = 0
    skipped: int = 0
