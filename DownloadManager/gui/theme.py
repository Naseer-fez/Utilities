"""Dark premium theme stylesheet for the Download Manager GUI."""

DARK_STYLESHEET = """
/* ── Global ── */
QWidget {
    background-color: #0d1117;
    color: #e6edf3;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
}

/* ── Main Window ── */
QMainWindow {
    background-color: #0d1117;
}

/* ── Tab Widget ── */
QTabWidget::pane {
    border: 1px solid #21262d;
    border-radius: 8px;
    background-color: #161b22;
    top: -1px;
}
QTabBar::tab {
    background-color: #21262d;
    color: #8b949e;
    padding: 10px 28px;
    margin-right: 2px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    font-weight: 600;
    font-size: 13px;
    min-width: 140px;
}
QTabBar::tab:selected {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #1a3a5c, stop:1 #1f6feb);
    color: #ffffff;
}
QTabBar::tab:hover:!selected {
    background-color: #30363d;
    color: #c9d1d9;
}

/* ── Group Box ── */
QGroupBox {
    background-color: #161b22;
    border: 1px solid #21262d;
    border-radius: 10px;
    margin-top: 16px;
    padding: 20px 14px 14px 14px;
    font-weight: 700;
    font-size: 13px;
    color: #58a6ff;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 2px 12px;
    background-color: #161b22;
    border-radius: 4px;
    color: #58a6ff;
}

/* ── Line Edit / Spin / Combo ── */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #0d1117;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 6px 10px;
    color: #e6edf3;
    selection-background-color: #1f6feb;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border: 1px solid #58a6ff;
}
QComboBox::drop-down {
    border: none;
    width: 28px;
}
QComboBox QAbstractItemView {
    background-color: #161b22;
    border: 1px solid #30363d;
    color: #e6edf3;
    selection-background-color: #1f6feb;
}

/* ── Buttons ── */
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #238636, stop:1 #1a7f37);
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
    font-weight: 600;
    font-size: 12px;
}
QPushButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #2ea043, stop:1 #238636);
}
QPushButton:pressed {
    background-color: #1a7f37;
}
QPushButton:disabled {
    background-color: #21262d;
    color: #484f58;
}
QPushButton[cssClass="secondary"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #30363d, stop:1 #21262d);
    color: #c9d1d9;
}
QPushButton[cssClass="secondary"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #3d444d, stop:1 #30363d);
}
QPushButton[cssClass="danger"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #da3633, stop:1 #b62324);
}
QPushButton[cssClass="danger"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #f85149, stop:1 #da3633);
}
QPushButton[cssClass="accent"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1f6feb, stop:1 #1a5ad4);
}
QPushButton[cssClass="accent"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #388bfd, stop:1 #1f6feb);
}

/* ── Check Box ── */
QCheckBox {
    spacing: 8px;
    color: #c9d1d9;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 4px;
    border: 1px solid #30363d;
    background-color: #0d1117;
}
QCheckBox::indicator:checked {
    background-color: #1f6feb;
    border-color: #1f6feb;
}

/* ── Progress Bar ── */
QProgressBar {
    background-color: #21262d;
    border: none;
    border-radius: 6px;
    height: 18px;
    text-align: center;
    font-weight: 700;
    font-size: 11px;
    color: #e6edf3;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #1f6feb, stop:1 #58a6ff);
    border-radius: 6px;
}

/* ── Text Edit (Log) ── */
QTextEdit, QPlainTextEdit {
    background-color: #0d1117;
    border: 1px solid #21262d;
    border-radius: 8px;
    padding: 8px;
    font-family: "Cascadia Code", "Consolas", monospace;
    font-size: 12px;
    color: #8b949e;
    selection-background-color: #1f6feb;
}

/* ── Scroll Bar ── */
QScrollBar:vertical {
    background-color: #0d1117;
    width: 10px;
    border-radius: 5px;
}
QScrollBar::handle:vertical {
    background-color: #30363d;
    min-height: 30px;
    border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
    background-color: #484f58;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

/* ── Labels ── */
QLabel {
    color: #c9d1d9;
}
QLabel[cssClass="heading"] {
    font-size: 15px;
    font-weight: 700;
    color: #e6edf3;
}
QLabel[cssClass="muted"] {
    color: #8b949e;
    font-size: 11px;
}
QLabel[cssClass="status-ok"] { color: #3fb950; }
QLabel[cssClass="status-fail"] { color: #f85149; }
QLabel[cssClass="status-info"] { color: #58a6ff; }

/* ── Splitter ── */
QSplitter::handle {
    background-color: #21262d;
}

/* ── Tool Tip ── */
QToolTip {
    background-color: #1c2128;
    color: #e6edf3;
    border: 1px solid #30363d;
    border-radius: 6px;
    padding: 6px;
}
"""
