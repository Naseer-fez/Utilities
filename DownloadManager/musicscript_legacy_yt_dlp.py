import os
import re
import csv
import json
import time
import requests
from io import BytesIO
from threading import Lock
from concurrent.futures import ThreadPoolExecutor, as_completed

import yt_dlp
from PIL import Image
from tqdm import tqdm

from mutagen.mp3 import MP3
from mutagen.id3 import (
    ID3,
    APIC,
    TIT2,
    TPE1,
    TALB,
    TRCK
)

# =========================================================
# CONFIG
# =========================================================
CSV_FILE = "playlist.csv"

OUTPUT_DIR = "downloads"

HISTORY_FILE = "download_history.json"

FAILED_LOG = "failed.txt"

MAX_WORKERS = 1

AUDIO_QUALITY = "320"

# =========================================================
# GLOBALS
# =========================================================
lock = Lock()

os.makedirs(OUTPUT_DIR, exist_ok=True)

if os.path.exists(HISTORY_FILE):

    with open(HISTORY_FILE, "r", encoding="utf-8") as f:
        history = json.load(f)

else:

    history = {
        "success": [],
        "failed": []
    }

# =========================================================
# UTIL
# =========================================================
def sanitize_filename(name):

    return re.sub(r'[\\/*?:"<>|]', "", name)


def save_history():

    with lock:

        with open(HISTORY_FILE, "w", encoding="utf-8") as f:
            json.dump(history, f, indent=2)


def log_failed(track, reason):

    with lock:

        with open(FAILED_LOG, "a", encoding="utf-8") as f:

            f.write("\n" + "=" * 80 + "\n")

            f.write(f"TRACK : {track}\n")

            f.write(f"ERROR : {reason}\n")


# =========================================================
# EMBED METADATA
# =========================================================
def embed_metadata(
    mp3_path,
    title,
    artist,
    album,
    thumbnail_url,
    track_number
):

    audio = MP3(mp3_path, ID3=ID3)

    try:
        audio.add_tags()
    except:
        pass

    audio.tags.add(TIT2(encoding=3, text=title))

    audio.tags.add(TPE1(encoding=3, text=artist))

    audio.tags.add(TALB(encoding=3, text=album))

    audio.tags.add(TRCK(encoding=3, text=str(track_number)))

    # THUMBNAIL
    if thumbnail_url:

        try:

            response = requests.get(
                thumbnail_url,
                timeout=20
            )

            img = Image.open(BytesIO(response.content))

            temp_cover = "temp_cover.jpg"

            img.convert("RGB").save(
                temp_cover,
                "JPEG"
            )

            with open(temp_cover, "rb") as albumart:

                audio.tags.add(
                    APIC(
                        encoding=3,
                        mime="image/jpeg",
                        type=3,
                        desc="Cover",
                        data=albumart.read()
                    )
                )

            os.remove(temp_cover)

        except Exception as e:

            print(f"\n⚠ Thumbnail embed failed: {e}")

    audio.save()


# =========================================================
# LOAD CSV
# =========================================================
songs = []

with open(CSV_FILE, "r", encoding="utf-8-sig") as f:

    reader = csv.DictReader(f)

    for row in reader:

        uri = row.get("Track URI", "").strip()

        title = row.get("Track Name", "").strip()

        artist = row.get("Artist Name(s)", "").strip()

        album = row.get("Album Name", "").strip()

        if not title:
            continue

        if uri in history["success"]:
            continue

        songs.append({
            "uri": uri,
            "title": title,
            "artist": artist,
            "album": album
        })

# =========================================================
# DOWNLOAD
# =========================================================
def download_song(song, index):

    title = song["title"]

    artist = song["artist"]

    album = song["album"]

    query = f"{artist} - {title} official audio"

    safe_name = sanitize_filename(
        f"{artist} - {title}"
    )

    output_template = os.path.join(
        OUTPUT_DIR,
        safe_name + ".%(ext)s"
    )

    ydl_opts = {

        # OUTPUT
        "outtmpl": output_template,

        # AUDIO
        "format": "bestaudio[ext=m4a]/bestaudio/best",

        # NO PLAYLIST
        "noplaylist": True,

        # FASTER
        "extract_flat": "discard_in_playlist",

        # STABILITY
        "retries": 10,
        "fragment_retries": 10,
        "socket_timeout": 30,

        # AVOID CACHE ISSUES
        "cachedir": False,

        # LESS RATE LIMITING
        "sleep_interval_requests": 1,

        # CLEANER OUTPUT
        "quiet": True,
        "no_warnings": True,

        # YOUTUBE FIX
        "extractor_args": {
            "youtube": {
                "player_client": ["android"]
            }
        },

        # NODEJS JS ENGINE
        "js_runtimes": {
            "node": "node"
        },

        # MP3 CONVERSION
        "postprocessors": [
            {
                "key": "FFmpegExtractAudio",
                "preferredcodec": "mp3",
                "preferredquality": AUDIO_QUALITY
            }
        ]
    }

    try:

        with yt_dlp.YoutubeDL(ydl_opts) as ydl:

            # STEP 1 SEARCH ONLY
            search_result = ydl.extract_info(
                f"ytsearch1:{query}",
                download=False
            )

            if not search_result:
                raise Exception("Search failed")

            if "entries" not in search_result:
                raise Exception("No entries found")

            entries = search_result["entries"]

            if not entries:
                raise Exception("Empty search results")

            video = entries[0]

            video_url = video["webpage_url"]

            thumbnail = video.get("thumbnail")

            # STEP 2 DOWNLOAD DIRECT VIDEO
            ydl.download([video_url])

        mp3_path = os.path.join(
            OUTPUT_DIR,
            safe_name + ".mp3"
        )

        if not os.path.exists(mp3_path):
            raise Exception("MP3 file missing after download")

        # EMBED TAGS
        embed_metadata(
            mp3_path=mp3_path,
            title=title,
            artist=artist,
            album=album,
            thumbnail_url=thumbnail,
            track_number=index + 1
        )

        with lock:

            history["success"].append(song["uri"])

            save_history()

        return True

    except Exception as e:

        with lock:

            history["failed"].append({
                "track": title,
                "error": str(e)
            })

            save_history()

        log_failed(
            title,
            str(e)
        )

        return False


# =========================================================
# MAIN
# =========================================================
print("\n" + "=" * 70)

print("🎵 ADVANCED SPOTIFY DOWNLOADER")

print("=" * 70)

print(f"📦 Pending Songs : {len(songs)}")

print(f"⚙️ Workers       : {MAX_WORKERS}")

print("=" * 70 + "\n")

start_time = time.time()

with tqdm(
    total=len(songs),
    desc="Downloading",
    dynamic_ncols=True
) as pbar:

    with ThreadPoolExecutor(
        max_workers=MAX_WORKERS
    ) as executor:

        futures = []

        for idx, song in enumerate(songs):

            futures.append(
                executor.submit(
                    download_song,
                    song,
                    idx
                )
            )

        for future in as_completed(futures):

            future.result()

            pbar.update(1)

elapsed = round(
    time.time() - start_time,
    2
)

print("\n" + "=" * 70)

print("✅ DOWNLOAD COMPLETE")

print(f"⏱ Time Taken : {elapsed} sec")

print("=" * 70 + "\n")