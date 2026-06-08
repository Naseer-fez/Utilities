import os

brain_dir = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain"

for root, dirs, files in os.walk(brain_dir):
    for file in files:
        path = os.path.join(root, file)
        try:
            # Skip large binary files
            if file.endswith((".exe", ".dll", ".pb", ".png", ".jpg", ".zip", ".tar", ".gz")):
                continue
                
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
                if "class MetricsCollector" in content:
                    print(f"Found match in {path} (size: {len(content)} bytes)")
                    # Count how many occurrences
                    occurrences = content.count("class MetricsCollector")
                    print(f"  Occurrences: {occurrences}")
        except Exception:
            pass
