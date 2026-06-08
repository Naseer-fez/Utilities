import re
import os
import pypdf
from typing import List, Dict, Any

class DocumentProcessor:
    """Handles loading, parsing and chunking of study documents."""

    @staticmethod
    def extract_text_from_pdf(file_path: str) -> str:
        """Extracts text from a PDF file using pypdf."""
        text = ""
        try:
            reader = pypdf.PdfReader(file_path)
            for page_idx, page in enumerate(reader.pages):
                page_text = page.extract_text()
                if page_text:
                    text += f"\n--- PAGE {page_idx + 1} ---\n{page_text}"
        except Exception as e:
            print(f"Error parsing PDF {file_path}: {e}")
        return text

    @staticmethod
    def extract_text_from_txt(file_path: str) -> str:
        """Extracts text from a UTF-8/ANSI plain text file."""
        encodings = ["utf-8", "latin-1", "utf-16", "cp1252"]
        for encoding in encodings:
            try:
                with open(file_path, "r", encoding=encoding) as f:
                    return f.read()
            except UnicodeDecodeError:
                continue
        raise ValueError(f"Could not decode text file: {file_path}")

    @classmethod
    def load_document(cls, file_path: str) -> str:
        """Identifies file extension and extracts all raw text."""
        ext = os.path.splitext(file_path)[1].lower()
        if ext == ".pdf":
            return cls.extract_text_from_pdf(file_path)
        elif ext in (".txt", ".md", ".json", ".csv"):
            return cls.extract_text_from_txt(file_path)
        else:
            raise ValueError(f"Unsupported file format: {ext}")

    @classmethod
    def chunk_text(cls, text: str, chunk_size: int = 800, chunk_overlap: int = 150) -> List[Dict[str, Any]]:
        """Splits raw text into overlapping chunks, tracking page numbers."""
        # Find all page markers
        page_markers = []
        for m in re.finditer(r"\n--- PAGE (\d+) ---\n", text):
            page_markers.append((m.start(), int(m.group(1))))
        
        # If no page markers, default to page 1
        if not page_markers:
            page_markers.append((0, 1))

        # Basic overlapping character chunking
        raw_chunks = []
        start = 0
        text_len = len(text)
        
        while start < text_len:
            end = min(start + chunk_size, text_len)
            if end < text_len:
                # Find word boundary
                space_idx = text.rfind(" ", start, end)
                if space_idx > start + chunk_size // 2:
                    end = space_idx
            raw_chunks.append(text[start:end])
            start += (chunk_size - chunk_overlap)
            if chunk_size - chunk_overlap <= 0:
                break
                
        processed_chunks = []
        search_start = 0
        
        for idx, chunk_content in enumerate(raw_chunks):
            cleaned = chunk_content.strip()
            if not cleaned:
                continue
                
            pos = text.find(chunk_content, search_start)
            if pos == -1:
                pos = search_start
            else:
                search_start = pos + len(chunk_content)
                
            page_num = 1
            for marker_pos, marker_page in page_markers:
                if marker_pos <= pos:
                    page_num = marker_page
                else:
                    break
                    
            processed_chunks.append({
                "chunk_id": idx,
                "content": cleaned,
                "page": page_num,
                "length": len(cleaned)
            })
            
        return processed_chunks

    @classmethod
    def process_file(cls, file_path: str, chunk_size: int = 800, chunk_overlap: int = 150) -> List[Dict[str, Any]]:
        """Loads a document and splits it into fully annotated metadata chunks."""
        filename = os.path.basename(file_path)
        raw_text = cls.load_document(file_path)
        chunks = cls.chunk_text(raw_text, chunk_size, chunk_overlap)
        
        # Inject standard metadata
        for chunk in chunks:
            chunk["source_name"] = filename
            chunk["source_path"] = file_path
            # Dynamic ID that combines file and chunk index
            chunk["id"] = f"{filename}_chunk_{chunk['chunk_id']}"
            
        return chunks
