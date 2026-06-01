# DevControl

> **A telemetry and API monitoring engine**

DevControl is a specialized system designed to capture, monitor, and analyze telemetry data from various applications. Built with Python, it provides a backend server and an AI engine for extracting and processing logs in real-time.

---

## 🚀 Features

- **Telemetry Monitoring**: Monitor application behavior and metrics in real-time.
- **AI-Powered Log Analysis**: Extract meaningful information and chunks from large log sets using the built-in AI engine.
- **Metrics Database**: SQLite-based database to store and query telemetry events (`aether_monitor.db`).
- **Integration Testing**: Built-in scripts to simulate usage and verify telemetry flow.

## 🛠️ Tech Stack

- **Python**: Core engine and server.
- **SQLite**: Lightweight data storage.
- **C++ Components**: For benchmarking and high-performance tasks.

## 📦 Getting Started

### Prerequisites

- Python 3.10+
- Dependencies listed in `requirements.txt` (if applicable)

### Installation

1. Clone the repository and navigate to `DevControl`:
   ```bash
   cd DevControl
   ```
2. Run the server:
   ```bash
   python server.py
   ```

## 📂 Project Structure

- `server.py`: Main backend server to receive telemetry.
- `metrics.py` / `ai_engine.py`: Data processing and log extraction.
- `database.py`: Database interaction layer.
- `benchmark.cpp`: Performance benchmarking utility.
