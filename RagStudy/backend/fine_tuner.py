import os
import re
import json
import collections
import sys
from pathlib import Path

# Add project root to sys.path to allow running this script directly
sys.path.append(str(Path(__file__).resolve().parent.parent))

from backend.config import STUDY_DOCS_DIR, DB_DIR
from backend.document_processor import DocumentProcessor

# Define standard stopwords to filter out noise
STOPWORDS = {
    'a', 'about', 'above', 'after', 'again', 'against', 'all', 'am', 'an', 'and', 'any', 'are', 'arent', 'as', 'at',
    'be', 'because', 'been', 'before', 'being', 'below', 'between', 'both', 'but', 'by', 'can', 'cant', 'cannot',
    'co', 'could', 'couldnt', 'did', 'didnt', 'do', 'does', 'doesnt', 'doing', 'dont', 'down', 'during',
    'each', 'few', 'for', 'from', 'further', 'had', 'hadnt', 'has', 'hasnt', 'have', 'havent', 'having',
    'he', 'hed', 'hell', 'hes', 'her', 'here', 'heres', 'hers', 'herself', 'him', 'himself', 'his', 'how', 'hows',
    'i', 'id', 'ill', 'im', 'ive', 'if', 'in', 'into', 'is', 'isnt', 'it', 'its', 'itself', 'lets', 'me', 'more',
    'most', 'mustnt', 'my', 'myself', 'no', 'nor', 'not', 'of', 'off', 'on', 'once', 'only', 'or', 'other', 'ought',
    'our', 'ours', 'ourselves', 'out', 'over', 'own', 'same', 'shant', 'she', 'shed', 'shell', 'shes', 'should',
    'shouldnt', 'so', 'some', 'such', 'than', 'that', 'thats', 'the', 'their', 'theirs', 'them', 'themselves',
    'then', 'there', 'theres', 'these', 'they', 'theyd', 'theyll', 'theyre', 'theyve', 'this', 'those', 'through',
    'to', 'too', 'under', 'until', 'up', 'very', 'was', 'wasnt', 'we', 'wed', 'well', 'were', 'weve', 'werent',
    'what', 'whats', 'when', 'whens', 'where', 'wheres', 'which', 'while', 'who', 'whos', 'whom', 'why', 'whys',
    'with', 'wont', 'would', 'wouldnt', 'you', 'youd', 'youll', 'youre', 'youve', 'your', 'yours', 'yourself', 'yourselves'
}

def clean_and_tokenize(text: str):
    """Tokenizes text, strips punctuation, and normalizes to lowercase."""
    text = text.lower()
    # Replace non-alphabetic chars with space, keeping single hyphens/apostrophes
    text = re.sub(r'[^a-z0-9\s\-\']', ' ', text)
    tokens = text.split()
    return [t for t in tokens if t not in STOPWORDS and len(t) > 2]

def extract_sentences(text: str):
    """Splits text into cohesive sentences for extraction and fact retrieval."""
    # Split on sentence boundaries: ., !, ?, with whitespace
    sentences = re.split(r'(?<=[.!?])\s+', text)
    return [s.strip() for s in sentences if len(s.strip()) > 10]

def extract_definition(sentence: str):
    """
    Attempts to extract (concept, definition_text) using linguistic triggers.
    E.g. 'Photosynthesis is defined as...' -> ('Photosynthesis', 'defined as...')
    """
    triggers = [
        r'\b(is\s+defined\s+as|are\s+defined\s+as)\b',
        r'\b(refers\s+to|refer\s+to)\b',
        r'\b(is\s+a\s+process\s+by\s+which|are\s+processes\s+by\s+which)\b',
        r'\b(is\s+the\s+study\s+of|are\s+the\s+studies\s+of)\b',
        r'\b(is\s+characterized\s+by|are\s+characterized\s+by)\b',
        r'\b(means|mean)\b',
        r'\b(consists\s+of|consist\s+of)\b',
        r'\b(stands\s+for|stand\s+for)\b',
        r'\b(is|are|was|were)\b' # Keep as final general fallback
    ]
    
    for trigger in triggers:
        match = re.search(trigger, sentence, re.IGNORECASE)
        if match:
            trigger_idx = match.start()
            trigger_end = match.end()
            concept = sentence[:trigger_idx].strip()
            def_text = sentence[trigger_end:].strip()
            
            # Basic validation to avoid false positives
            if 2 < len(concept.split()) <= 6 and len(def_text.split()) > 4:
                # Clean leading articles/punctuation
                concept = re.sub(r'^[-\s\*\•\+\d\.\,\(\)]+', '', concept).strip()
                # Capitalize first word of concept
                concept = concept.capitalize()
                return concept, sentence
    return None

def run_fine_tuner():
    """Scans all documents in study_docs, builds associations, definitions, and saves weights."""
    print("[Fine-Tuner] Starting workspace document fine-tuning...")
    
    if not os.path.exists(STUDY_DOCS_DIR):
        print(f"[Fine-Tuner] Warning: Directory {STUDY_DOCS_DIR} does not exist.")
        return
        
    all_sentences = []
    definitions = []
    word_cooccurrence = collections.defaultdict(collections.Counter)
    word_frequencies = collections.defaultdict(int)
    
    files_processed = 0
    
    for file in os.listdir(STUDY_DOCS_DIR):
        file_path = os.path.join(STUDY_DOCS_DIR, file)
        if os.path.isfile(file_path):
            ext = Path(file_path).suffix.lower()
            if ext in {".pdf", ".txt", ".md", ".json", ".csv"}:
                try:
                    print(f"[Fine-Tuner] Parsing {file}...")
                    raw_text = DocumentProcessor.load_document(file_path)
                    
                    # Split into pages to trace location info
                    pages = raw_text.split("--- PAGE ")
                    
                    for p in pages:
                        p_num = 1
                        p_content = p
                        if p.strip() and p[0].isdigit():
                            # Extract page number
                            header_match = re.match(r"^(\d+)\s*---", p)
                            if header_match:
                                p_num = int(header_match.group(1))
                                p_content = p[header_match.end():]
                                
                        sentences = extract_sentences(p_content)
                        for sent in sentences:
                            # Save sentence with source info
                            sent_entry = {
                                "text": sent,
                                "source": file,
                                "page": p_num
                            }
                            all_sentences.append(sent_entry)
                            
                            # Try to extract definitions
                            defn = extract_definition(sent)
                            if defn:
                                concept, raw_def = defn
                                definitions.append({
                                    "concept": concept,
                                    "text": raw_def,
                                    "source": file,
                                    "page": p_num
                                })
                                
                            # Tokenize for co-occurrence mapping
                            tokens = clean_and_tokenize(sent)
                            for token in tokens:
                                word_frequencies[token] += 1
                                
                            # Connect nearby terms (window size 5)
                            for i, t1 in enumerate(tokens):
                                start = max(0, i - 4)
                                end = min(len(tokens), i + 5)
                                for j in range(start, end):
                                    if i != j:
                                        t2 = tokens[j]
                                        word_cooccurrence[t1][t2] += 1
                                        
                    files_processed += 1
                except Exception as e:
                    print(f"[Fine-Tuner Warning] Error parsing {file}: {e}")
                    
    # Normalize co-occurrences to produce association scores (similar to neural attention weights)
    concepts_db = {}
    for term, freq in word_frequencies.items():
        if freq >= 2: # Keep only reasonably frequent terms as concept candidates
            associations = {}
            total_cooc = sum(word_cooccurrence[term].values())
            if total_cooc > 0:
                for target, count in word_cooccurrence[term].most_common(12):
                    # Relative score from 0.0 to 1.0
                    associations[target] = round(count / total_cooc, 4)
                    
            concepts_db[term] = {
                "frequency": freq,
                "associations": associations
            }
            
    # Compile the final weights
    weights = {
        "metadata": {
            "files_processed": files_processed,
            "total_sentences": len(all_sentences),
            "total_concepts": len(concepts_db),
            "total_definitions": len(definitions)
        },
        "concepts": concepts_db,
        "definitions": definitions,
        "document_sentences": all_sentences
    }
    
    # Save to db folder
    os.makedirs(DB_DIR, exist_ok=True)
    weights_path = os.path.join(DB_DIR, "fine_tuned_weights.json")
    with open(weights_path, "w", encoding="utf-8") as f:
        json.dump(weights, f, indent=2, ensure_ascii=False)
        
    print(f"[Fine-Tuner] Completed! Fine-tuned on {files_processed} files. Found {len(concepts_db)} concepts, {len(definitions)} definitions.")

if __name__ == "__main__":
    run_fine_tuner()
