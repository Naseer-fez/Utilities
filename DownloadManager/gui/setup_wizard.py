"""First-run setup wizard for non-technical users."""
from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QWizard,
    QWizardPage,
)

from .backend import (
    APP_NAME,
    BASE_DIR,
    CONFIG_PATH,
    USER_DATA_DIR,
    default_config,
    save_credentials,
    write_json,
)


def _label(text: str) -> QLabel:
    widget = QLabel(text)
    widget.setWordWrap(True)
    widget.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    return widget


class IntroPage(QWizardPage):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("Welcome")
        self.setSubTitle("A short setup will prepare Download Manager for everyday use.")

        layout = QVBoxLayout(self)
        layout.addWidget(
            _label(
                "This wizard saves your download folders, optional Spotify credentials, "
                "and safe defaults. You can change everything later from Settings."
            )
        )
        layout.addWidget(
            _label(
                "Use this app only for media you own, created yourself, or are otherwise "
                "allowed to download."
            )
        )
        layout.addStretch()


class FoldersPage(QWizardPage):
    def __init__(self, config: dict, parent=None):
        super().__init__(parent)
        self.setTitle("Choose Download Folders")
        self.setSubTitle("Pick simple folders where music and videos will be saved.")

        layout = QGridLayout(self)
        layout.setColumnStretch(1, 1)

        self.music_edit = QLineEdit(str(config.get("spotify_output", "")))
        self.video_edit = QLineEdit(str(config.get("youtube_output", "")))

        music_btn = QPushButton("Choose...")
        music_btn.clicked.connect(lambda: self._pick_folder(self.music_edit, "Choose music folder"))
        video_btn = QPushButton("Choose...")
        video_btn.clicked.connect(lambda: self._pick_folder(self.video_edit, "Choose video folder"))

        layout.addWidget(QLabel("Music folder:"), 0, 0)
        layout.addWidget(self.music_edit, 0, 1)
        layout.addWidget(music_btn, 0, 2)
        layout.addWidget(QLabel("Video folder:"), 1, 0)
        layout.addWidget(self.video_edit, 1, 1)
        layout.addWidget(video_btn, 1, 2)
        layout.addWidget(
            _label("Tip: the default folders are inside your Windows user profile, so no admin rights are needed."),
            2,
            0,
            1,
            3,
        )

    def _pick_folder(self, target: QLineEdit, title: str) -> None:
        start = target.text().strip() or str(Path.home())
        folder = QFileDialog.getExistingDirectory(self, title, start)
        if folder:
            target.setText(folder)

    def validatePage(self) -> bool:
        for path_text in (self.music_edit.text(), self.video_edit.text()):
            if not path_text.strip():
                QMessageBox.warning(self, APP_NAME, "Please choose both download folders.")
                return False
        return True


class SpotifyPage(QWizardPage):
    def __init__(self, config: dict, credentials: dict, parent=None):
        super().__init__(parent)
        self.setTitle("Spotify Setup")
        self.setSubTitle("Choose a playlist CSV and optionally save Spotify credentials.")

        layout = QVBoxLayout(self)

        csv_row = QHBoxLayout()
        csv_row.addWidget(QLabel("Playlist CSV:"))
        self.csv_edit = QLineEdit(str(config.get("spotify_csv", BASE_DIR / "playlist.csv")))
        csv_row.addWidget(self.csv_edit, 1)
        csv_btn = QPushButton("Browse...")
        csv_btn.clicked.connect(self._pick_csv)
        csv_row.addWidget(csv_btn)
        layout.addLayout(csv_row)

        layout.addWidget(
            _label(
                "Spotify credentials are optional. Client ID and Client Secret are recommended "
                "because they last longer than short-lived auth tokens."
            )
        )

        grid = QGridLayout()
        self.client_id_edit = QLineEdit(credentials.get("spotify_client_id", ""))
        self.client_secret_edit = QLineEdit(credentials.get("spotify_client_secret", ""))
        self.auth_token_edit = QLineEdit(credentials.get("spotify_auth_token", ""))
        for edit in (self.client_id_edit, self.client_secret_edit, self.auth_token_edit):
            edit.setEchoMode(QLineEdit.EchoMode.Password)

        grid.addWidget(QLabel("Client ID:"), 0, 0)
        grid.addWidget(self.client_id_edit, 0, 1)
        grid.addWidget(QLabel("Client Secret:"), 1, 0)
        grid.addWidget(self.client_secret_edit, 1, 1)
        grid.addWidget(QLabel("Auth token fallback:"), 2, 0)
        grid.addWidget(self.auth_token_edit, 2, 1)
        layout.addLayout(grid)
        layout.addStretch()

    def _pick_csv(self) -> None:
        start = str(Path(self.csv_edit.text()).parent) if self.csv_edit.text().strip() else str(BASE_DIR)
        file_name, _ = QFileDialog.getOpenFileName(
            self,
            "Choose Spotify playlist CSV",
            start,
            "CSV files (*.csv);;All files (*)",
        )
        if file_name:
            self.csv_edit.setText(file_name)


class YouTubePage(QWizardPage):
    def __init__(self, config: dict, parent=None):
        super().__init__(parent)
        self.setTitle("YouTube Defaults")
        self.setSubTitle("These can be left as-is for most people.")

        layout = QGridLayout(self)
        layout.setColumnStretch(1, 1)

        self.quality_combo = QComboBox()
        self.quality_combo.addItems(["360", "480", "720", "1080", "1440", "2160", "best"])
        self.quality_combo.setCurrentText(str(config.get("youtube_quality", "1080")))

        self.browser_combo = QComboBox()
        self.browser_combo.addItems(["", "chrome", "edge", "firefox", "brave", "opera"])
        self.browser_combo.setCurrentText(str(config.get("youtube_browser_cookies", "")))
        self.browser_combo.setEditable(True)

        self.use_cookie_file = QCheckBox("Use a cookies.txt file if I choose one later")
        self.use_cookie_file.setChecked(bool(config.get("youtube_use_cookies", False)))

        layout.addWidget(QLabel("Preferred quality:"), 0, 0)
        layout.addWidget(self.quality_combo, 0, 1)
        layout.addWidget(QLabel("Browser cookies:"), 1, 0)
        layout.addWidget(self.browser_combo, 1, 1)
        layout.addWidget(self.use_cookie_file, 2, 0, 1, 2)
        layout.addWidget(
            _label(
                "If YouTube asks for sign-in, choose the browser where you are already logged in. "
                "Otherwise, leave Browser cookies blank."
            ),
            3,
            0,
            1,
            2,
        )


class FinishPage(QWizardPage):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("Ready")
        self.setSubTitle("Setup is ready to save.")

        layout = QVBoxLayout(self)
        layout.addWidget(
            _label(
                "Click Finish to save setup. Download Manager will open with friendly defaults, "
                "Start Menu shortcut support, and all settings stored in your user profile."
            )
        )
        layout.addWidget(_label(f"Settings folder: {USER_DATA_DIR}"))
        layout.addStretch()


class SetupWizard(QWizard):
    def __init__(self, config: dict | None = None, credentials: dict | None = None, parent=None):
        super().__init__(parent)
        self.config = default_config()
        self.config.update(config or {})
        self.credentials = credentials or {}

        self.setWindowTitle(f"{APP_NAME} Setup Wizard")
        self.setWizardStyle(QWizard.WizardStyle.ModernStyle)
        self.setOption(QWizard.WizardOption.NoBackButtonOnStartPage, True)
        self.setOption(QWizard.WizardOption.HaveHelpButton, False)
        self.resize(720, 460)

        self.intro_page = IntroPage()
        self.folders_page = FoldersPage(self.config)
        self.spotify_page = SpotifyPage(self.config, self.credentials)
        self.youtube_page = YouTubePage(self.config)
        self.finish_page = FinishPage()

        self.addPage(self.intro_page)
        self.addPage(self.folders_page)
        self.addPage(self.spotify_page)
        self.addPage(self.youtube_page)
        self.addPage(self.finish_page)

    def accept(self) -> None:
        config = default_config()
        config.update(self.config)
        config.update(
            {
                "setup_wizard_completed": True,
                "spotify_csv": self.spotify_page.csv_edit.text().strip() or str(BASE_DIR / "playlist.csv"),
                "spotify_output": self.folders_page.music_edit.text().strip(),
                "spotify_use_cookies": False,
                "youtube_output": self.folders_page.video_edit.text().strip(),
                "youtube_quality": self.youtube_page.quality_combo.currentText(),
                "youtube_browser_cookies": self.youtube_page.browser_combo.currentText().strip(),
                "youtube_use_cookies": self.youtube_page.use_cookie_file.isChecked(),
            }
        )

        USER_DATA_DIR.mkdir(parents=True, exist_ok=True)
        write_json(CONFIG_PATH, config)
        save_credentials(
            self.spotify_page.auth_token_edit.text(),
            self.spotify_page.client_id_edit.text(),
            self.spotify_page.client_secret_edit.text(),
        )
        super().accept()
