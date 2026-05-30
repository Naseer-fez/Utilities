"""
Desktop download manager for the existing Spotify CSV and YouTube downloader scripts.

This GUI is a wrapper around:
    - musicscript.py
    - youtube_video_downloader.py

Use it only for media you own, created yourself, or are otherwise allowed to download.
"""

import sys
import os

def handle_subprocess_commands():
    if getattr(sys, 'frozen', False):
        if len(sys.argv) >= 2 and sys.argv[1].lower().endswith(".py"):
            import runpy
            script_path = sys.argv[1]
            sys.argv = [script_path] + sys.argv[2:]
            runpy.run_path(script_path, run_name="__main__")
            sys.exit(0)
        if len(sys.argv) >= 3 and sys.argv[1] == "-c":
            # Intercept python -c commands
            code = sys.argv[2]
            exec(code)
            sys.exit(0)
        elif len(sys.argv) >= 3 and sys.argv[1] == "-m":
            module_name = sys.argv[2]
            sys.argv = [sys.argv[0]] + sys.argv[3:]
            if module_name == "yt_dlp":
                import yt_dlp
                yt_dlp.main()
            elif module_name == "spotdl":
                import spotdl
                spotdl.console_entry_point()
            else:
                import runpy
                runpy.run_module(module_name, run_name="__main__")
            sys.exit(0)

def main() -> int:
    handle_subprocess_commands()
    try:
        from gui.main_window import run_app
        return run_app()
    except ImportError as e:
        print(f"Error: Could not load GUI. Please ensure PySide6 is installed. Details: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
