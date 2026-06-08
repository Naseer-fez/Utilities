import os
import json

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
                if "metrics.py" in content and ("File Path:" in content or "write_to_file" in str(step)):
                    step_index = step.get("step_index", "N/A")
                    print(f"Match in {transcript_path} Step {step_index} (content len: {len(content)})")
                    if "File Path:" in content:
                        lines = content.split("\n")
                        print("Sample lines:")
                        for idx, l in enumerate(lines[:15]):
                            print(f"  {l}")
                        print("...")
                        for idx, l in enumerate(lines[-10:]):
                            print(f"  {l}")
                        print("=" * 40)
            except Exception as e:
                pass
