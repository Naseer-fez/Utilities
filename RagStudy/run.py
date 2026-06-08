import os
import sys
import subprocess
import threading
import time
import webbrowser
import signal

# Curated harmonious color palette
C_BACKEND = "\033[92m"  # Sleek Green
C_FRONTEND = "\033[96m" # Sleek Cyan
C_SYSTEM = "\033[93m"   # Warm Yellow
C_RESET = "\033[0m"

# Ensure ANSI escape codes work on Windows
if sys.platform == "win32":
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass

# Global process tracking
backend_process = None
frontend_process = None

def kill_process_tree(process):
    """Gracefully and forcefully cleans up a process and all its children."""
    if not process:
        return
    try:
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
    except Exception:
        try:
            process.terminate()
        except Exception:
            pass

def log_stream(stream, prefix, color):
    """Logs stream output line by line with color prefixing."""
    for line in iter(stream.readline, b""):
        try:
            decoded_line = line.decode("utf-8", errors="ignore").strip()
            if decoded_line:
                print(f"{color}{prefix}{C_RESET} {decoded_line}")
        except Exception:
            pass

def main():
    global backend_process, frontend_process

    # Detect directories
    base_dir = os.path.dirname(os.path.abspath(__file__))
    frontend_dir = os.path.join(base_dir, "frontend")
    
    # Locate python executable in venv
    venv_python = os.path.join(base_dir, ".venv", "Scripts", "python.exe") if sys.platform == "win32" else os.path.join(base_dir, ".venv", "bin", "python")
    python_exe = venv_python if os.path.exists(venv_python) else sys.executable

    # Locate npm
    npm_bin = "npm.cmd" if sys.platform == "win32" else "npm"

    print(f"{C_SYSTEM}[System]{C_RESET} Starting StudyFlow AI Development Servers...")

    # Start Backend FastAPI
    try:
        backend_args = [
            python_exe, "-m", "uvicorn", "backend.app:app", 
            "--host", "127.0.0.1", "--port", "8000", "--reload"
        ]
        backend_process = subprocess.Popen(
            backend_args,
            cwd=base_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            shell=False
        )
        
        # Start backend logging thread
        backend_thread = threading.Thread(
            target=log_stream, 
            args=(backend_process.stdout, "[Backend]", C_BACKEND),
            daemon=True
        )
        backend_thread.start()
        print(f"{C_SYSTEM}[System]{C_RESET} Backend process launched on port 8000.")
    except Exception as e:
        print(f"{C_SYSTEM}[System Error]{C_RESET} Failed to start backend: {e}")
        sys.exit(1)

    # Start Frontend Vite
    try:
        frontend_process = subprocess.Popen(
            [npm_bin, "run", "dev"],
            cwd=frontend_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            shell=(sys.platform == "win32")
        )
        
        # Start frontend logging thread
        frontend_thread = threading.Thread(
            target=log_stream, 
            args=(frontend_process.stdout, "[Frontend]", C_FRONTEND),
            daemon=True
        )
        frontend_thread.start()
        print(f"{C_SYSTEM}[System]{C_RESET} Frontend process launched on port 5173.")
    except Exception as e:
        print(f"{C_SYSTEM}[System Error]{C_RESET} Failed to start frontend: {e}")
        kill_process_tree(backend_process)
        sys.exit(1)

    # Wait 4 seconds and open browser
    time.sleep(4)
    print(f"{C_SYSTEM}[System]{C_RESET} Opening web browser at http://localhost:5173 ...")
    webbrowser.open("http://localhost:5173")

    print(f"{C_SYSTEM}[System]{C_RESET} Both servers are running! Press {C_SYSTEM}Ctrl+C{C_RESET} to shut down concurrently.")

    try:
        while True:
            if backend_process.poll() is not None:
                print(f"{C_SYSTEM}[System Warning]{C_RESET} Backend process exited with code {backend_process.poll()}")
                break
            if frontend_process.poll() is not None:
                print(f"{C_SYSTEM}[System Warning]{C_RESET} Frontend process exited with code {frontend_process.poll()}")
                break
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"\n{C_SYSTEM}[System]{C_RESET} Shutting down servers gracefully...")
    finally:
        print(f"{C_SYSTEM}[System]{C_RESET} Cleaning up process tree...")
        kill_process_tree(backend_process)
        kill_process_tree(frontend_process)
        print(f"{C_SYSTEM}[System] Offline.{C_RESET}")

if __name__ == "__main__":
    main()
