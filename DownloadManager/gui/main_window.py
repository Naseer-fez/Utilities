"""Main application window tying tabs and backend together."""
from __future__ import annotations
import os
import subprocess
import threading
import time
import queue
import re
import ctypes
from pathlib import Path

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QProgressBar, QMessageBox, QApplication
)
from PySide6.QtCore import Qt, QTimer, QPropertyAnimation, QEasingCurve
from PySide6.QtGui import QIcon

from .backend import (
    BASE_DIR, APP_NAME, MUSIC_SCRIPT, YOUTUBE_SCRIPT, GUI_YOUTUBE_INPUT,
    ProcessState, write_json, history_counts, load_config,
    load_credentials, save_credentials, CONFIG_PATH, CREDENTIALS_PATH,
    SPOTIFY_HISTORY_PATH, SPOTIFY_FAILED_CSV, SPOTIFY_ERROR_DIR,
    YOUTUBE_HISTORY_PATH, YOUTUBE_FAILED_CSV, YOUTUBE_ERROR_DIR, YOUTUBE_ARCHIVE_FILE,
    is_windows
)
from .spotify_tab import SpotifyTab
from .youtube_tab import YouTubeTab
from .settings_tab import SettingsTab
from .theme import DARK_STYLESHEET

class MainWindow(QMainWindow):
    def __init__(self, config: dict | None = None, credentials: dict | None = None):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.setMinimumSize(1120, 740)
        
        self.process_state: ProcessState | None = None
        self.output_queue: queue.Queue = queue.Queue()
        
        self.config = config if config is not None else load_config()
        self.credentials = credentials if credentials is not None else load_credentials()
        
        self._build_ui()
        self.refresh_history()
        
        # Poll queue periodically for subprocess output
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self._poll_queue)
        self.poll_timer.start(100)
        
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(16)
        
        # Tabs
        self.tabs = QTabWidget()
        self.spotify_tab = SpotifyTab(self.config)
        self.youtube_tab = YouTubeTab(self.config)
        self.settings_tab = SettingsTab(self.credentials)
        
        self.tabs.addTab(self.spotify_tab, "🎵  Spotify Music")
        self.tabs.addTab(self.youtube_tab, "🎬  YouTube Videos")
        self.tabs.addTab(self.settings_tab, "⚙️  Settings & Logs")
        main_layout.addWidget(self.tabs, 1)
        
        # Footer
        footer = QHBoxLayout()
        self.status_label = QLabel("Ready")
        self.status_label.setMinimumWidth(200)
        footer.addWidget(self.status_label)
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(True)
        self.progress_bar.setFormat("%v / %m")
        footer.addWidget(self.progress_bar, 1)
        
        self.elapsed_label = QLabel("Elapsed: 0s")
        self.elapsed_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.elapsed_label.setMinimumWidth(100)
        footer.addWidget(self.elapsed_label)
        
        main_layout.addLayout(footer)
        
        # Connect signals
        self.spotify_tab.dry_run_btn.clicked.connect(lambda: self.start_spotify(True))
        self.spotify_tab.start_btn.clicked.connect(lambda: self.start_spotify(False))
        self.spotify_tab.stop_btn.clicked.connect(self.stop_process)
        self.spotify_tab.refresh_btn.clicked.connect(self.refresh_history)
        
        self.youtube_tab.dry_run_btn.clicked.connect(lambda: self.start_youtube(True))
        self.youtube_tab.start_btn.clicked.connect(lambda: self.start_youtube(False))
        self.youtube_tab.stop_btn.clicked.connect(self.stop_process)
        
        self.settings_tab.save_creds_btn.clicked.connect(self.save_credentials)
        self.settings_tab.clear_creds_btn.clicked.connect(self.clear_credentials)
        self.settings_tab.save_all_btn.clicked.connect(self.save_config)
        self.settings_tab.setup_wizard_btn.clicked.connect(self.run_setup_wizard)

    def log(self, message: str):
        ts = time.strftime("%H:%M:%S")
        self.settings_tab.append_log(f"[{ts}] {message}")
        
    def refresh_history(self):
        m_ok, m_fail = history_counts(SPOTIFY_HISTORY_PATH)
        y_ok, y_fail = history_counts(YOUTUBE_HISTORY_PATH)
        self.spotify_tab.history_label.setText(
            f"Spotify completed: {m_ok}    Spotify failed: {m_fail}    "
            f"YouTube completed: {y_ok}    YouTube failed: {y_fail}"
        )
        
    def save_config(self):
        cfg = {}
        cfg.update(self.spotify_tab.get_settings())
        cfg.update(self.youtube_tab.get_settings())
        cfg["setup_wizard_completed"] = True
        write_json(CONFIG_PATH, cfg)
        self.config = cfg
        self.log("Saved manager settings to your user profile.")
        
    def save_credentials(self):
        token = self.settings_tab.auth_token_edit.text()
        cid = self.settings_tab.client_id_edit.text()
        csec = self.settings_tab.client_secret_edit.text()
        save_credentials(token, cid, csec)
        self.credentials = load_credentials()
        self.settings_tab.update_status(self.credentials)
        self.log("Saved encrypted credentials.")
        
    def clear_credentials(self):
        self.settings_tab.auth_token_edit.clear()
        self.settings_tab.client_id_edit.clear()
        self.settings_tab.client_secret_edit.clear()
        if CREDENTIALS_PATH.exists():
            try:
                CREDENTIALS_PATH.unlink()
            except OSError as e:
                QMessageBox.warning(self, APP_NAME, f"Could not delete credentials:\n{e}")
                return
        self.credentials = {}
        self.settings_tab.update_status(self.credentials)
        self.log("Cleared saved credentials.")
        
    def _ensure_output_dir(self, path_str: str) -> Path | None:
        if not path_str.strip():
            QMessageBox.warning(self, APP_NAME, "Please select an output folder.")
            return None
        p = Path(path_str).expanduser()
        try:
            p.mkdir(parents=True, exist_ok=True)
            return p
        except OSError as e:
            QMessageBox.critical(self, APP_NAME, f"Could not create folder:\n{p}\n{e}")
            return None

    def start_spotify(self, dry_run: bool):
        if self.process_state:
            QMessageBox.information(self, APP_NAME, "A batch is already running.")
            return
            
        settings = self.spotify_tab.get_settings()
        csv_path = Path(settings["spotify_csv"]).expanduser()
        if not csv_path.exists():
            QMessageBox.critical(self, APP_NAME, f"Spotify CSV not found:\n{csv_path}")
            return
            
        out_dir = self._ensure_output_dir(settings["spotify_output"])
        if not out_dir: return
        
        self.save_config()
        env = os.environ.copy()
        c = self.credentials
        if c.get("spotify_client_id"): env["SPOTIFY_CLIENT_ID"] = c["spotify_client_id"]
        if c.get("spotify_client_secret"): env["SPOTIFY_CLIENT_SECRET"] = c["spotify_client_secret"]
        if c.get("spotify_auth_token") and not (c.get("spotify_client_id") and c.get("spotify_client_secret")):
            env["SPOTIFY_AUTH_TOKEN"] = c["spotify_auth_token"]
            
        cmd = [
            sys.executable, str(MUSIC_SCRIPT),
            "--csv", str(csv_path),
            "--output-dir", str(out_dir),
            "--history", str(SPOTIFY_HISTORY_PATH),
            "--failed-csv", str(SPOTIFY_FAILED_CSV),
            "--error-dir", str(SPOTIFY_ERROR_DIR),
            "--workers", str(settings["spotify_workers"]),
            "--format", settings["spotify_format"],
            "--bitrate", settings["spotify_bitrate"],
            "--retries", str(settings["spotify_retries"]),
            "--timeout", str(max(1, settings["spotify_timeout"]) * 60)
        ]
        if settings["spotify_limit"]: cmd.extend(["--limit", settings["spotify_limit"]])
        if settings["spotify_preload"]: cmd.append("--preload")
        if settings["spotify_use_cookies"] and settings["spotify_cookie_file"]:
            cmd.extend(["--cookie-file", settings["spotify_cookie_file"]])
        else:
            cmd.append("--no-cookie-file")
        if dry_run: cmd.append("--dry-run")
        
        self.launch_process("spotify", cmd, env, dry_run)

    def start_youtube(self, dry_run: bool):
        if self.process_state:
            QMessageBox.information(self, APP_NAME, "A batch is already running.")
            return
            
        settings = self.youtube_tab.get_settings()
        out_dir = self._ensure_output_dir(settings["youtube_output"])
        if not out_dir: return
        
        # Prepare input
        urls = settings["youtube_urls"]
        input_file = settings["youtube_input_file"]
        input_path = None
        if urls:
            lines = [l.strip() for l in urls.splitlines() if l.strip()]
            GUI_YOUTUBE_INPUT.parent.mkdir(parents=True, exist_ok=True)
            with GUI_YOUTUBE_INPUT.open("w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
            input_path = GUI_YOUTUBE_INPUT
        elif input_file:
            p = Path(input_file).expanduser()
            if p.exists(): input_path = p
            
        if not input_path:
            QMessageBox.critical(self, APP_NAME, "Paste YouTube URLs or choose an input file.")
            return
            
        self.save_config()
        cmd = [
            sys.executable, str(YOUTUBE_SCRIPT),
            "--input", str(input_path),
            "--output-dir", str(out_dir),
            "--history", str(YOUTUBE_HISTORY_PATH),
            "--failed-csv", str(YOUTUBE_FAILED_CSV),
            "--error-dir", str(YOUTUBE_ERROR_DIR),
            "--archive", str(YOUTUBE_ARCHIVE_FILE),
            "--workers", str(settings["youtube_workers"]),
            "--quality", settings["youtube_quality"],
            "--fragments", str(settings["youtube_fragments"]),
            "--retries", str(settings["youtube_retries"]),
            "--timeout", str(max(1, settings["youtube_timeout"]) * 60)
        ]
        if settings["youtube_limit"]: cmd.extend(["--limit", settings["youtube_limit"]])
        if settings["youtube_allow_playlists"]: cmd.append("--allow-playlists")
        if settings["youtube_subs"]: cmd.append("--subs")
        if settings["youtube_use_cookies"] and settings["youtube_cookie_file"]:
            cmd.extend(["--cookie-file", settings["youtube_cookie_file"]])
        else:
            cmd.append("--no-cookie-file")
        if settings["youtube_browser_cookies"]:
            cmd.extend(["--cookies-from-browser", settings["youtube_browser_cookies"]])
        if settings["youtube_rate_limit"]:
            cmd.extend(["--rate-limit", settings["youtube_rate_limit"]])
        if dry_run: cmd.append("--dry-run")
        
        self.launch_process("youtube", cmd, os.environ.copy(), dry_run)

    def launch_process(self, mode: str, cmd: list[str], env: dict[str, str], dry_run: bool):
        if not MUSIC_SCRIPT.exists() or not YOUTUBE_SCRIPT.exists():
            QMessageBox.critical(self, APP_NAME, "Downloader scripts missing next to GUI.")
            return
            
        self.progress_bar.setRange(0, 0) # Indeterminate
        self.status_label.setText(f"{mode.title()} {'dry run' if dry_run else 'batch'} running")
        self.elapsed_label.setText("Elapsed: 0s")
        self.log("")
        self.log(f"Starting {mode} {'dry run' if dry_run else 'batch'}")
        
        # Redact secrets
        redacted = []
        skip = False
        for p in cmd:
            if skip:
                redacted.append("****")
                skip = False
                continue
            redacted.append(p)
            if p in ("--auth-token", "--client-id", "--client-secret"):
                skip = True
        self.log("Command: " + " ".join(redacted))
        
        try:
            proc = subprocess.Popen(
                cmd, cwd=str(BASE_DIR), env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
                creationflags=subprocess.CREATE_NO_WINDOW if is_windows() else 0
            )
        except OSError as e:
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(0)
            self.status_label.setText("Failed to start")
            QMessageBox.critical(self, APP_NAME, f"Could not start downloader:\n{e}")
            return
            
        self.process_state = ProcessState(mode=mode, process=proc, started_at=time.time())
        threading.Thread(target=self._read_output, args=(proc,), daemon=True).start()
        
    def _read_output(self, proc: subprocess.Popen):
        for line in proc.stdout:
            self.output_queue.put(("line", line.rstrip()))
        code = proc.wait()
        self.output_queue.put(("done", code))
        
    def _poll_queue(self):
        while not self.output_queue.empty():
            kind, payload = self.output_queue.get()
            if kind == "line":
                self._handle_line(payload)
            elif kind == "done":
                self._handle_done(payload)
                
        if self.process_state:
            el = int(time.time() - self.process_state.started_at)
            self.elapsed_label.setText(f"Elapsed: {el}s")

    def _animate_progress(self, target_val: int):
        self.anim = QPropertyAnimation(self.progress_bar, b"value")
        self.anim.setDuration(250)
        self.anim.setStartValue(self.progress_bar.value())
        self.anim.setEndValue(target_val)
        self.anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.anim.start()

    def _handle_line(self, line: str):
        if not line: return
        self.log(line)
        st = self.process_state
        if not st: return
        
        tm = re.search(r"Pending this run\s*:\s*(\d+)", line)
        if tm:
            st.total = int(tm.group(1))
            self.progress_bar.setRange(0, max(1, st.total))
            self.progress_bar.setValue(0)
            return
            
        cm = re.search(r"\[(ok|fail|skip)\]\s+(\d+)/(\d+)", line)
        if cm:
            status, comp, tot = cm.groups()
            st.completed = int(comp)
            st.total = int(tot)
            if status == "ok": st.ok += 1
            elif status == "fail": st.failed += 1
            elif status == "skip": st.skipped += 1
            
            self.progress_bar.setRange(0, max(1, st.total))
            self._animate_progress(st.completed)
            self.status_label.setText(f"{st.mode.title()} running: {st.ok} ok, {st.failed} failed, {st.skipped} skipped")
            return
            
        dm = re.search(r"\[dry-run\]\s+#", line)
        if dm and st.total:
            st.completed = min(st.total, st.completed + 1)
            self.progress_bar.setRange(0, max(1, st.total))
            self._animate_progress(st.completed)
            
    def _handle_done(self, code: int):
        st = self.process_state
        if st:
            if st.total:
                self.progress_bar.setRange(0, st.total)
                self.progress_bar.setValue(st.completed)
            else:
                self.progress_bar.setRange(0, 100)
                self.progress_bar.setValue(0)
            
            if code == 0: self.status_label.setText(f"{st.mode.title()} batch complete")
            else: self.status_label.setText(f"{st.mode.title()} batch ended with code {code}")
            self.log(f"{st.mode.title()} process exited with code {code}.")
            
        self.process_state = None
        self.refresh_history()
        
    def stop_process(self):
        if not self.process_state:
            self.status_label.setText("No batch is running")
            return
        ans = QMessageBox.question(self, APP_NAME, "Stop the current batch?\nCompleted downloads stay in history.")
        if ans == QMessageBox.StandardButton.Yes:
            self.log("Stopping process...")
            self.status_label.setText("Stopping...")
            self.process_state.process.terminate()

    def run_setup_wizard(self):
        if self.process_state:
            QMessageBox.information(self, APP_NAME, "Please wait for the current batch to finish before changing setup.")
            return

        from .setup_wizard import SetupWizard

        wizard = SetupWizard(self.config, self.credentials, self)
        if wizard.exec() != wizard.DialogCode.Accepted:
            return

        self.config = load_config()
        self.credentials = load_credentials()
        self._build_ui()
        self.refresh_history()
        self.log("Setup wizard saved your settings.")
            
    def closeEvent(self, event):
        if self.process_state:
            ans = QMessageBox.question(self, APP_NAME, "A batch is running. Stop it and close?")
            if ans != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
            self.process_state.process.terminate()
        self.save_config()
        event.accept()

def run_app():
    if os.name == "nt":
        # Separate this app from python.exe in the Windows taskbar
        myappid = 'fez.naseer.downloadmanager.gui.1.0'
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
        
    app = QApplication([])
    app.setStyleSheet(DARK_STYLESHEET)
    
    # Try to load a custom icon if it exists in the root folder
    icon_path = BASE_DIR / "icon.png"
    if not icon_path.exists():
        icon_path = BASE_DIR / "icon.ico"
    if icon_path.exists():
        app.setWindowIcon(QIcon(str(icon_path)))
        
    config = load_config()
    credentials = load_credentials()
    if not config.get("setup_wizard_completed"):
        from .setup_wizard import SetupWizard

        wizard = SetupWizard(config, credentials)
        if wizard.exec() == wizard.DialogCode.Accepted:
            config = load_config()
            credentials = load_credentials()

    win = MainWindow(config=config, credentials=credentials)
    win.show()
    return app.exec()
