"""Settings & Logs tab widget."""
from __future__ import annotations
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QLabel, QLineEdit, QPushButton, QTextEdit,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QTextCharFormat, QColor, QFont
from .backend import USER_DATA_DIR, SPOTIFY_FAILED_CSV, YOUTUBE_FAILED_CSV, mask_secret, load_credentials


class SettingsTab(QWidget):
    def __init__(self, credentials: dict, parent=None):
        super().__init__(parent)
        self.credentials = credentials
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        # ── Spotify Credentials ──
        creds = QGroupBox("🔑  Spotify Credentials")
        cl = QGridLayout(creds)
        cl.setHorizontalSpacing(12)
        cl.setVerticalSpacing(8)

        cl.addWidget(QLabel("Client ID:"), 0, 0)
        self.client_id_edit = QLineEdit(self.credentials.get("spotify_client_id", ""))
        self.client_id_edit.setEchoMode(QLineEdit.EchoMode.Password)
        cl.addWidget(self.client_id_edit, 0, 1)

        cl.addWidget(QLabel("Client Secret:"), 1, 0)
        self.client_secret_edit = QLineEdit(self.credentials.get("spotify_client_secret", ""))
        self.client_secret_edit.setEchoMode(QLineEdit.EchoMode.Password)
        cl.addWidget(self.client_secret_edit, 1, 1)

        cl.addWidget(QLabel("Auth Token Fallback:"), 2, 0)
        self.auth_token_edit = QLineEdit(self.credentials.get("spotify_auth_token", ""))
        self.auth_token_edit.setEchoMode(QLineEdit.EchoMode.Password)
        cl.addWidget(self.auth_token_edit, 2, 1)

        btn_row = QHBoxLayout()
        self.save_creds_btn = QPushButton("💾  Save Credentials")
        btn_row.addWidget(self.save_creds_btn)
        self.clear_creds_btn = QPushButton("🗑  Clear Credentials")
        self.clear_creds_btn.setProperty("cssClass", "danger")
        btn_row.addWidget(self.clear_creds_btn)
        self.creds_status = QLabel(self._summary())
        self.creds_status.setProperty("cssClass", "muted")
        btn_row.addWidget(self.creds_status, 1)
        cl.addLayout(btn_row, 3, 0, 1, 2)

        layout.addWidget(creds)

        # ── Manager Settings ──
        mgr = QGroupBox("🛠  Manager Settings")
        ml = QHBoxLayout(mgr)
        self.save_all_btn = QPushButton("💾  Save All Settings")
        ml.addWidget(self.save_all_btn)
        self.setup_wizard_btn = QPushButton("Setup Wizard")
        self.setup_wizard_btn.setProperty("cssClass", "accent")
        ml.addWidget(self.setup_wizard_btn)
        open_cfg = QPushButton("📂  Open Config Folder")
        open_cfg.setProperty("cssClass", "secondary")
        open_cfg.clicked.connect(lambda: self._open_path(str(USER_DATA_DIR)))
        ml.addWidget(open_cfg)
        open_fail_music = QPushButton("📄  Failed Music CSV")
        open_fail_music.setProperty("cssClass", "secondary")
        open_fail_music.clicked.connect(lambda: self._open_path(str(SPOTIFY_FAILED_CSV)))
        ml.addWidget(open_fail_music)
        open_fail_video = QPushButton("📄  Failed Video CSV")
        open_fail_video.setProperty("cssClass", "secondary")
        open_fail_video.clicked.connect(lambda: self._open_path(str(YOUTUBE_FAILED_CSV)))
        ml.addWidget(open_fail_video)
        ml.addStretch()
        layout.addWidget(mgr)

        # ── Live Log ──
        log_box = QGroupBox("📋  Live Log")
        ll = QVBoxLayout(log_box)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Cascadia Code", 11))
        ll.addWidget(self.log_text)
        clear_btn = QPushButton("Clear Log")
        clear_btn.setProperty("cssClass", "secondary")
        clear_btn.clicked.connect(self.clear_log)
        ll.addWidget(clear_btn, alignment=Qt.AlignmentFlag.AlignLeft)
        layout.addWidget(log_box, 1)

    def _summary(self) -> str:
        c = self.credentials
        return (
            f"ID: {mask_secret(c.get('spotify_client_id', ''))}  "
            f"Secret: {mask_secret(c.get('spotify_client_secret', ''))}  "
            f"Token: {mask_secret(c.get('spotify_auth_token', ''))}"
        )

    def update_status(self, credentials: dict):
        self.credentials = credentials
        self.creds_status.setText(self._summary())

    def append_log(self, message: str):
        """Append a timestamped, color-coded line to the log."""
        fmt = QTextCharFormat()
        if "[ok]" in message or "complete" in message.lower():
            fmt.setForeground(QColor("#3fb950"))
        elif "[fail]" in message or "error" in message.lower() or "[warn]" in message:
            fmt.setForeground(QColor("#f85149"))
        elif "[skip]" in message:
            fmt.setForeground(QColor("#d29922"))
        elif "[start]" in message or "[dry-run]" in message:
            fmt.setForeground(QColor("#58a6ff"))
        elif "Saved" in message or "Starting" in message:
            fmt.setForeground(QColor("#a5d6ff"))
        else:
            fmt.setForeground(QColor("#8b949e"))

        cursor = self.log_text.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        cursor.insertText(message + "\n", fmt)
        self.log_text.setTextCursor(cursor)
        self.log_text.ensureCursorVisible()

    def clear_log(self):
        self.log_text.clear()

    def _open_path(self, path_str: str):
        import os, webbrowser
        from pathlib import Path as P
        p = P(path_str)
        if p.exists():
            if os.name == "nt":
                os.startfile(p)
            else:
                webbrowser.open(p.as_uri())
