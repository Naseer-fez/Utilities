import os
import json
import re
import sys

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
                if "ui/app.js" in content and "File Path:" in content:
                    lines = content.split("\n")
                    file_lines = []
                    for l in lines:
                        m = re.match(r"^\s*(\d+):\s?(.*)", l)
                        if m:
                            file_lines.append(m.group(2))
                    if len(file_lines) == 86:
                        print(f"Found 86-line version in {transcript_path} Step {step.get('step_index')}")
                        out_path = r"D:\AI\Agents\DevControl\ui\app.js"
                        os.makedirs(os.path.dirname(out_path), exist_ok=True)
                        with open(out_path, "w", encoding="utf-8") as out_f:
                            out_f.write("\n".join(file_lines))
                        print("Restored ui/app.js successfully!")
                        sys.exit(0)
            except Exception as e:
                pass
