"""Spotify Music tab widget."""
from __future__ import annotations
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QLabel, QLineEdit, QSpinBox, QComboBox, QCheckBox, QPushButton,
    QFileDialog,
)
from PySide6.QtCore import Qt
from .backend import BASE_DIR, DEFAULT_MUSIC_OUTPUT


class SpotifyTab(QWidget):
    def __init__(self, config: dict, parent=None):
        super().__init__(parent)
        self.config = config
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        # ── Source CSV ──
        src = QGroupBox("📂  Playlist CSV Source")
        sl = QHBoxLayout(src)
        sl.addWidget(QLabel("CSV File:"))
        self.csv_edit = QLineEdit(str(self.config.get("spotify_csv", BASE_DIR / "playlist.csv")))
        sl.addWidget(self.csv_edit, 1)
        browse_csv = QPushButton("Browse...")
        browse_csv.setProperty("cssClass", "secondary")
        browse_csv.clicked.connect(self._pick_csv)
        sl.addWidget(browse_csv)
        layout.addWidget(src)

        # ── Output Location ──
        out = QGroupBox("📁  Download Location")
        ol = QHBoxLayout(out)
        ol.addWidget(QLabel("Music Folder:"))
        self.output_edit = QLineEdit(str(self.config.get("spotify_output", DEFAULT_MUSIC_OUTPUT)))
        ol.addWidget(self.output_edit, 1)
        browse_out = QPushButton("Choose...")
        browse_out.setProperty("cssClass", "secondary")
        browse_out.clicked.connect(self._pick_output)
        ol.addWidget(browse_out)
        open_btn = QPushButton("Open")
        open_btn.setProperty("cssClass", "secondary")
        open_btn.clicked.connect(lambda: self._open_path(self.output_edit.text()))
        ol.addWidget(open_btn)
        layout.addWidget(out)

        # ── Batch Controls ──
        ctrl = QGroupBox("⚙️  Batch Controls")
        gl = QGridLayout(ctrl)
        gl.setHorizontalSpacing(16)
        gl.setVerticalSpacing(8)

        gl.addWidget(QLabel("Workers"), 0, 0)
        self.workers_spin = QSpinBox()
        self.workers_spin.setRange(1, 16)
        self.workers_spin.setValue(int(self.config.get("spotify_workers", 4)))
        gl.addWidget(self.workers_spin, 1, 0)

        gl.addWidget(QLabel("Limit"), 0, 1)
        self.limit_edit = QLineEdit(str(self.config.get("spotify_limit", "")))
        self.limit_edit.setPlaceholderText("All")
        gl.addWidget(self.limit_edit, 1, 1)

        gl.addWidget(QLabel("Format"), 0, 2)
        self.format_combo = QComboBox()
        self.format_combo.addItems(["mp3", "flac", "ogg", "opus", "m4a", "wav"])
        self.format_combo.setCurrentText(str(self.config.get("spotify_format", "mp3")))
        gl.addWidget(self.format_combo, 1, 2)

        gl.addWidget(QLabel("Bitrate"), 0, 3)
        self.bitrate_combo = QComboBox()
        self.bitrate_combo.addItems(["320k", "256k", "192k", "128k", "auto", "disable"])
        self.bitrate_combo.setCurrentText(str(self.config.get("spotify_bitrate", "320k")))
        self.bitrate_combo.setEditable(True)
        gl.addWidget(self.bitrate_combo, 1, 3)

        gl.addWidget(QLabel("Retries"), 0, 4)
        self.retries_spin = QSpinBox()
        self.retries_spin.setRange(1, 10)
        self.retries_spin.setValue(int(self.config.get("spotify_retries", 2)))
        gl.addWidget(self.retries_spin, 1, 4)

        gl.addWidget(QLabel("Timeout (min)"), 0, 5)
        self.timeout_spin = QSpinBox()
        self.timeout_spin.setRange(1, 180)
        self.timeout_spin.setValue(int(self.config.get("spotify_timeout", 20)))
        gl.addWidget(self.timeout_spin, 1, 5)

        self.use_cookies_cb = QCheckBox("Use cookies")
        self.use_cookies_cb.setChecked(bool(self.config.get("spotify_use_cookies", True)))
        gl.addWidget(self.use_cookies_cb, 1, 6)

        self.preload_cb = QCheckBox("Preload")
        self.preload_cb.setChecked(bool(self.config.get("spotify_preload", False)))
        gl.addWidget(self.preload_cb, 1, 7)

        # Cookie file row
        gl.addWidget(QLabel("Cookies file:"), 2, 0)
        self.cookie_edit = QLineEdit(str(self.config.get("spotify_cookie_file", BASE_DIR / "cookies.txt")))
        gl.addWidget(self.cookie_edit, 2, 1, 1, 6)
        browse_ck = QPushButton("Browse...")
        browse_ck.setProperty("cssClass", "secondary")
        browse_ck.clicked.connect(self._pick_cookie)
        gl.addWidget(browse_ck, 2, 7)

        layout.addWidget(ctrl)

        # ── Action Buttons ──
        actions = QHBoxLayout()
        actions.setSpacing(10)
        self.dry_run_btn = QPushButton("🔍  Dry Run")
        self.dry_run_btn.setProperty("cssClass", "accent")
        actions.addWidget(self.dry_run_btn)
        self.start_btn = QPushButton("▶  Start Spotify Batch")
        actions.addWidget(self.start_btn)
        self.stop_btn = QPushButton("⏹  Stop / Resume Later")
        self.stop_btn.setProperty("cssClass", "danger")
        actions.addWidget(self.stop_btn)
        actions.addStretch()
        layout.addLayout(actions)

        # ── History Stats ──
        hist = QGroupBox("📊  Resumable History")
        hl = QHBoxLayout(hist)
        self.history_label = QLabel("Loading...")
        self.history_label.setProperty("cssClass", "muted")
        hl.addWidget(self.history_label, 1)
        refresh_btn = QPushButton("Refresh")
        refresh_btn.setProperty("cssClass", "secondary")
        hl.addWidget(refresh_btn)
        # connect refresh later from main window
        self.refresh_btn = refresh_btn
        layout.addWidget(hist)

        layout.addStretch()

    def _pick_csv(self):
        f, _ = QFileDialog.getOpenFileName(self, "Spotify Playlist CSV", str(BASE_DIR), "CSV files (*.csv);;All files (*)")
        if f:
            self.csv_edit.setText(f)

    def _pick_output(self):
        d = QFileDialog.getExistingDirectory(self, "Choose music folder", str(BASE_DIR))
        if d:
            self.output_edit.setText(d)

    def _pick_cookie(self):
        f, _ = QFileDialog.getOpenFileName(self, "Cookies file", str(BASE_DIR), "Text files (*.txt);;All files (*)")
        if f:
            self.cookie_edit.setText(f)

    def _open_path(self, path_str: str):
        import os, webbrowser
        from pathlib import Path as P
        p = P(path_str)
        if p.exists():
            if os.name == "nt":
                os.startfile(p)
            else:
                webbrowser.open(p.as_uri())

    def get_settings(self) -> dict:
        return {
            "spotify_csv": self.csv_edit.text(),
            "spotify_output": self.output_edit.text(),
            "spotify_workers": self.workers_spin.value(),
            "spotify_limit": self.limit_edit.text().strip(),
            "spotify_format": self.format_combo.currentText(),
            "spotify_bitrate": self.bitrate_combo.currentText(),
            "spotify_retries": self.retries_spin.value(),
            "spotify_timeout": self.timeout_spin.value(),
            "spotify_preload": self.preload_cb.isChecked(),
            "spotify_use_cookies": self.use_cookies_cb.isChecked(),
            "spotify_cookie_file": self.cookie_edit.text(),
        }
