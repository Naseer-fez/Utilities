import os
import json
import re

brain_dir = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain"

for root, dirs, files in os.walk(brain_dir):
    if "transcript.jsonl" not in files:
        continue
    transcript_path = os.path.join(root, "transcript.jsonl")
    
    with open(transcript_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.strip():
                continue
            try:
                step = json.loads(line)
                content = step.get("content", "")
                if "server.py" in content and ("File Path:" in content or "write_to_file" in str(step)):
                    step_index = step.get("step_index", "N/A")
                    print(f"Match in {transcript_path} Step {step_index} (content len: {len(content)})")
                    # If it's a VIEW_FILE, let's print lines around 40-60
                    if "File Path:" in content:
                        lines = content.split("\n")
                        print("Sample lines from view:")
                        for idx, l in enumerate(lines):
                            if 35 < idx < 65:
                                print(f"  {l}")
                        print("=" * 40)
            except Exception as e:
                pass
