import os
import json
import re

brain_dir = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain"
output_dir = r"D:\AI\Agents\DevControl"

# Group views by (file_rel_path, step_key) -> content
# step_key = (transcript_path, step_index)
views = {}

def process_file_view(target_path, content_text, step_key):
    norm_path = target_path.replace("\\", "/").lower()
    if "devcontrol" not in norm_path:
        return
        
    parts = norm_path.split("/devcontrol/")
    if len(parts) < 2:
        return
    rel_path = parts[1]
    if "recover" in rel_path.lower() or "inspect" in rel_path.lower():
        return
        
    lines = content_text.split("\n")
    file_lines = {}
    has_line_numbers = False
    
    for l in lines:
        m = re.match(r"^\s*(\d+):\s?(.*)", l)
        if m:
            line_num = int(m.group(1))
            line_val = m.group(2)
            file_lines[line_num] = line_val
            has_line_numbers = True
            
    if has_line_numbers and file_lines:
        if rel_path not in views:
            views[rel_path] = {}
        if step_key not in views[rel_path]:
            views[rel_path][step_key] = {}
        views[rel_path][step_key].update(file_lines)

def process_write_tool(target_path, code_content, step_key):
    norm_path = target_path.replace("\\", "/").lower()
    if "devcontrol" not in norm_path:
        return
        
    parts = norm_path.split("/devcontrol/")
    if len(parts) < 2:
        return
    rel_path = parts[1]
    if "recover" in rel_path.lower() or "inspect" in rel_path.lower():
        return
        
    if "<truncated" in code_content:
        return
        
    lines = code_content.split("\n")
    lines_dict = {i+1: l for i, l in enumerate(lines)}
    
    if rel_path not in views:
        views[rel_path] = {}
    views[rel_path][step_key] = lines_dict

def decode_string(s):
    if not isinstance(s, str):
        return s
    if (s.startswith('"') and s.endswith('"')) or (s.startswith("'") and s.endswith("'")):
        try: return json.loads(s)
        except: pass
    if '\\n' in s or '\\t' in s or '\\"' in s or '\\\\' in s:
        try:
            test_str = s
            if not test_str.startswith('"'):
                test_str = '"' + test_str.replace('"', '\\"') + '"'
            return json.loads(test_str)
        except Exception:
            pass
    return s

# Scan transcripts
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
                step_idx = step.get("step_index", 0)
                step_key = f"{os.path.basename(os.path.dirname(root))}_step_{step_idx}"
                tool_calls = step.get("tool_calls", [])
                
                # Check write_to_file
                for tc in tool_calls:
                    name = tc.get("name", "")
                    args = tc.get("args", {})
                    if isinstance(args, str):
                        try: args = json.loads(args)
                        except: pass
                    
                    resolved_args = {}
                    for k, v in args.items():
                        resolved_args[k] = decode_string(v)
                        
                    if "write_to_file" in name:
                        target = resolved_args.get("TargetFile")
                        content = resolved_args.get("CodeContent")
                        if target and content:
                            process_write_tool(target, content, step_key)
                            
                # Check VIEW_FILE content
                if step.get("type") == "VIEW_FILE" or "view_file" in str(tool_calls).lower():
                    content = step.get("content", "")
                    path_match = re.search(r"File Path:\s*`file:///([^`]+)`", content)
                    target_path = None
                    if path_match:
                        target_path = path_match.group(1)
                    else:
                        for tc in tool_calls:
                            args = tc.get("args", {})
                            if isinstance(args, str):
                                try: args = json.loads(args)
                                except: pass
                            target_path = args.get("AbsolutePath") or args.get("TargetFile")
                            if target_path:
                                target_path = decode_string(target_path)
                                break
                    if target_path and content:
                        process_file_view(target_path, content, step_key)
            except Exception as e:
                pass

print("\n--- Selecting Best Versions ---")
for rel, step_dict in views.items():
    # Find step with most lines
    best_step = None
    best_lines_dict = {}
    best_line_count = 0
    
    # We can check if we have a contiguous series of line numbers.
    # If a view is missing lines in the middle (e.g. key 47 is missing but 46 and 141 exist),
    # we want to detect that and count the size of the largest contiguous block or just count the keys.
    # Actually, let's look at the keys. If the keys are not contiguous, it's truncated!
    # Let's verify contiguity.
    for step_key, lines_dict in step_dict.items():
        keys = sorted(lines_dict.keys())
        if not keys:
            continue
            
        # Count contiguous lines from 1
        contiguous_count = 0
        for i in range(1, len(keys) + 1):
            if i in lines_dict:
                contiguous_count += 1
            else:
                break
        
        # If the step has more contiguous lines, or if it is just a larger view,
        # let's select it.
        # Actually, let's sort step keys to prefer the latest conversations and step indices if count is close.
        # But here, we just check total number of unique contiguous lines from 1, or total keys.
        # If we have a view of a middle chunk (e.g. step 104 viewed 240 to 449), it won't start at 1.
        # Let's just find the step with the maximum number of keys, or print them so we can see!
        total_keys = len(lines_dict)
        print(f"File {rel} in {step_key} has {total_keys} keys (range: {min(keys)}-{max(keys)})")
        if total_keys > best_line_count:
            best_line_count = total_keys
            best_step = step_key
            best_lines_dict = lines_dict

    if best_step:
        sorted_keys = sorted(best_lines_dict.keys())
        sorted_lines = [best_lines_dict[k] for k in sorted_keys]
        out_path = os.path.join(output_dir, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as out_f:
            out_f.write("\n".join(sorted_lines))
        print(f"==> Restored {rel} using version from {best_step} ({len(sorted_lines)} lines)")
