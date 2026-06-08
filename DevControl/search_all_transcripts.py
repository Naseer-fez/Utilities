import os
import json

brain_dir = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain"

write_calls = []

for root, dirs, files in os.walk(brain_dir):
    if "transcript.jsonl" not in files:
        continue
    t_path = os.path.join(root, "transcript.jsonl")
    conv_id = os.path.basename(root)
    
    try:
        with open(t_path, "r", encoding="utf-8", errors="ignore") as f:
            for line_idx, line in enumerate(f):
                if '"write_to_file"' not in line:
                    continue
                try:
                    step = json.loads(line)
                    tool_calls = step.get("tool_calls", [])
                    for tc in tool_calls:
                        if tc.get("name") == "write_to_file":
                            args = tc.get("args", {})
                            if isinstance(args, str):
                                try: args = json.loads(args)
                                except: pass
                            target = args.get("TargetFile", "")
                            if "index.html" in target or "style.css" in target or "server.py" in target or "ai_engine.py" in target:
                                content = args.get("CodeContent", "")
                                write_calls.append({
                                    "conv_id": conv_id,
                                    "step": step.get("step_index", 0),
                                    "target": target,
                                    "length": len(content),
                                    "is_truncated": "<truncated" in content
                                })
                except Exception:
                    pass
    except Exception as e:
        print(f"Error reading {t_path}: {e}")

# Sort by length descending
write_calls.sort(key=lambda x: x["length"], reverse=True)

print("--- Top Write Calls Found ---")
for wc in write_calls[:40]:
    print(f"Conv: {wc['conv_id']} | Step: {wc['step']} | Target: {wc['target']} | Length: {wc['length']} | Truncated: {wc['is_truncated']}")
