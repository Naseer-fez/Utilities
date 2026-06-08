import os
import shutil
import sys
from pathlib import Path
from typing import Optional, List
from fastapi import FastAPI, UploadFile, File, Form, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, StreamingResponse
from pydantic import BaseModel

# Add project root to sys.path to allow running this script directly
sys.path.append(str(Path(__file__).resolve().parent.parent))

from backend.config import STUDY_DOCS_DIR, get_config_status
from backend.document_processor import DocumentProcessor
from backend.vector_store import VectorStore
from backend.models import LLMClient
from backend.agent import StudyAgent
from backend.fine_tuner import run_fine_tuner

app = FastAPI(title="StudyFlow AI API")

# --- ENABLE CORS FOR DUAL-PORT REACT DEV WORK ---
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Allow all for local private setup
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Initialize global Vector Store
vector_store = VectorStore()

# Ensure Study Directory exists
os.makedirs(STUDY_DOCS_DIR, exist_ok=True)

# --- PYDANTIC SCHEMAS ---
class QueryRequest(BaseModel):
    query: str
    provider: str
    model: str
    temperature: float = 0.5
    top_k: int = 4
    enable_agentic_flow: bool = True

class ClearRequest(BaseModel):
    confirm: bool

class PathRequest(BaseModel):
    path: str

# --- API ENDPOINTS ---

@app.get("/api/status")
def get_status():
    """Returns local system configuration status and active model list."""
    config_details = get_config_status()
    ollama_info = LLMClient.check_ollama_status()
    
    # Check if StudyLM weights are active
    weights_path = Path(__file__).resolve().parent / "db" / "fine_tuned_weights.json"
    studylm_status = "online" if weights_path.exists() else "offline"
    
    return {
        "status": "online",
        "config": config_details,
        "ollama": ollama_info,
        "studylm": {
            "status": studylm_status,
            "models": ["studylm-1.0-scratch"]
        }
    }

@app.get("/api/files")
def get_files():
    """Lists currently indexed files in the RAG Vector Database."""
    return vector_store.get_indexed_files()

@app.delete("/api/files/{filename}")
def delete_file(filename: str):
    """Deletes a file and all its associated vector chunks from the database."""
    try:
        vector_store.remove_file(filename)
        return {"message": f"Successfully deleted {filename} from index."}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/clear")
def clear_database(req: ClearRequest):
    """Wipes the entire vector store index."""
    if req.confirm:
        vector_store.clear()
        return {"message": "Successfully cleared all files and indices."}
    raise HTTPException(status_code=400, detail="Confirmation required.")

@app.post("/api/scan")
def scan_study_docs():
    """Scans the configured STUDY_DOCS_DIR, processing any new/unindexed files."""
    try:
        if not os.path.exists(STUDY_DOCS_DIR):
            return {"indexed": [], "message": f"Folder {STUDY_DOCS_DIR} does not exist."}
            
        indexed_files = [f["filename"] for f in vector_store.get_indexed_files()]
        processed_files = []
        
        # Look for PDF, TXT, MD files
        supported_exts = {".pdf", ".txt", ".md", ".json", ".csv"}
        
        for file in os.listdir(STUDY_DOCS_DIR):
            file_path = os.path.join(STUDY_DOCS_DIR, file)
            if os.path.isfile(file_path):
                ext = Path(file_path).suffix.lower()
                if ext in supported_exts and file not in indexed_files:
                    # Index the file
                    chunks = DocumentProcessor.process_file(file_path)
                    if chunks:
                        # Index using the current configured default embedding mode
                        vector_store.add_chunks(chunks)
                        processed_files.append(file)
                        
        # Trigger dynamic fine-tuning on scanned files
        run_fine_tuner()
                        
        return {
            "processed": processed_files,
            "message": f"Scan completed. Indexed {len(processed_files)} new files."
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/upload")
async def upload_file(
    file: UploadFile = File(...),
    embedding_mode: str = Form("ollama")
):
    """Directly uploads and indexes a study file from the browser."""
    try:
        # Save uploaded file to study_docs dir
        save_path = os.path.join(STUDY_DOCS_DIR, file.filename)
        with open(save_path, "wb") as buffer:
            shutil.copyfileobj(file.file, buffer)
            
        # Parse and chunk immediately
        chunks = DocumentProcessor.process_file(save_path)
        if not chunks:
            # Clean up empty file
            try:
                os.remove(save_path)
            except:
                pass
            raise HTTPException(status_code=400, detail="The file is empty or text extraction failed.")
            
        vector_store.add_chunks(chunks, embedding_mode=embedding_mode)
        
        # Trigger dynamic fine-tuning on direct uploads
        run_fine_tuner()
        
        return {
            "filename": file.filename,
            "chunk_count": len(chunks),
            "message": f"File '{file.filename}' uploaded and indexed successfully!"
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/query")
def query_assistant(req: QueryRequest):
    """
    Primary chat query endpoint.
    Streams logs, sources, answer tokens, and the study deck in real-time.
    """
    if not req.query or len(req.query.strip()) == 0:
        raise HTTPException(status_code=400, detail="Query cannot be empty.")
        
    import json
    
    def event_generator():
        if req.enable_agentic_flow:
            for event in StudyAgent.run_agentic_flow_stream(
                user_query=req.query,
                vector_store=vector_store,
                provider=req.provider,
                model=req.model,
                temperature=req.temperature,
                top_k=req.top_k
            ):
                yield f"data: {json.dumps(event)}\n\n"
        else:
            for event in StudyAgent.run_standard_flow_stream(
                user_query=req.query,
                vector_store=vector_store,
                provider=req.provider,
                model=req.model,
                temperature=req.temperature,
                top_k=req.top_k
            ):
                yield f"data: {json.dumps(event)}\n\n"

    return StreamingResponse(event_generator(), media_type="text/event-stream")

# --- SERVE REACT PRODUCTION BUILD STATIC ASSETS ---

# Root path returns main index.html from Vite build
@app.get("/")
def get_index():
    index_path = Path(__file__).resolve().parent.parent / "frontend" / "dist" / "index.html"
    if index_path.exists():
        return FileResponse(str(index_path))
    else:
        return {
            "warning": "Vite React build not found. Running in Development Mode.",
            "instructions": "Please run 'npm run build' in the frontend folder, or start start.bat launcher."
        }

# Mount static React bundle assets from frontend/dist
frontend_dist = Path(__file__).resolve().parent.parent / "frontend" / "dist"
if frontend_dist.exists():
    app.mount("/", StaticFiles(directory=str(frontend_dist)), name="frontend")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("backend.app:app", host="127.0.0.1", port=8000, reload=True)
