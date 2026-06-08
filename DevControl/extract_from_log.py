import os

log_path = r"C:\Users\FEZ NASEER\.gemini\antigravity\brain\4378ffac-20de-4a7a-8cba-45c1172b87c7\.system_generated\tasks\task-133.log"

if os.path.exists(log_path):
    print("Reading log...")
    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        # Jump to index 3988000
        f.seek(3988000)
        content = f.read(30000) # Read 30KB
        
    print("Writing extracted chunk...")
    with open("extracted_chunk.txt", "w", encoding="utf-8") as f:
        f.write(content)
    print("Done! Extracted 30KB to extracted_chunk.txt")
else:
    print("Log file not found.")
