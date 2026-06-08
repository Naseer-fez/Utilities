"""
Concurrent resumable YouTube video downloader powered by yt-dlp.

Install command:
    python -m pip install --upgrade yt-dlp

Optional:
    - Put one URL per line in youtube_urls.txt, then run this script.
    - Or pass URLs directly with --url.
    - Or pass a CSV/TXT file with --input.

Examples:
    python youtube_video_downloader.py --url "https://www.youtube.com/watch?v=VIDEO_ID"
    python youtube_video_downloader.py --input youtube_urls.txt --workers 4 --limit 25
    python youtube_video_downloader.py --input videos.csv --workers 4 --quality 720
"""

from __future__ import annotations

import argparse
import csv
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

DEFAULT_INPUT_FILE = BASE_DIR / "youtube_urls.txt"
DEFAULT_OUTPUT_DIR = BASE_DIR / "Downloaded_Videos"
DEFAULT_HISTORY_FILE = BASE_DIR / "youtube_download_history.json"
DEFAULT_FAILED_CSV = BASE_DIR / "youtube_failed.csv"
DEFAULT_ERROR_DIR = BASE_DIR / "youtube_errors"
DEFAULT_ARCHIVE_FILE = BASE_DIR / "youtube_archive.txt"

URL_HEADERS = ("url", "URL", "Video URL", "Youtube URL", "YouTube URL", "Link", "link")
TITLE_HEADERS = ("title", "Title", "Video Title", "Name", "name")

print_lock = threading.Lock()
history_lock = threading.RLock()


@dataclass(frozen=True)
class Video:
    url: str
    title: str
    list_position: int

    @property
    def label(self) -> str:
        return self.title or self.url


@dataclass(frozen=True)
class DownloadResult:
    ok: bool
    video: Video
    error: str = ""
    elapsed_seconds: float = 0.0


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


def short_hash(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8", errors="ignore")).hexdigest()[:10]


def first_present(row: dict[str, str], names: tuple[str, ...]) -> str:
    for name in names:
        value = row.get(name)
        if value is not None and str(value).strip():
            return str(value).strip()
    return ""


def looks_like_url(text: str) -> bool:
    lowered = text.strip().lower()
    return lowered.startswith(("http://", "https://", "www.", "youtube.com/", "youtu.be/"))


def normalize_url(text: str) -> str:
    text = text.strip()
    if text.startswith("www."):
        return "https://" + text
    if text.startswith(("youtube.com/", "youtu.be/")):
        return "https://" + text
    return text


def parse_txt(path: Path) -> list[Video]:
    videos: list[Video] = []
    seen: set[str] = set()

    with path.open("r", encoding="utf-8-sig") as handle:
        for index, line in enumerate(handle, start=1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            if "," in line:
                maybe_url, maybe_title = [part.strip() for part in line.split(",", 1)]
            else:
                maybe_url, maybe_title = line, ""

            url = normalize_url(maybe_url)
            if not looks_like_url(url):
                safe_print(f"[skip] Line {index}: not a URL: {line}")
                continue

            if url in seen:
                safe_print(f"[skip] Line {index}: duplicate URL already queued: {url}")
                continue

            seen.add(url)
            videos.append(Video(url=url, title=maybe_title, list_position=index))

    return videos


def parse_csv_file(path: Path) -> list[Video]:
    videos: list[Video] = []
    seen: set[str] = set()

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            raise ValueError(f"CSV has no header row: {path}")

        first_column = reader.fieldnames[0]
        for index, row in enumerate(reader, start=1):
            url = first_present(row, URL_HEADERS) or str(row.get(first_column, "")).strip()
            title = first_present(row, TITLE_HEADERS)
            url = normalize_url(url)

            if not url:
                safe_print(f"[skip] Row {index}: missing URL")
                continue
            if not looks_like_url(url):
                safe_print(f"[skip] Row {index}: not a URL: {url}")
                continue
            if url in seen:
                safe_print(f"[skip] Row {index}: duplicate URL already queued: {url}")
                continue

            seen.add(url)
            videos.append(Video(url=url, title=title, list_position=index))

    return videos


def parse_inputs(args: argparse.Namespace) -> list[Video]:
    videos: list[Video] = []
    seen: set[str] = set()

    for index, url in enumerate(args.url or [], start=1):
        normalized = normalize_url(url)
        if normalized and normalized not in seen:
            videos.append(Video(url=normalized, title="", list_position=index))
            seen.add(normalized)

    input_path = args.input
    if input_path is None and not videos and DEFAULT_INPUT_FILE.exists():
        input_path = DEFAULT_INPUT_FILE

    if input_path:
        input_path = input_path.resolve()
        if not input_path.exists():
            raise FileNotFoundError(f"Input file not found: {input_path}")
        parsed = parse_csv_file(input_path) if input_path.suffix.lower() == ".csv" else parse_txt(input_path)
        for video in parsed:
            if video.url not in seen:
                videos.append(video)
                seen.add(video.url)

    return videos


def empty_history() -> dict[str, Any]:
    return {
        "version": 1,
        "created_at": utc_now(),
        "updated_at": utc_now(),
        "success": {},
        "failed": {},
    }


def load_history(path: Path) -> dict[str, Any]:
    if not path.exists():
        return empty_history()

    try:
        with path.open("r", encoding="utf-8") as handle:
            raw = json.load(handle)
    except json.JSONDecodeError as exc:
        backup = path.with_suffix(f".corrupt.{int(time.time())}.json")
        shutil.copy2(path, backup)
        safe_print(f"[warn] History JSON was invalid. Backed it up to {backup.name}.")
        safe_print(f"[warn] JSON error: {exc}")
        return empty_history()

    history = empty_history()
    if isinstance(raw, dict):
        history["created_at"] = raw.get("created_at") or history["created_at"]
        history["updated_at"] = raw.get("updated_at") or utc_now()
        if isinstance(raw.get("success"), dict):
            history["success"] = raw["success"]
        if isinstance(raw.get("failed"), dict):
            history["failed"] = raw["failed"]
    return history


def atomic_write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with temp_path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    os.replace(temp_path, path)


def classify_error(error: str) -> str:
    lowered = error.lower()
    if "private video" in lowered:
        return "Private video"
    if "video unavailable" in lowered or "this video is unavailable" in lowered:
        return "Video unavailable"
    if "copyright" in lowered:
        return "Copyright restriction"
    if "sign in" in lowered or "confirm you're not a bot" in lowered:
        return "Needs cookies/login"
    if "requested format is not available" in lowered:
        return "Format unavailable"
    if "timed out" in lowered:
        return "Timed out"
    return "Other"


def write_failed_csv(path: Path, history: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    fields = [
        "list_position",
        "title",
        "url",
        "attempts",
        "reason",
        "updated_at",
        "last_error",
    ]

    with temp_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        failed = history.get("failed", {})
        for record in sorted(failed.values(), key=lambda item: (item.get("list_position") or 0, item.get("title") or "")):
            row = {field: record.get(field, "") for field in fields}
            row["last_error"] = clean_error(str(row["last_error"]), max_chars=2000).replace("\n", " ")
            writer.writerow(row)

    os.replace(temp_path, path)


def mark_success(history_path: Path, failed_csv_path: Path, history: dict[str, Any], video: Video) -> None:
    with history_lock:
        history["success"][video.url] = {
            "url": video.url,
            "title": video.title,
            "list_position": video.list_position,
            "completed_at": utc_now(),
        }
        history["failed"].pop(video.url, None)
        history["updated_at"] = utc_now()
        atomic_write_json(history_path, history)
        write_failed_csv(failed_csv_path, history)


def mark_failed(history_path: Path, failed_csv_path: Path, history: dict[str, Any], video: Video, error: str) -> None:
    with history_lock:
        previous = history["failed"].get(video.url, {})
        attempts = int(previous.get("attempts", 0) or 0) + 1
        history["failed"][video.url] = {
            "url": video.url,
            "title": video.title,
            "list_position": video.list_position,
            "attempts": attempts,
            "reason": classify_error(error),
            "updated_at": utc_now(),
            "last_error": clean_error(error, max_chars=2000),
        }
        history["updated_at"] = utc_now()
        atomic_write_json(history_path, history)
        write_failed_csv(failed_csv_path, history)


def _is_real_python(exe: Path) -> bool:
    """Return True only if exe looks like a Python interpreter, not a frozen GUI exe."""
    name = exe.name.lower()
    return name.startswith("python") and name.endswith(".exe")


def python_has_ytdlp(python_exe: Path) -> bool:
    try:
        completed = subprocess.run(
            [str(python_exe), "-c", "import yt_dlp"],
            capture_output=True,
            text=True,
            timeout=20,
        )
    except Exception:
        return False
    return completed.returncode == 0


def resolve_ytdlp_runner() -> list[str] | None:
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
        if python_has_ytdlp(resolved):
            return [str(resolved), "-m", "yt_dlp"]

    return None


def quality_format(quality: str) -> str:
    if quality == "best":
        return "bv*+ba/best"
    height = int(quality)
    return (
        f"bv*[height<={height}][ext=mp4]+ba[ext=m4a]/"
        f"bv*[height<={height}]+ba/"
        f"b[height<={height}][ext=mp4]/"
        f"best[height<={height}]/best"
    )


def build_command(runner: list[str], video: Video, args: argparse.Namespace) -> list[str]:
    error_file = args.error_dir / f"{video.list_position:04d}_{short_hash(video.url)}.txt"
    output_template = str((args.output_dir / "%(title).180B [%(id)s].%(ext)s").resolve())

    command = [
        *runner,
        video.url,
        "--format",
        quality_format(args.quality),
        "--merge-output-format",
        "mp4",
        "--remux-video",
        "mp4",
        "--output",
        output_template,
        "--paths",
        str(args.output_dir),
        "--download-archive",
        str(args.archive.resolve()),
        "--concurrent-fragments",
        str(args.fragments),
        "--retries",
        str(args.ytdlp_retries),
        "--fragment-retries",
        str(args.ytdlp_retries),
        "--socket-timeout",
        str(args.socket_timeout),
        "--embed-metadata",
        "--write-thumbnail",
        "--embed-thumbnail",
        "--convert-thumbnails",
        "jpg",
        "--restrict-filenames",
        "--newline",
        "--no-warnings",
        "--no-simulate",
        "--print",
        "after_move:filepath",
    ]

    if not args.allow_playlists:
        command.append("--no-playlist")

    if args.subs:
        command.extend(["--write-subs", "--write-auto-subs", "--sub-langs", args.sub_langs, "--embed-subs"])

    if args.cookie_file:
        command.extend(["--cookies", str(args.cookie_file.resolve())])

    if args.cookies_from_browser:
        command.extend(["--cookies-from-browser", args.cookies_from_browser])

    if args.rate_limit:
        command.extend(["--limit-rate", args.rate_limit])

    if args.ytdlp_args:
        command.extend(args.ytdlp_args)

    command.extend(["--no-continue" if args.no_continue else "--continue"])

    # Keep an empty file available for easier per-item troubleshooting.
    error_file.touch(exist_ok=True)
    return command


def run_ytdlp(command: list[str], timeout: int, video: Video, live_output: bool) -> tuple[int, str]:
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

    threading.Thread(target=reader, daemon=True).start()
    started = time.time()
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
                safe_print(f"[yt-dlp #{video.list_position}] {clean_line}")

        if process.poll() is not None:
            while True:
                try:
                    extra = line_queue.get_nowait().rstrip()
                except queue.Empty:
                    break
                lines.append(extra)
                if live_output and extra:
                    safe_print(f"[yt-dlp #{video.list_position}] {extra}")
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
    if timed_out:
        output = output + f"\nTimed out after {timeout} seconds."
        return 124, output
    return code, output


def download_video(
    video: Video,
    runner: list[str],
    history_path: Path,
    failed_csv_path: Path,
    history: dict[str, Any],
    args: argparse.Namespace,
) -> DownloadResult:
    start = time.time()
    command = build_command(runner, video, args)
    safe_print(f"[start] #{video.list_position}: {video.label}")

    last_error = ""
    for attempt in range(1, args.retries + 1):
        code, output = run_ytdlp(command, timeout=args.timeout, video=video, live_output=args.live_output)
        if code == 0:
            mark_success(history_path, failed_csv_path, history, video)
            return DownloadResult(ok=True, video=video, elapsed_seconds=time.time() - start)

        last_error = f"yt-dlp exited with code {code}\n{clean_error(output)}"
        if attempt < args.retries:
            safe_print(f"[retry] #{video.list_position}: attempt {attempt} failed, retrying")
            time.sleep(min(10, 2 * attempt))

    mark_failed(history_path, failed_csv_path, history, video, last_error)
    return DownloadResult(ok=False, video=video, error=last_error, elapsed_seconds=time.time() - start)


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Concurrent resumable YouTube video downloader.")
    parser.add_argument("--url", action="append", help="YouTube URL. Can be used multiple times.")
    parser.add_argument("--input", type=Path, help="TXT or CSV input file. TXT = one URL per line.")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--history", type=Path, default=DEFAULT_HISTORY_FILE)
    parser.add_argument("--failed-csv", type=Path, default=DEFAULT_FAILED_CSV)
    parser.add_argument("--error-dir", type=Path, default=DEFAULT_ERROR_DIR)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE_FILE)
    parser.add_argument("--workers", type=positive_int, default=3)
    parser.add_argument("--limit", type=positive_int, help="Download only the first N pending videos.")
    parser.add_argument("--quality", default="1080", choices=("360", "480", "720", "1080", "1440", "2160", "best"))
    parser.add_argument("--fragments", type=positive_int, default=4, help="Per-video fragment concurrency.")
    parser.add_argument("--retries", type=positive_int, default=2, help="Wrapper retries per video.")
    parser.add_argument("--ytdlp-retries", type=positive_int, default=10)
    parser.add_argument("--timeout", type=positive_int, default=60 * 60, help="Timeout per video in seconds.")
    parser.add_argument("--socket-timeout", type=positive_int, default=30)
    parser.add_argument("--cookie-file", type=Path, default=BASE_DIR / "cookies.txt")
    parser.add_argument("--no-cookie-file", action="store_true")
    parser.add_argument("--cookies-from-browser", help='Example: "chrome" or "firefox"')
    parser.add_argument("--allow-playlists", action="store_true", help="Allow playlist URLs to download full playlists.")
    parser.add_argument("--subs", action="store_true", help="Download and embed available subtitles.")
    parser.add_argument("--sub-langs", default="en.*")
    parser.add_argument("--rate-limit", help='Optional speed cap, e.g. "5M".')
    parser.add_argument("--no-continue", action="store_true", help="Restart partial downloads instead of resuming them.")
    parser.add_argument("--hide-output", action="store_true", help="Hide live yt-dlp output.")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--ytdlp-args", nargs=argparse.REMAINDER, help="Extra raw yt-dlp args after --ytdlp-args.")
    return parser


def resolve_paths(args: argparse.Namespace) -> None:
    args.output_dir = args.output_dir.resolve()
    args.history = args.history.resolve()
    args.failed_csv = args.failed_csv.resolve()
    args.error_dir = args.error_dir.resolve()
    args.archive = args.archive.resolve()

    if args.input:
        args.input = args.input.resolve()

    if args.no_cookie_file:
        args.cookie_file = None
    elif args.cookie_file and args.cookie_file.exists():
        args.cookie_file = args.cookie_file.resolve()
    else:
        args.cookie_file = None

    args.live_output = not args.hide_output


def filter_pending(videos: list[Video], history: dict[str, Any], limit: int | None) -> list[Video]:
    success = set(history.get("success", {}).keys())
    pending = [video for video in videos if video.url not in success]
    if limit:
        return pending[:limit]
    return pending


def print_summary(videos: list[Video], pending: list[Video], history: dict[str, Any], args: argparse.Namespace) -> None:
    safe_print("")
    safe_print("=" * 72)
    safe_print("YOUTUBE VIDEO DOWNLOADER")
    safe_print("=" * 72)
    safe_print(f"Input videos     : {len(videos)}")
    safe_print(f"Already complete : {len(history.get('success', {}))}")
    safe_print(f"Failed to retry  : {len(history.get('failed', {}))}")
    safe_print(f"Pending this run : {len(pending)}")
    safe_print(f"Workers          : {args.workers}")
    safe_print(f"Quality          : {args.quality}")
    safe_print(f"Output directory : {args.output_dir}")
    safe_print(f"History file     : {args.history}")
    safe_print(f"Failed CSV       : {args.failed_csv}")
    safe_print(f"Cookies          : {args.cookie_file if args.cookie_file else 'not used'}")
    safe_print(f"yt-dlp runner    : {' '.join(args.runner)}")
    safe_print("=" * 72)
    safe_print("")


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    resolve_paths(args)

    runner = resolve_ytdlp_runner()
    if not runner:
        safe_print("yt-dlp was not found in any usable Python environment.")
        safe_print("Run: python -m pip install --upgrade yt-dlp")
        return 2
    args.runner = runner

    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.error_dir.mkdir(parents=True, exist_ok=True)

    videos = parse_inputs(args)
    if not videos:
        safe_print("No YouTube URLs found.")
        safe_print("Create D:\\Music\\youtube_urls.txt with one URL per line, or run with --url URL.")
        return 2

    history = load_history(args.history)
    pending = filter_pending(videos, history, args.limit)
    write_failed_csv(args.failed_csv, history)
    print_summary(videos, pending, history, args)

    if args.dry_run:
        for video in pending[:20]:
            safe_print(f"[dry-run] #{video.list_position}: {video.label} <{video.url}>")
        if len(pending) > 20:
            safe_print(f"[dry-run] ... {len(pending) - 20} more pending videos")
        return 0

    if not pending:
        safe_print("Nothing to download. History is already complete.")
        return 0

    started = time.time()
    ok_count = 0
    fail_count = 0

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = [
            executor.submit(download_video, video, runner, args.history, args.failed_csv, history, args)
            for video in pending
        ]
        total = len(futures)
        completed_count = 0
        for future in as_completed(futures):
            completed_count += 1
            result = future.result()
            if result.ok:
                ok_count += 1
                safe_print(f"[ok] {completed_count}/{total} in {result.elapsed_seconds:.1f}s: {result.video.label}")
            else:
                fail_count += 1
                safe_print(f"[fail] {completed_count}/{total} in {result.elapsed_seconds:.1f}s: {result.video.label}")
                safe_print(f"       {clean_error(result.error, max_chars=500)}")

    write_failed_csv(args.failed_csv, history)
    elapsed = time.time() - started
    safe_print("")
    safe_print("=" * 72)
    safe_print("RUN COMPLETE")
    safe_print("=" * 72)
    safe_print(f"Downloaded this run : {ok_count}")
    safe_print(f"Failed this run     : {fail_count}")
    safe_print(f"Elapsed             : {elapsed:.1f}s")
    safe_print(f"Video directory     : {args.output_dir}")
    safe_print(f"History             : {args.history}")
    safe_print(f"Failed CSV          : {args.failed_csv}")
    safe_print("=" * 72)

    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
