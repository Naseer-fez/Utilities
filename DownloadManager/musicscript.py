"""
Resumable Spotify playlist CSV downloader powered by spotDL.

Install commands:
    python -m pip install --upgrade spotdl

Optional FFmpeg helper:
    spotdl --download-ffmpeg

Notes:
    - Use this only for tracks you are allowed to download.
    - spotDL handles Spotify metadata, ID3 tags, and album art embedding.
    - This wrapper handles CSV parsing, concurrent execution, strict history,
      retries, and flat output into Downloaded_Music.
"""

from __future__ import annotations

import argparse
import base64
import csv
import ctypes
import ctypes.wintypes
import hashlib
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


BASE_DIR = Path(__file__).resolve().parent

DEFAULT_CSV_FILE = BASE_DIR / "playlist.csv"
DEFAULT_OUTPUT_DIR = BASE_DIR / "Downloaded_Music"
DEFAULT_HISTORY_FILE = BASE_DIR / "download_history.json"
DEFAULT_FAILED_CSV = BASE_DIR / "failed.csv"
DEFAULT_ERROR_DIR = BASE_DIR / "spotdl_errors"
DEFAULT_AUTH_TOKEN_FILE = BASE_DIR / "spotify_auth_token.txt"
CREDENTIALS_PATH = BASE_DIR / "download_manager_credentials.json"

DEFAULT_WORKERS = 2
DEFAULT_BITRATE = "320k"
DEFAULT_FORMAT = "mp3"
DEFAULT_RETRIES = 2
DEFAULT_PER_TRACK_TIMEOUT = 20 * 60
AUTH_RATE_LIMIT_EXIT_CODE = 88

TRACK_URI_HEADERS = ("Track URI", "Spotify URI", "URI", "uri")
TRACK_NAME_HEADERS = ("Track Name", "Name", "Title", "track_name")
ARTIST_HEADERS = ("Artist Name(s)", "Artist Names", "Artists", "Artist", "artist")
ALBUM_HEADERS = ("Album Name", "Album", "album")

history_lock = threading.RLock()
print_lock = threading.Lock()


class SecretBox(ctypes.Structure):
    _fields_ = [
        ("cbData", ctypes.wintypes.DWORD),
        ("pbData", ctypes.POINTER(ctypes.c_char)),
    ]


@dataclass(frozen=True)
class Track:
    uri: str
    query: str
    name: str
    artist: str
    album: str
    list_position: int

    @property
    def label(self) -> str:
        if self.artist and self.name:
            return f"{self.artist} - {self.name}"
        return self.name or self.uri


@dataclass(frozen=True)
class DownloadResult:
    ok: bool
    track: Track
    error: str = ""
    elapsed_seconds: float = 0.0
    skipped: bool = False
    rate_limited: bool = False


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def safe_print(message: str) -> None:
    with print_lock:
        print(message, flush=True)


def clean_error(text: str, max_chars: int = 4000) -> str:
    text = (text or "").replace("\r\n", "\n").strip()
    if len(text) <= max_chars:
        return text
    return text[-max_chars:]


def is_windows() -> bool:
    return os.name == "nt"


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


def read_saved_credentials(path: Path = CREDENTIALS_PATH) -> dict[str, str]:
    if not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8") as handle:
            raw = json.load(handle)
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(raw, dict):
        return {}

    credentials: dict[str, str] = {}
    for key in ("spotify_auth_token", "spotify_client_id", "spotify_client_secret"):
        item = raw.get(key, {})
        if isinstance(item, dict):
            credentials[key] = dpapi_unprotect(item).strip()
        elif isinstance(item, str):
            credentials[key] = item.strip()
        else:
            credentials[key] = ""
    return credentials


def read_auth_token_file(path: Path | None) -> str:
    if not path or not path.exists():
        return ""
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                return line
    except OSError:
        return ""
    return ""


def first_present(row: dict[str, str], names: tuple[str, ...]) -> str:
    for name in names:
        value = row.get(name)
        if value is not None and str(value).strip():
            return str(value).strip()
    return ""


def spotify_query_from_uri(uri: str) -> str:
    uri = uri.strip()
    if uri.startswith("spotify:track:"):
        track_id = uri.rsplit(":", 1)[-1]
        return f"https://open.spotify.com/track/{track_id}"
    return uri


def parse_csv(csv_path: Path) -> list[Track]:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    tracks: list[Track] = []
    seen: set[str] = set()

    with csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            raise ValueError(f"CSV has no header row: {csv_path}")

        first_column = reader.fieldnames[0]
        for index, row in enumerate(reader, start=1):
            uri = first_present(row, TRACK_URI_HEADERS) or str(row.get(first_column, "")).strip()
            name = first_present(row, TRACK_NAME_HEADERS)
            artist = first_present(row, ARTIST_HEADERS)
            album = first_present(row, ALBUM_HEADERS)

            if not uri:
                safe_print(f"[skip] Row {index}: missing Track URI")
                continue

            if uri in seen:
                safe_print(f"[skip] Row {index}: duplicate URI already queued: {uri}")
                continue

            seen.add(uri)
            tracks.append(
                Track(
                    uri=uri,
                    query=spotify_query_from_uri(uri),
                    name=name,
                    artist=artist,
                    album=album,
                    list_position=index,
                )
            )

    return tracks


def empty_history() -> dict[str, Any]:
    return {
        "version": 2,
        "created_at": utc_now(),
        "updated_at": utc_now(),
        "success": {},
        "failed": {},
    }


def normalize_history(raw: Any) -> dict[str, Any]:
    history = empty_history()
    if not isinstance(raw, dict):
        return history

    success = raw.get("success", {})
    failed = raw.get("failed", {})

    if isinstance(success, list):
        for uri in success:
            if isinstance(uri, str) and uri.strip():
                history["success"][uri.strip()] = {
                    "uri": uri.strip(),
                    "track_name": "",
                    "artist": "",
                    "album": "",
                    "file": "",
                    "completed_at": raw.get("updated_at") or utc_now(),
                }
    elif isinstance(success, dict):
        for uri, info in success.items():
            if isinstance(uri, str) and uri.strip():
                if isinstance(info, dict):
                    record = dict(info)
                else:
                    record = {}
                record.setdefault("uri", uri.strip())
                record.setdefault("completed_at", raw.get("updated_at") or utc_now())
                history["success"][uri.strip()] = record

    if isinstance(failed, list):
        for item in failed:
            if not isinstance(item, dict):
                continue
            uri = str(item.get("uri", "")).strip()
            if not uri:
                continue
            history["failed"][uri] = {
                "uri": uri,
                "track_name": item.get("track_name") or item.get("track") or "",
                "artist": item.get("artist", ""),
                "album": item.get("album", ""),
                "attempts": int(item.get("attempts", 0) or 0),
                "last_error": item.get("error") or item.get("last_error") or "",
                "updated_at": item.get("updated_at") or raw.get("updated_at") or utc_now(),
            }
    elif isinstance(failed, dict):
        for uri, info in failed.items():
            if not isinstance(uri, str) or not uri.strip():
                continue
            if isinstance(info, dict):
                record = dict(info)
            else:
                record = {}
            record.setdefault("uri", uri.strip())
            record.setdefault("attempts", 0)
            record.setdefault("updated_at", raw.get("updated_at") or utc_now())
            history["failed"][uri.strip()] = record

    history["created_at"] = raw.get("created_at") or history["created_at"]
    history["updated_at"] = raw.get("updated_at") or utc_now()
    return history


def load_history(history_path: Path) -> dict[str, Any]:
    if not history_path.exists():
        return empty_history()

    try:
        with history_path.open("r", encoding="utf-8") as handle:
            return normalize_history(json.load(handle))
    except json.JSONDecodeError as exc:
        backup = history_path.with_suffix(f".corrupt.{int(time.time())}.json")
        shutil.copy2(history_path, backup)
        safe_print(f"[warn] History JSON was invalid. Backed it up to {backup.name}.")
        safe_print(f"[warn] JSON error: {exc}")
        return empty_history()


def atomic_write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with temp_path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    os.replace(temp_path, path)


def write_failed_csv(path: Path, history: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    fields = [
        "uri",
        "track_name",
        "artist",
        "album",
        "attempts",
        "last_error",
        "updated_at",
    ]

    with temp_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for record in history.get("failed", {}).values():
            writer.writerow({field: record.get(field, "") for field in fields})

    os.replace(temp_path, path)


def mark_success(
    history_path: Path,
    failed_csv_path: Path,
    history: dict[str, Any],
    track: Track,
) -> None:
    with history_lock:
        history["success"][track.uri] = {
            "uri": track.uri,
            "track_name": track.name,
            "artist": track.artist,
            "album": track.album,
            "list_position": track.list_position,
            "completed_at": utc_now(),
        }
        history["failed"].pop(track.uri, None)
        history["updated_at"] = utc_now()
        atomic_write_json(history_path, history)
        write_failed_csv(failed_csv_path, history)


def mark_failed(
    history_path: Path,
    failed_csv_path: Path,
    history: dict[str, Any],
    track: Track,
    error: str,
) -> None:
    with history_lock:
        previous = history["failed"].get(track.uri, {})
        attempts = int(previous.get("attempts", 0) or 0) + 1
        history["failed"][track.uri] = {
            "uri": track.uri,
            "track_name": track.name,
            "artist": track.artist,
            "album": track.album,
            "list_position": track.list_position,
            "attempts": attempts,
            "last_error": clean_error(error, max_chars=2000),
            "updated_at": utc_now(),
        }
        history["updated_at"] = utc_now()
        atomic_write_json(history_path, history)
        write_failed_csv(failed_csv_path, history)


def short_hash(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8", errors="ignore")).hexdigest()[:10]


def error_file_for_track(error_dir: Path, track: Track) -> Path:
    return error_dir / f"{track.list_position:04d}_{short_hash(track.uri)}.txt"


def write_error_log(error_dir: Path, track: Track, error: str) -> None:
    if not error:
        return
    error_dir.mkdir(parents=True, exist_ok=True)
    error_file = error_file_for_track(error_dir, track)
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    with error_file.open("w", encoding="utf-8", errors="replace") as handle:
        handle.write(f"{timestamp}\n{track.query}\n")
        handle.write(clean_error(error, max_chars=8000))
        handle.write("\n")


def _is_real_python(exe: Path) -> bool:
    """Return True only if exe looks like a Python interpreter, not a frozen GUI exe."""
    name = exe.name.lower()
    return name.startswith("python") and name.endswith(".exe")


def python_has_spotdl(python_exe: Path) -> bool:
    try:
        completed = subprocess.run(
            [str(python_exe), "-c", "import spotdl"],
            capture_output=True,
            text=True,
            timeout=20,
        )
    except Exception:
        return False

    return completed.returncode == 0


def resolve_spotdl_runner() -> list[str] | None:
    candidates: list[Path] = []

    # When frozen, sys.executable is the GUI exe (e.g. DownloadManager.exe),
    # NOT a Python interpreter — calling it as a subprocess with -c/-m causes
    # an access-violation crash (exit code 0xC0000005).  Skip it entirely.
    if sys.executable and not getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable))

    scoop_python = Path.home() / "scoop" / "apps" / "python" / "current" / "python.exe"
    candidates.append(scoop_python)

    path_python = shutil.which("python")
    if path_python:
        candidates.append(Path(path_python))

    path_python3 = shutil.which("python3")
    if path_python3:
        candidates.append(Path(path_python3))

    path_spotdl = shutil.which("spotdl")
    if path_spotdl:
        spotdl_path = Path(path_spotdl)
        nearby_python = spotdl_path.parent.parent / "python.exe"
        candidates.append(nearby_python)

    seen: set[Path] = set()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved in seen or not resolved.exists():
            continue
        seen.add(resolved)
        # Guard: never use a frozen GUI exe as a Python runner
        if not _is_real_python(resolved):
            continue
        if python_has_spotdl(resolved):
            return [str(resolved), "-m", "spotdl"]

    return None


def build_spotdl_command(
    spotdl_runner: list[str],
    track: Track,
    output_dir: Path,
    error_dir: Path,
    args: argparse.Namespace,
) -> list[str]:
    output_template = str((output_dir / "{artists} - {title}.{output-ext}").resolve())

    command = [
        *spotdl_runner,
        "download",
        track.query,
        "--audio",
        "youtube-music",
        "youtube",
        "--format",
        args.format,
        "--bitrate",
        args.bitrate,
        "--output",
        output_template,
        "--overwrite",
        "skip",
        "--scan-for-songs",
        "--threads",
        "1",
        "--max-retries",
        str(args.spotdl_retries),
        "--max-filename-length",
        str(args.max_filename_length),
        "--id3-separator",
        "; ",
        "--print-errors",
        "--log-level",
        args.spotdl_log_level,
    ]

    if args.restrict:
        command.extend(["--restrict", args.restrict])

    if args.cookie_file:
        command.extend(["--cookie-file", str(args.cookie_file.resolve())])

    if args.preload:
        command.append("--preload")

    if args.client_id and args.client_secret:
        command.extend(["--client-id", args.client_id, "--client-secret", args.client_secret])
    elif args.auth_token:
        command.extend(["--auth-token", args.auth_token])

    return command


def is_spotify_retry_notice(text: str) -> bool:
    lowered = text.lower()
    return "retry will occur after:" in lowered


def is_spotify_rate_limit(text: str) -> bool:
    lowered = text.lower()
    return (
        "rate/request limit" in lowered
        or "too many requests" in lowered and "spotify" in lowered
        or "429" in lowered and "spotify" in lowered
    )


def is_spotify_auth_error(text: str) -> bool:
    lowered = text.lower()
    return (
        "invalid auth token" in lowered
        or "invalid access token" in lowered
        or "expired access token" in lowered
        or "access token expired" in lowered
        or "unauthorized" in lowered
        or "401" in lowered and "spotify" in lowered
        or "active premium subscription required" in lowered
        or "403" in lowered and "api.spotify.com" in lowered
    )


def run_spotdl(command: list[str], timeout: int, track: Track, live_output: bool) -> tuple[int, str]:
    process = subprocess.Popen(
        command,
        cwd=Path.cwd(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )

    lines: list[str] = []
    line_queue: queue.Queue[str] = queue.Queue()

    def reader() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            line_queue.put(line)

    reader_thread = threading.Thread(target=reader, daemon=True)
    reader_thread.start()

    started = time.time()
    rate_limited = False
    timed_out = False

    while True:
        try:
            line = line_queue.get(timeout=0.25)
        except queue.Empty:
            line = ""

        if line:
            clean_line = line.rstrip()
            lines.append(clean_line)
            if live_output and clean_line:
                safe_print(f"[spotdl #{track.list_position}] {clean_line}")
            if is_spotify_retry_notice(clean_line):
                safe_print(f"[wait] #{track.list_position}: Spotify asked spotDL to back off; letting it retry.")
            elif is_spotify_rate_limit(clean_line):
                rate_limited = True
                process.kill()
            if is_spotify_auth_error(clean_line):
                rate_limited = True
                process.kill()

        if process.poll() is not None:
            while True:
                try:
                    extra = line_queue.get_nowait().rstrip()
                except queue.Empty:
                    break
                lines.append(extra)
                if live_output and extra:
                    safe_print(f"[spotdl #{track.list_position}] {extra}")
            break

        if time.time() - started > timeout:
            timed_out = True
            process.kill()
            break

    try:
        code = process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        code = 124

    output = "\n".join(lines)
    if rate_limited:
        output = output + "\nStopped early because Spotify auth/rate limit blocked the run."
        return AUTH_RATE_LIMIT_EXIT_CODE, output
    if timed_out:
        output = output + f"\nTimed out after {timeout} seconds."
        return 124, output
    return code, output


def download_track(
    track: Track,
    spotdl_runner: list[str],
    output_dir: Path,
    error_dir: Path,
    history_path: Path,
    failed_csv_path: Path,
    history: dict[str, Any],
    args: argparse.Namespace,
    stop_event: threading.Event,
    auth_fallback_event: threading.Event,
) -> DownloadResult:
    if stop_event.is_set():
        return DownloadResult(ok=False, track=track, error="Skipped because another download hit a Spotify rate limit.", skipped=True)

    start = time.time()
    effective_args = args
    if auth_fallback_event.is_set() and (args.auth_token or args.client_id or args.client_secret):
        effective_args = argparse.Namespace(**vars(args))
        effective_args.auth_token = ""
        effective_args.auth_token_source = ""
        effective_args.client_id = ""
        effective_args.client_secret = ""

    command = build_spotdl_command(spotdl_runner, track, output_dir, error_dir, effective_args)
    using_auth_token = bool(effective_args.auth_token) and not (effective_args.client_id and effective_args.client_secret)
    using_client_credentials = bool(effective_args.client_id and effective_args.client_secret)
    safe_print(f"[start] #{track.list_position}: {track.label}")

    last_error = ""
    retried_without_custom_spotify_auth = False
    for attempt in range(1, args.retries + 1):
        try:
            code, output = run_spotdl(command, timeout=args.timeout, track=track, live_output=args.live_spotdl_output)
            if code == 0:
                mark_success(history_path, failed_csv_path, history, track)
                elapsed = time.time() - start
                return DownloadResult(ok=True, track=track, elapsed_seconds=elapsed)

            if (
                code == AUTH_RATE_LIMIT_EXIT_CODE
                and (using_auth_token or using_client_credentials)
                and is_spotify_auth_error(output)
                and not retried_without_custom_spotify_auth
            ):
                safe_print(
                    f"[auth] #{track.list_position}: custom Spotify auth failed; "
                    "retrying once with spotDL's cached/default client credentials."
                )
                auth_fallback_event.set()
                args_without_custom_auth = argparse.Namespace(**vars(args))
                args_without_custom_auth.auth_token = ""
                args_without_custom_auth.auth_token_source = ""
                args_without_custom_auth.client_id = ""
                args_without_custom_auth.client_secret = ""
                command = build_spotdl_command(spotdl_runner, track, output_dir, error_dir, args_without_custom_auth)
                using_auth_token = False
                using_client_credentials = False
                retried_without_custom_spotify_auth = True
                continue

            if code == AUTH_RATE_LIMIT_EXIT_CODE:
                stop_event.set()
                last_error = output
                write_error_log(error_dir, track, last_error)
                mark_failed(history_path, failed_csv_path, history, track, last_error)
                elapsed = time.time() - start
                return DownloadResult(ok=False, track=track, error=last_error, elapsed_seconds=elapsed, rate_limited=True)

            last_error = f"spotDL exited with code {code}\n{clean_error(output)}"
        except Exception as exc:
            last_error = repr(exc)

        if attempt < args.retries:
            safe_print(f"[retry] #{track.list_position}: attempt {attempt} failed, retrying: {track.label}")
            time.sleep(min(10, 2 * attempt))

    write_error_log(error_dir, track, last_error)
    mark_failed(history_path, failed_csv_path, history, track, last_error)
    elapsed = time.time() - start
    return DownloadResult(ok=False, track=track, error=last_error, elapsed_seconds=elapsed)


def filter_pending(tracks: list[Track], history: dict[str, Any], limit: int | None) -> list[Track]:
    successful = set(history.get("success", {}).keys())
    pending = [track for track in tracks if track.uri not in successful]
    if limit is not None:
        return pending[:limit]
    return pending


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Download a Spotify playlist CSV with spotDL, concurrency, and resumable history."
    )
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV_FILE, help="Playlist CSV file.")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="Flat MP3 output directory.")
    parser.add_argument("--history", type=Path, default=DEFAULT_HISTORY_FILE, help="JSON history file.")
    parser.add_argument("--failed-csv", type=Path, default=DEFAULT_FAILED_CSV, help="Readable CSV of failed tracks.")
    parser.add_argument("--error-dir", type=Path, default=DEFAULT_ERROR_DIR, help="Raw spotDL error logs.")
    parser.add_argument("--workers", type=positive_int, default=DEFAULT_WORKERS, help="Concurrent track downloads.")
    parser.add_argument("--retries", type=positive_int, default=DEFAULT_RETRIES, help="Wrapper retries per track.")
    parser.add_argument("--spotdl-retries", type=positive_int, default=3, help="spotDL metadata retry count.")
    parser.add_argument("--timeout", type=positive_int, default=DEFAULT_PER_TRACK_TIMEOUT, help="Timeout per track in seconds.")
    parser.add_argument("--format", default=DEFAULT_FORMAT, choices=("mp3", "flac", "ogg", "opus", "m4a", "wav"))
    parser.add_argument("--bitrate", default=DEFAULT_BITRATE, help="spotDL bitrate, e.g. 320k, 256k, auto, disable.")
    parser.add_argument("--restrict", choices=("strict", "ascii", "none"), default="strict", help="Filename sanitization.")
    parser.add_argument("--cookie-file", type=Path, default=Path("cookies.txt"), help="Optional YouTube Music cookies file.")
    parser.add_argument("--no-cookie-file", action="store_true", help="Do not pass cookies.txt to spotDL.")
    parser.add_argument("--preload", action="store_true", help="Ask spotDL to preload URLs.")
    parser.add_argument("--limit", type=positive_int, help="Download only the first N pending tracks.")
    parser.add_argument("--dry-run", action="store_true", help="Show what would be downloaded without downloading.")
    parser.add_argument("--max-filename-length", type=positive_int, default=180)
    parser.add_argument("--spotdl-log-level", default="INFO", choices=("CRITICAL", "FATAL", "ERROR", "WARN", "WARNING", "INFO", "MATCH", "DEBUG", "NOTSET"))
    parser.add_argument("--auth-token-env", default="SPOTIFY_AUTH_TOKEN", help="Env var name containing a fresh Spotify auth token.")
    parser.add_argument("--auth-token-file", type=Path, default=DEFAULT_AUTH_TOKEN_FILE, help="File containing a fresh Spotify auth token.")
    parser.add_argument("--no-auth-token-file", action="store_true", help="Do not read spotify_auth_token.txt.")
    parser.add_argument("--client-id-env", default="SPOTIFY_CLIENT_ID", help="Env var name containing your Spotify app client id.")
    parser.add_argument("--client-secret-env", default="SPOTIFY_CLIENT_SECRET", help="Env var name containing your Spotify app client secret.")
    parser.add_argument("--hide-spotdl-output", action="store_true", help="Hide live spotDL subprocess output.")
    return parser


def resolve_paths(args: argparse.Namespace) -> None:
    args.csv = args.csv.resolve()
    args.output_dir = args.output_dir.resolve()
    args.history = args.history.resolve()
    args.failed_csv = args.failed_csv.resolve()
    args.error_dir = args.error_dir.resolve()
    args.auth_token_file = None if args.no_auth_token_file else args.auth_token_file.resolve()

    if args.no_cookie_file:
        args.cookie_file = None
    elif args.cookie_file and args.cookie_file.exists():
        args.cookie_file = args.cookie_file.resolve()
    else:
        args.cookie_file = None

    saved_credentials = read_saved_credentials()

    args.auth_token = ""
    args.auth_token_source = ""
    if args.auth_token_env:
        args.auth_token = os.environ.get(args.auth_token_env, "").strip()
        if args.auth_token:
            args.auth_token_source = f"env var {args.auth_token_env}"

    if not args.auth_token:
        args.auth_token = saved_credentials.get("spotify_auth_token", "")
        if args.auth_token:
            args.auth_token_source = str(CREDENTIALS_PATH)

    if not args.auth_token:
        args.auth_token = read_auth_token_file(args.auth_token_file)
        if args.auth_token:
            args.auth_token_source = str(args.auth_token_file)

    args.client_id = os.environ.get(args.client_id_env, "").strip() if args.client_id_env else ""
    args.client_secret = os.environ.get(args.client_secret_env, "").strip() if args.client_secret_env else ""
    if not args.client_id:
        args.client_id = saved_credentials.get("spotify_client_id", "")
    if not args.client_secret:
        args.client_secret = saved_credentials.get("spotify_client_secret", "")

    if not (args.client_id and args.client_secret) and not args.auth_token:
        safe_print(
            f"[warn] No saved Spotify client credentials or auth token found. "
            "spotDL will use its built-in/default client credentials, which may rate limit sooner."
        )
    elif args.client_id and args.client_secret and args.auth_token:
        safe_print("[auth] Spotify client credentials found; ignoring the short-lived auth token for this run.")

    args.live_spotdl_output = not args.hide_spotdl_output


def print_summary(
    tracks: list[Track],
    pending: list[Track],
    history: dict[str, Any],
    args: argparse.Namespace,
) -> None:
    success_count = len(history.get("success", {}))
    failed_count = len(history.get("failed", {}))
    safe_print("")
    safe_print("=" * 72)
    safe_print("SPOTDL CSV DOWNLOADER")
    safe_print("=" * 72)
    safe_print(f"CSV tracks       : {len(tracks)}")
    safe_print(f"Already complete : {success_count}")
    safe_print(f"Failed to retry  : {failed_count}")
    safe_print(f"Pending this run : {len(pending)}")
    safe_print(f"Workers          : {args.workers}")
    safe_print(f"Output directory : {args.output_dir}")
    safe_print(f"History file     : {args.history}")
    safe_print(f"Failed CSV       : {args.failed_csv}")
    safe_print(f"Cookies          : {args.cookie_file if args.cookie_file else 'not used'}")
    if args.client_id and args.client_secret:
        spotify_auth = f"client credentials loaded from {args.client_id_env}/{args.client_secret_env}"
    elif args.auth_token:
        spotify_auth = f"token loaded from {args.auth_token_source}"
    else:
        spotify_auth = "not loaded"
    safe_print(f"Spotify auth     : {spotify_auth}")
    safe_print(f"spotDL runner    : {' '.join(args.spotdl_runner)}")
    safe_print("=" * 72)
    safe_print("")


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    resolve_paths(args)

    spotdl_runner = resolve_spotdl_runner()
    if not spotdl_runner:
        safe_print("spotDL was not found in any usable Python environment.")
        safe_print("Run with the same Python you use for this script: python -m pip install --upgrade spotdl")
        return 2
    args.spotdl_runner = spotdl_runner

    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.error_dir.mkdir(parents=True, exist_ok=True)

    tracks = parse_csv(args.csv)
    history = load_history(args.history)
    pending = filter_pending(tracks, history, args.limit)
    write_failed_csv(args.failed_csv, history)

    print_summary(tracks, pending, history, args)

    if args.dry_run:
        preview_count = min(20, len(pending))
        for track in pending[:preview_count]:
            safe_print(f"[dry-run] #{track.list_position}: {track.label} <{track.uri}>")
        if len(pending) > preview_count:
            safe_print(f"[dry-run] ... {len(pending) - preview_count} more pending tracks")
        return 0

    if not pending:
        safe_print("Nothing to download. History is already complete.")
        return 0

    started = time.time()
    ok_count = 0
    fail_count = 0
    skipped_count = 0
    rate_limited_count = 0
    stop_event = threading.Event()
    auth_fallback_event = threading.Event()

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = [
            executor.submit(
                download_track,
                track,
                spotdl_runner,
                args.output_dir,
                args.error_dir,
                args.history,
                args.failed_csv,
                history,
                args,
                stop_event,
                auth_fallback_event,
            )
            for track in pending
        ]

        completed_count = 0
        total = len(futures)
        for future in as_completed(futures):
            completed_count += 1
            result = future.result()
            if result.ok:
                ok_count += 1
                safe_print(
                    f"[ok] {completed_count}/{total} in {result.elapsed_seconds:.1f}s: {result.track.label}"
                )
            elif result.skipped:
                skipped_count += 1
                safe_print(f"[skip] {completed_count}/{total}: {result.track.label}")
            else:
                fail_count += 1
                safe_print(
                    f"[fail] {completed_count}/{total} in {result.elapsed_seconds:.1f}s: {result.track.label}"
                )
                safe_print(f"       {clean_error(result.error, max_chars=500)}")
                if result.rate_limited:
                    rate_limited_count += 1
                    safe_print("       Spotify auth/rate limit blocked spotDL. Add a fresh token and rerun.")

    elapsed = time.time() - started
    write_failed_csv(args.failed_csv, history)
    safe_print("")
    safe_print("=" * 72)
    safe_print("RUN COMPLETE")
    safe_print("=" * 72)
    safe_print(f"Downloaded this run : {ok_count}")
    safe_print(f"Failed this run     : {fail_count}")
    safe_print(f"Skipped this run    : {skipped_count}")
    safe_print(f"Elapsed             : {elapsed:.1f}s")
    safe_print(f"MP3 directory       : {args.output_dir}")
    safe_print(f"History             : {args.history}")
    safe_print("=" * 72)

    if rate_limited_count:
        return AUTH_RATE_LIMIT_EXIT_CODE
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
