import os
import re

log_path = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain\4378ffac-20de-4a7a-8cba-45c1172b87c7\.system_generated\tasks\task-133.log"

if not os.path.exists(log_path):
    print("Log not found.")
    exit(1)

with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
    content = f.read()

print(f"Log length: {len(content)}")

# Look for patterns like "1: import psutil" or "import psutil" that continue for many lines
# Let's search for "import psutil" in the log
idx = 0
while True:
    idx = content.find("import psutil", idx)
    if idx == -1:
        break
    print(f"\nFound 'import psutil' at index {idx}")
    # Print the next 2000 chars
    print(content[idx:idx+1500])
    print("-" * 50)
    idx += len("import psutil")
    
# Let's also search for "from metrics import MetricsCollector"
idx = 0
while True:
    idx = content.find("from metrics import", idx)
    if idx == -1:
        break
    print(f"\nFound 'from metrics import' at index {idx}")
    print(content[idx:idx+1500])
    print("-" * 50)
    idx += len("from metrics import")
