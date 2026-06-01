# Focus

> **Your dedicated productivity and focus session manager**

Focus is a desktop application designed to help you maintain concentration and track your work sessions seamlessly. 

---

## 🚀 Features

- **Session Management**: Start, pause, and track focus sessions.
- **Custom Configuration**: Personalize behavior using `config.json`.
- **Inter-Process Communication**: Leverages local IPC and named pipes (`test_ipc.exe`, `test_pipe.py`) for background task orchestration.

## 🛠️ Tech Stack

- **Python**: Application orchestration and scripts.
- **C++ / Executables**: Heavy-lifting and background task execution (`focus.exe`).

## 📦 Getting Started

### Installation & Usage

1. Navigate to the `Focus` directory.
2. Review the settings in `config.json`.
3. Start the focus session manager:
   ```cmd
   run_focus_engine.bat
   ```
   *Alternatively, run the python entry point directly:*
   ```bash
   python start_session.py
   ```

## 📂 Project Structure

- `focus.exe`: Main application binary.
- `start_session.py`: Entry point for launching sessions.
- `config.json`: User configuration settings.
- `instruction.md`: Internal application documentation.
