import os
import json
import math
import re
import requests
from typing import List, Dict, Any, Tuple
from backend.config import DB_PATH, OLLAMA_URL, OLLAMA_EMBEDDING_MODEL, GEMINI_API_KEY, OPENAI_API_KEY

class VectorStore:
    """
    A lightweight, high-performance, pure-Python vector store that persists
    to a JSON file. Employs a hybrid search algorithm combining dense vector
    embeddings (Ollama/Cloud) and sparse TF-IDF keyword relevance.
    """

    def __init__(self, db_path: str = DB_PATH):
        self.db_path = db_path
        # Schema:
        # {
        #    "files": { "filename": { "path": "...", "chunk_count": N, "indexed_at": "..." } },
        #    "chunks": [ { "id": "...", "content": "...", "metadata": {...}, "embedding": [...] } ]
        # }
        self.data = {"files": {}, "chunks": []}
        self.idf = {}  # Cached IDF values for vocabulary terms
        self.load()

    def load(self):
        """Loads database from path if exists; otherwise initializes new structure."""
        if os.path.exists(self.db_path):
            try:
                with open(self.db_path, "r", encoding="utf-8") as f:
                    self.data = json.load(f)
                print(f"[INFO] Loaded vector store database containing {len(self.data['chunks'])} chunks across {len(self.data['files'])} files.")
                self.recalculate_tfidf()
            except Exception as e:
                print(f"[WARNING] Database failed to load ({e}). Initializing empty database.")
                self.data = {"files": {}, "chunks": []}
        else:
            self.data = {"files": {}, "chunks": []}

    def save(self):
        """Saves current state to the persistent database JSON file."""
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        try:
            with open(self.db_path, "w", encoding="utf-8") as f:
                json.dump(self.data, f, ensure_ascii=False, indent=2)
            print(f"[INFO] Vector database saved successfully. Total chunks: {len(self.data['chunks'])}")
        except Exception as e:
            print(f"[ERROR] Failed to save database: {e}")

    def clear(self):
        """Wipes the database."""
        self.data = {"files": {}, "chunks": []}
        self.idf = {}
        self.save()

    def remove_file(self, filename: str):
        """Removes a file and all its associated chunks from the index."""
        if filename in self.data["files"]:
            del self.data["files"][filename]
            self.data["chunks"] = [c for c in self.data["chunks"] if c.get("source_name") != filename]
            self.recalculate_tfidf()
            self.save()
            print(f"[INFO] Removed file {filename} from the index.")

    def get_indexed_files(self) -> List[Dict[str, Any]]:
        """Returns list of currently indexed files and metadata."""
        files_list = []
        for name, meta in self.data["files"].items():
            files_list.append({
                "filename": name,
                "path": meta.get("path"),
                "chunk_count": meta.get("chunk_count", 0),
                "indexed_at": meta.get("indexed_at", "Unknown")
            })
        return files_list

    # ==========================================
    # --- TEXT PREPROCESSING & TF-IDF ENGINE ---
    # ==========================================

    @staticmethod
    def tokenize(text: str) -> List[str]:
        """Cleans, lowercases, and splits text into individual tokens (words)."""
        text = text.lower()
        # Keep alphanumeric words and remove punctuation
        words = re.findall(r"\b[a-z0-9]{2,}\b", text)
        return words

    def recalculate_tfidf(self):
        """Computes IDF (Inverse Document Frequency) values across all chunks in the database."""
        self.idf = {}
        total_chunks = len(self.data["chunks"])
        if total_chunks == 0:
            return

        # Count document frequency for each term
        doc_frequencies = {}
        for chunk in self.data["chunks"]:
            tokens = set(self.tokenize(chunk["content"]))
            for token in tokens:
                doc_frequencies[token] = doc_frequencies.get(token, 0) + 1

        # Calculate IDF for each term: log(1 + N / (1 + DF))
        for token, df in doc_frequencies.items():
            self.idf[token] = math.log(1.0 + (total_chunks / (1.0 + df)))

    def get_sparse_scores(self, query: str) -> Dict[str, float]:
        """Computes BM25/TF-IDF keyword score for all chunks relative to the query."""
        scores = {}
        query_tokens = self.tokenize(query)
        if not query_tokens or not self.data["chunks"]:
            return {c["id"]: 0.0 for c in self.data["chunks"]}

        # Query term counts to compute query TF
        query_tf = {}
        for qt in query_tokens:
            query_tf[qt] = query_tf.get(qt, 0) + 1

        for chunk in self.data["chunks"]:
            chunk_tokens = self.tokenize(chunk["content"])
            if not chunk_tokens:
                scores[chunk["id"]] = 0.0
                continue

            chunk_tf = {}
            for ct in chunk_tokens:
                chunk_tf[ct] = chunk_tf.get(ct, 0) + 1

            chunk_score = 0.0
            # Standard dot product of (TF_chunk * IDF) * (TF_query * IDF)
            for token in set(query_tokens):
                if token in chunk_tf and token in self.idf:
                    tf_c = chunk_tf[token] / len(chunk_tokens)
                    tf_q = query_tf[token] / len(query_tokens)
                    idf_val = self.idf[token]
                    chunk_score += (tf_c * idf_val) * (tf_q * idf_val)

            scores[chunk["id"]] = chunk_score

        return scores

    # ==========================================
    # --- DENSE RETRIEVAL / EMBEDDINGS ---
    # ==========================================

    @staticmethod
    def get_ollama_embedding(text: str) -> List[float]:
        """Gets dense vector embeddings from local Ollama instance."""
        try:
            # We try using the new /api/embed API first, then fall back to /api/embeddings
            url = f"{OLLAMA_URL}/api/embed"
            payload = {"model": OLLAMA_EMBEDDING_MODEL, "input": text}
            response = requests.post(url, json=payload, timeout=8)
            
            if response.status_code == 200:
                result = response.json()
                if "embeddings" in result and result["embeddings"]:
                    return result["embeddings"][0]
            
            # Fallback to legacy `/api/embeddings`
            url_legacy = f"{OLLAMA_URL}/api/embeddings"
            payload_legacy = {"model": OLLAMA_EMBEDDING_MODEL, "prompt": text}
            response_legacy = requests.post(url_legacy, json=payload_legacy, timeout=8)
            if response_legacy.status_code == 200:
                return response_legacy.json().get("embedding", [])
        except Exception as e:
            print(f"[WARNING] Ollama embedding failed: {e}. Ensure Ollama is running and '{OLLAMA_EMBEDDING_MODEL}' is pulled.")
        return []

    @staticmethod
    def get_cloud_embedding(text: str) -> List[float]:
        """Gets dense embeddings using cloud providers (Gemini fallback, then OpenAI)."""
        # Gemini Embedding API
        if GEMINI_API_KEY:
            try:
                url = f"https://generativelanguage.googleapis.com/v1beta/models/text-embedding-004:embedContent?key={GEMINI_API_KEY}"
                payload = {
                    "model": "models/text-embedding-004",
                    "content": {"parts": [{"text": text}]}
                }
                response = requests.post(url, json=payload, timeout=8)
                if response.status_code == 200:
                    return response.json().get("embedding", {}).get("values", [])
            except Exception as e:
                print(f"[WARNING] Gemini embedding request failed: {e}")

        # OpenAI Embedding API
        if OPENAI_API_KEY:
            try:
                url = "https://api.openai.com/v1/embeddings"
                headers = {
                    "Content-Type": "application/json",
                    "Authorization": f"Bearer {OPENAI_API_KEY}"
                }
                payload = {
                    "model": "text-embedding-3-small",
                    "input": text
                }
                response = requests.post(url, json=payload, headers=headers, timeout=8)
                if response.status_code == 200:
                    return response.json()["data"][0]["embedding"]
            except Exception as e:
                print(f"[WARNING] OpenAI embedding request failed: {e}")

        return []

    def get_embedding(self, text: str, mode: str = "ollama") -> List[float]:
        """Unified embedding selector with active fallbacks."""
        emb = []
        if mode in ("ollama", "studylm"):
            emb = self.get_ollama_embedding(text)
            if not emb:  # Fall back to cloud if local failed
                emb = self.get_cloud_embedding(text)
        else:
            emb = self.get_cloud_embedding(text)
            if not emb:  # Fall back to local if cloud failed
                emb = self.get_ollama_embedding(text)
        return emb

    @staticmethod
    def cosine_similarity(v1: List[float], v2: List[float]) -> float:
        """Calculates standard cosine similarity between two float vectors."""
        if not v1 or not v2 or len(v1) != len(v2):
            return 0.0
        
        dot_product = sum(a * b for a, b in zip(v1, v2))
        norm_v1 = math.sqrt(sum(a * a for a in v1))
        norm_v2 = math.sqrt(sum(b * b for b in v2))
        
        if norm_v1 == 0 or norm_v2 == 0:
            return 0.0
        
        return dot_product / (norm_v1 * norm_v2)

    def add_chunks(self, chunks: List[Dict[str, Any]], embedding_mode: str = "ollama"):
        """Computes embeddings for chunks and inserts them into the database."""
        if not chunks:
            return

        print(f"[INFO] Indexing {len(chunks)} chunks into vector store. Generating embeddings...")
        
        # Track file metrics
        filename = chunks[0]["source_name"]
        filepath = chunks[0]["source_path"]
        
        from datetime import datetime
        self.data["files"][filename] = {
            "path": filepath,
            "chunk_count": len(chunks),
            "indexed_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        }

        # Remove existing chunks for this file if any (re-indexing protection)
        self.data["chunks"] = [c for c in self.data["chunks"] if c.get("source_name") != filename]

        for i, chunk in enumerate(chunks):
            # Generate embedding
            emb = self.get_embedding(chunk["content"], embedding_mode)
            
            self.data["chunks"].append({
                "id": chunk["id"],
                "content": chunk["content"],
                "page": chunk["page"],
                "source_name": chunk["source_name"],
                "source_path": chunk["source_path"],
                "embedding": emb  # Can be empty list if both local & cloud embedding fail
            })
            
            if (i + 1) % 10 == 0 or (i + 1) == len(chunks):
                print(f"Processed {i + 1}/{len(chunks)} chunks...")

        self.recalculate_tfidf()
        self.save()

    def search(self, query: str, top_k: int = 5, embedding_mode: str = "ollama") -> List[Dict[str, Any]]:
        """
        Executes a hybrid search query. Normalizes dense cosine score and sparse TF-IDF score
        and returns a sorted list of highly relevant chunks with details.
        """
        if not self.data["chunks"]:
            return []

        # 1. Fetch sparse scores (TF-IDF keyword score)
        sparse_scores = self.get_sparse_scores(query)
        max_sparse = max(sparse_scores.values()) if sparse_scores.values() else 0.0

        # 2. Fetch query embedding and calculate dense scores (Cosine Similarity)
        query_emb = self.get_embedding(query, embedding_mode)
        dense_scores = {}
        
        has_dense = bool(query_emb)
        if has_dense:
            for chunk in self.data["chunks"]:
                chunk_emb = chunk.get("embedding", [])
                if chunk_emb and len(chunk_emb) == len(query_emb):
                    dense_scores[chunk["id"]] = self.cosine_similarity(query_emb, chunk_emb)
                else:
                    dense_scores[chunk["id"]] = 0.0
        else:
            print("[INFO] Embedding unavailable for query. Relying on keyword TF-IDF retrieval.")
            dense_scores = {c["id"]: 0.0 for c in self.data["chunks"]}

        max_dense = max(dense_scores.values()) if dense_scores.values() else 0.0

        # 3. Hybrid fusion and scoring
        scored_chunks = []
        for chunk in self.data["chunks"]:
            cid = chunk["id"]
            
            # Normalize scores between 0 and 1
            norm_sparse = sparse_scores.get(cid, 0.0) / max_sparse if max_sparse > 0 else 0.0
            norm_dense = dense_scores.get(cid, 0.0) / max_dense if max_dense > 0 else 0.0
            
            # Calculate final combined score
            # If embeddings are missing, rely 100% on keyword matching. Otherwise, split 50/50.
            if has_dense and max_dense > 0:
                hybrid_score = (0.4 * norm_sparse) + (0.6 * norm_dense)
            else:
                hybrid_score = norm_sparse
            
            scored_chunks.append({
                "chunk": chunk,
                "score": hybrid_score,
                "sparse_score": norm_sparse,
                "dense_score": norm_dense
            })

        # Sort chunks by highest score
        scored_chunks.sort(key=lambda x: x["score"], reverse=True)
        
        # Take the top K, format and return them
        results = []
        for item in scored_chunks[:top_k]:
            c = item["chunk"]
            results.append({
                "id": c["id"],
                "content": c["content"],
                "page": c.get("page", 1),
                "source_name": c["source_name"],
                "source_path": c["source_path"],
                "score": round(item["score"], 4),
                "dense_score": round(item["dense_score"], 4),
                "sparse_score": round(item["sparse_score"], 4)
            })

        return results
