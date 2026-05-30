from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
HISTORY_FILE = BASE_DIR / "download_history.json"

BATCH_SIZE = "40"
WORKERS = "2"
SLEEP_SECONDS = 120


def success_count() -> int:
    if not HISTORY_FILE.exists():
        return 0
    try:
        data = json.loads(HISTORY_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return 0
    success = data.get("success", {})
    return len(success) if isinstance(success, dict) else 0


def run_batch() -> int:
    command = [
        sys.executable,
        str(BASE_DIR / "musicscript.py"),
        "--workers",
        WORKERS,
        "--limit",
        BATCH_SIZE,
    ]
    cookie_file = BASE_DIR / "cookies.txt"
    if cookie_file.exists():
        command.extend(["--cookie-file", str(cookie_file)])
    else:
        command.append("--no-cookie-file")
    return subprocess.run(command, cwd=BASE_DIR).returncode


def main() -> int:
    round_number = 1

    while True:
        before = success_count()
        print(f"\n[round {round_number}] starting batch. downloaded so far: {before}", flush=True)

        code = run_batch()
        after = success_count()
        gained = after - before

        print(f"[round {round_number}] finished with code {code}. new downloads: {gained}", flush=True)

        if code == 88:
            print(
                "Stopped: Spotify auth/rate limit. Wait a bit, or save Spotify client ID/secret in the GUI and run again.",
                flush=True,
            )
            return 88

        if code == 0 and gained == 0:
            print("Done: no pending songs left to download.", flush=True)
            return 0

        if gained == 0:
            print("Stopped: no progress this round. Check failed.csv for songs spotDL could not download.", flush=True)
            return code or 1

        round_number += 1
        print(f"Waiting {SLEEP_SECONDS} seconds before next batch...", flush=True)
        time.sleep(SLEEP_SECONDS)


if __name__ == "__main__":
    raise SystemExit(main())
