import os
from pathlib import Path
from dotenv import load_dotenv

# Base Directory path (d:\AI\Agents)
BASE_DIR = Path(__file__).resolve().parent.parent

# Load configuration from .env in base directory
dotenv_path = BASE_DIR / ".env"
if dotenv_path.exists():
    load_dotenv(dotenv_path)
else:
    load_dotenv()

# App Directories
STUDY_DOCS_DIR = os.getenv("STUDY_DOCS_DIR")
if not STUDY_DOCS_DIR:
    STUDY_DOCS_DIR = str(BASE_DIR / "study_docs")
else:
    STUDY_DOCS_DIR = str(Path(STUDY_DOCS_DIR).resolve())

# Create study_docs directory if it doesn't exist
os.makedirs(STUDY_DOCS_DIR, exist_ok=True)

DB_DIR = BASE_DIR / "backend" / "db"
os.makedirs(DB_DIR, exist_ok=True)

DB_PATH = str(DB_DIR / "vector_db.json")

# LLM & Embedding Settings
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434").rstrip("/")
OLLAMA_DEFAULT_MODEL = os.getenv("OLLAMA_DEFAULT_MODEL", "llama3")
OLLAMA_EMBEDDING_MODEL = os.getenv("OLLAMA_EMBEDDING_MODEL", "nomic-embed-text")

# Cloud Model Keys
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "").strip()
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "").strip()
ANTHROPIC_API_KEY = os.getenv("ANTHROPIC_API_KEY", "").strip()

def get_config_status():
    """Returns a dictionary showing which models are available and active"""
    return {
        "ollama": {
            "url": OLLAMA_URL,
            "default_model": OLLAMA_DEFAULT_MODEL,
            "embedding_model": OLLAMA_EMBEDDING_MODEL
        },
        "cloud_keys_present": {
            "gemini": bool(GEMINI_API_KEY),
            "openai": bool(OPENAI_API_KEY),
            "anthropic": bool(ANTHROPIC_API_KEY)
        },
        "study_docs_dir": STUDY_DOCS_DIR,
        "db_path": DB_PATH
    }
