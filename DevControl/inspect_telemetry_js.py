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
                if "ui/modules/telemetry.js" in content and "File Path:" in content:
                    lines = content.split("\n")
                    file_lines = []
                    for l in lines:
                        m = re.match(r"^\s*(\d+):\s?(.*)", l)
                        if m:
                            file_lines.append(m.group(2))
                    print(f"Found version in {transcript_path} Step {step.get('step_index')} with {len(file_lines)} lines")
                    if len(file_lines) > 80:
                        # Let's inspect lines 38-50
                        print("Lines 35-55:")
                        for idx, l in enumerate(file_lines[34:55]):
                            print(f"  {idx+35}: {l}")
                        print("=" * 40)
            except Exception as e:
                pass
