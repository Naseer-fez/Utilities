import sys
import os
import time
import threading
import socket
import uvicorn
from PySide6.QtCore import QUrl, Qt
from PySide6.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget
from PySide6.QtWebEngineWidgets import QWebEngineView

class NoSignalServer(uvicorn.Server):
    def install_signal_handlers(self) -> None:
        pass

# Thread target to run uvicorn in-process
def run_fastapi_server():
    try:
        # Force cwd to this directory to ensure assets are resolved correctly
        os.chdir(os.path.dirname(os.path.abspath(__file__)))
        
        config = uvicorn.Config("server:app", host="127.0.0.1", port=5821, log_level="warning")
        server = NoSignalServer(config=config)
        server.run()
    except Exception as e:
        import traceback
        print(f"Error in FastAPI server thread: {e}", file=sys.stderr)
        traceback.print_exc(file=sys.stderr)

def wait_for_server(host="127.0.0.1", port=5821, timeout=12.0):
    """Wait for the server port to become active by polling it with sockets."""
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except (OSError, ConnectionRefusedError):
            time.sleep(0.1)
    return False

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AetherMonitor // AI-Powered Core Diagnostics")
        self.resize(1200, 800)
        self.setMinimumSize(1000, 650)
        
        # Central widget and layout
        central_widget = QWidget(self)
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # Create QWebEngineView
        self.browser = QWebEngineView(self)
        layout.addWidget(self.browser)
        
        # Enable developer tools if needed
        # self.browser.page().setDevToolsPage(...) 
        
        # Load local FastAPI URL
        self.browser.setUrl(QUrl("http://127.0.0.1:5821/"))

def main():
    # 1. Enable High DPI Scaling
    os.environ["QT_AUTO_SCREEN_SCALE_FACTOR"] = "1"
    
    # Chromium switches to prevent flickering and blinking in QWebEngineView on Windows
    sys.argv.append("--disable-gpu-driver-bug-workarounds")
    sys.argv.append("--disable-gpu-vsync")
    sys.argv.append("--disable-gpu-compositing")
    
    # 2. Launch FastAPI server in a background thread
    server_thread = threading.Thread(target=run_fastapi_server, daemon=True)
    server_thread.start()
    
    # Wait for the backend server to bind to the port (robustly up to 12 seconds)
    if not wait_for_server("127.0.0.1", 5821, timeout=12.0):
        print("Warning: FastAPI server port 5821 did not become active in time.", file=sys.stderr)
    
    # 3. Launch native PySide6 Desktop GUI
    app = QApplication(sys.argv)
    
    window = MainWindow()
    window.show()
    
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
