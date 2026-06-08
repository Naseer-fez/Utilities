import os
import json
import asyncio
import subprocess
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from metrics import MetricsCollector
from ai_engine import AIDiagnosticEngine
import db

# Setup FastAPI App
app = FastAPI(title="AetherMonitor Backend", version="1.0.0")

# Enable CORS for local cross-origin connections
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
BENCHMARK_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "benchmark.exe")

collector = MetricsCollector()
ai_engine = AIDiagnosticEngine()

# Input validation schemas
class SettingsRequest(BaseModel):
    gemini_api_key: str

class ChatRequest(BaseModel):
    prompt: str

# Config management helpers
def get_config():
    if not os.path.exists(CONFIG_PATH):
        return {"gemini_api_key": ""}
    try:
        with open(CONFIG_PATH, "r") as f:
            return json.load(f)
    except Exception:
        return {"gemini_api_key": ""}

def save_config(cfg):
    try:
        with open(CONFIG_PATH, "w") as f:
            json.dump(cfg, f, indent=4)
        return True
    except Exception:
        return False

# Hourly background logging aggregator
async def telemetry_logger_task():
    while True:
        try:
            # Wait 1 hour between telemetry logs
            await asyncio.sleep(3600)
            
            cpu_m = collector.get_cpu_metrics()
            ram_m = collector.get_ram_metrics()
            net_m = collector.get_network_metrics()
            
            db.save_telemetry(
                cpu_avg=cpu_m.get('percent', 0.0),
                ram_avg=ram_m.get('percent', 0.0),
                net_in_mb=net_m.get('download_mbs', 0.0),
                net_out_mb=net_m.get('upload_mbs', 0.0),
                temp_max=cpu_m.get('temp_c', 0.0)
            )
        except Exception:
            pass

@app.on_event("startup")
async def startup_event():
    asyncio.create_task(telemetry_logger_task())

# Endpoints
@app.get("/api/processes")
def get_processes():
    try:
        return collector.get_process_list()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/docker")
def get_docker():
    try:
        return collector.get_docker_metrics()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/apis")
def get_apis():
    try:
        return collector.check_apis()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/logs")
def get_logs():
    try:
        return collector.get_windows_logs()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/chat")
def post_chat(req: ChatRequest):
    try:
        cfg = get_config()
        api_key = cfg.get("gemini_api_key", "").strip()
        response_text = ai_engine.analyze(req.prompt, api_key=api_key)
        return {"status": "success", "response": response_text}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/benchmark/run")
def run_benchmark():
    if not os.path.exists(BENCHMARK_EXE):
        raise HTTPException(status_code=404, detail="C++ Benchmark binary not found.")
    
    try:
        # Run statically compiled benchmark.exe (wait up to 30 seconds)
        # Use shell execution or direct executable spawning
        res = subprocess.run(
            [BENCHMARK_EXE],
            capture_output=True,
            text=True,
            timeout=30.0
        )
        
        if res.returncode != 0:
            raise Exception(f"Benchmark exited with error code {res.returncode}: {res.stderr}")
            
        data = json.loads(res.stdout.strip())
        
        # Save into SQLite database
        db.save_benchmark(
            cpu_score=data.get("cpu_score", 0.0),
            cpu_threads=data.get("cpu_threads", 1),
            memory_bandwidth_gbs=data.get("memory_bandwidth_gbs", 0.0),
            disk_write_mbs=data.get("disk_write_mbs", 0.0),
            disk_read_mbs=data.get("disk_read_mbs", 0.0)
        )
        return data
    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=504, detail="Benchmark process timed out.")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/benchmark/history")
def get_benchmark_history():
    try:
        return db.get_benchmark_history(30)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/settings")
def get_settings():
    return get_config()

@app.post("/api/settings")
def post_settings(req: SettingsRequest):
    cfg = get_config()
    cfg["gemini_api_key"] = req.gemini_api_key.strip()
    if save_config(cfg):
        return {"status": "success", "message": "Settings updated successfully."}
    else:
        raise HTTPException(status_code=500, detail="Failed to write configuration file.")

# WebSocket Telemetry stream at 1Hz
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            # Stream system parameters
            data = collector.gather_all()
            await websocket.send_json(data)
            await asyncio.sleep(1.0)
    except WebSocketDisconnect:
        pass
    except Exception:
        pass

# Mount static UI at the root
UI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui")
if os.path.exists(UI_DIR):
    app.mount("/", StaticFiles(directory=UI_DIR, html=True), name="ui")
else:
    @app.get("/")
    def read_root():
        return {"message": "AetherMonitor backend operational. Static UI folder is missing."}