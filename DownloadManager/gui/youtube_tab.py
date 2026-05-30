"""YouTube Videos tab widget."""
from __future__ import annotations
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QLabel, QLineEdit, QSpinBox, QComboBox, QCheckBox, QPushButton,
    QPlainTextEdit, QFileDialog,
)
from PySide6.QtCore import Qt
from .backend import BASE_DIR, DEFAULT_VIDEO_OUTPUT


class YouTubeTab(QWidget):
    def __init__(self, config: dict, parent=None):
        super().__init__(parent)
        self.config = config
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        # ── Video Sources ──
        src = QGroupBox("🔗  Video Sources")
        sl = QVBoxLayout(src)

        file_row = QHBoxLayout()
        file_row.addWidget(QLabel("CSV/TXT file:"))
        self.input_edit = QLineEdit(str(self.config.get("youtube_input_file", "")))
        file_row.addWidget(self.input_edit, 1)
        browse_btn = QPushButton("Browse...")
        browse_btn.setProperty("cssClass", "secondary")
        browse_btn.clicked.connect(self._pick_input)
        file_row.addWidget(browse_btn)
        sl.addLayout(file_row)

        sl.addWidget(QLabel("Or paste one YouTube URL per line:"))
        self.urls_text = QPlainTextEdit()
        self.urls_text.setMaximumHeight(140)
        self.urls_text.setPlaceholderText("https://www.youtube.com/watch?v=...\nhttps://youtu.be/...")
        saved_urls = str(self.config.get("youtube_urls", ""))
        if saved_urls:
            self.urls_text.setPlainText(saved_urls)
        sl.addWidget(self.urls_text)
        layout.addWidget(src)

        # ── Output Location ──
        out = QGroupBox("📁  Download Location")
        ol = QHBoxLayout(out)
        ol.addWidget(QLabel("Video Folder:"))
        self.output_edit = QLineEdit(str(self.config.get("youtube_output", DEFAULT_VIDEO_OUTPUT)))
        ol.addWidget(self.output_edit, 1)
        choose_btn = QPushButton("Choose...")
        choose_btn.setProperty("cssClass", "secondary")
        choose_btn.clicked.connect(self._pick_output)
        ol.addWidget(choose_btn)
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
        self.workers_spin.setValue(int(self.config.get("youtube_workers", 3)))
        gl.addWidget(self.workers_spin, 1, 0)

        gl.addWidget(QLabel("Limit"), 0, 1)
        self.limit_edit = QLineEdit(str(self.config.get("youtube_limit", "")))
        self.limit_edit.setPlaceholderText("All")
        gl.addWidget(self.limit_edit, 1, 1)

        gl.addWidget(QLabel("Quality"), 0, 2)
        self.quality_combo = QComboBox()
        self.quality_combo.addItems(["360", "480", "720", "1080", "1440", "2160", "best"])
        self.quality_combo.setCurrentText(str(self.config.get("youtube_quality", "1080")))
        gl.addWidget(self.quality_combo, 1, 2)

        gl.addWidget(QLabel("Fragments"), 0, 3)
        self.fragments_spin = QSpinBox()
        self.fragments_spin.setRange(1, 16)
        self.fragments_spin.setValue(int(self.config.get("youtube_fragments", 4)))
        gl.addWidget(self.fragments_spin, 1, 3)

        gl.addWidget(QLabel("Retries"), 0, 4)
        self.retries_spin = QSpinBox()
        self.retries_spin.setRange(1, 10)
        self.retries_spin.setValue(int(self.config.get("youtube_retries", 2)))
        gl.addWidget(self.retries_spin, 1, 4)

        gl.addWidget(QLabel("Timeout (min)"), 0, 5)
        self.timeout_spin = QSpinBox()
        self.timeout_spin.setRange(1, 240)
        self.timeout_spin.setValue(int(self.config.get("youtube_timeout", 60)))
        gl.addWidget(self.timeout_spin, 1, 5)

        gl.addWidget(QLabel("Browser cookies"), 0, 6)
        self.browser_combo = QComboBox()
        self.browser_combo.addItems(["", "chrome", "edge", "firefox", "brave", "opera"])
        self.browser_combo.setCurrentText(str(self.config.get("youtube_browser_cookies", "")))
        self.browser_combo.setEditable(True)
        gl.addWidget(self.browser_combo, 1, 6)

        gl.addWidget(QLabel("Speed cap"), 0, 7)
        self.rate_edit = QLineEdit(str(self.config.get("youtube_rate_limit", "")))
        self.rate_edit.setPlaceholderText("e.g. 5M")
        gl.addWidget(self.rate_edit, 1, 7)

        # Checkboxes and cookies file row
        self.playlists_cb = QCheckBox("Playlists")
        self.playlists_cb.setChecked(bool(self.config.get("youtube_allow_playlists", False)))
        gl.addWidget(self.playlists_cb, 2, 0)

        self.subs_cb = QCheckBox("Subtitles")
        self.subs_cb.setChecked(bool(self.config.get("youtube_subs", False)))
        gl.addWidget(self.subs_cb, 2, 1)

        self.use_cookies_cb = QCheckBox("Use cookies file")
        self.use_cookies_cb.setChecked(bool(self.config.get("youtube_use_cookies", True)))
        gl.addWidget(self.use_cookies_cb, 2, 2)

        self.cookie_edit = QLineEdit(str(self.config.get("youtube_cookie_file", BASE_DIR / "cookies.txt")))
        gl.addWidget(self.cookie_edit, 2, 3, 1, 4)

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
        self.start_btn = QPushButton("▶  Start YouTube Batch")
        actions.addWidget(self.start_btn)
        self.stop_btn = QPushButton("⏹  Stop / Resume Later")
        self.stop_btn.setProperty("cssClass", "danger")
        actions.addWidget(self.stop_btn)
        actions.addStretch()
        layout.addLayout(actions)

        layout.addStretch()

    def _pick_input(self):
        f, _ = QFileDialog.getOpenFileName(self, "YouTube input", str(BASE_DIR), "CSV/TXT (*.csv *.txt);;All (*)")
        if f:
            self.input_edit.setText(f)

    def _pick_output(self):
        d = QFileDialog.getExistingDirectory(self, "Choose video folder", str(BASE_DIR))
        if d:
            self.output_edit.setText(d)

    def _pick_cookie(self):
        f, _ = QFileDialog.getOpenFileName(self, "Cookies file", str(BASE_DIR), "Text (*.txt);;All (*)")
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
            "youtube_input_file": self.input_edit.text(),
            "youtube_urls": self.urls_text.toPlainText().strip(),
            "youtube_output": self.output_edit.text(),
            "youtube_workers": self.workers_spin.value(),
            "youtube_limit": self.limit_edit.text().strip(),
            "youtube_quality": self.quality_combo.currentText(),
            "youtube_fragments": self.fragments_spin.value(),
            "youtube_retries": self.retries_spin.value(),
            "youtube_timeout": self.timeout_spin.value(),
            "youtube_allow_playlists": self.playlists_cb.isChecked(),
            "youtube_subs": self.subs_cb.isChecked(),
            "youtube_use_cookies": self.use_cookies_cb.isChecked(),
            "youtube_cookie_file": self.cookie_edit.text(),
            "youtube_browser_cookies": self.browser_combo.currentText().strip(),
            "youtube_rate_limit": self.rate_edit.text().strip(),
        }
