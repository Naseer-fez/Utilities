import urllib.request
import json
import time
import sys
import os
from metrics import MetricsCollector
import db

class AIDiagnosticEngine:
    def __init__(self):
        self.collector = MetricsCollector()

    def analyze(self, user_prompt, api_key=None):
        # 1. Gather all current telemetry context
        cpu = self.collector.get_cpu_metrics()
        ram = self.collector.get_ram_metrics()
        gpu = self.collector.get_gpu_metrics()
        network = self.collector.get_network_metrics()
        latency = self.collector.get_latency_metrics()
        docker = self.collector.get_docker_metrics()
        
        # Gather process lists
        all_procs = self.collector.get_process_list()
        top_cpu = sorted(all_procs, key=lambda x: x['cpu_percent'], reverse=True)[:8]
        top_ram = sorted(all_procs, key=lambda x: x['memory_mb'], reverse=True)[:8]
        
        # Filter python processes specifically
        python_procs = [p for p in all_procs if "python" in p['name'].lower() or "python" in p['cmdline'].lower()]
        
        # Fetch benchmark history
        benchmarks = db.get_benchmark_history(20)
        
        # Fetch recent logs
        logs = self.collector.get_windows_logs()[:10]

        prompt_lower = user_prompt.lower()
        
        # If API key is available, run advanced Gemini API diagnostics
        if api_key and len(api_key.strip()) > 10:
            return self._query_gemini_api(
                user_prompt, api_key.strip(),
                cpu, ram, gpu, network, latency, docker,
                top_cpu, top_ram, python_procs, benchmarks, logs
            )
        
        # Otherwise, run fallback offline local heuristics engine
        return self._run_local_diagnostics(
            user_prompt, cpu, ram, gpu, network, latency, docker,
            top_cpu, top_ram, python_procs, benchmarks, logs
        )

    def _query_gemini_api(self, prompt, api_key, cpu, ram, gpu, network, latency, docker, top_cpu, top_ram, python_procs, benchmarks, logs):
        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={api_key}"
        
        # Construct highly-detailed prompt context for the LLM
        system_report = f"""
=== AETHERMONITOR CORE SYSTEM STATUS REPORT ===
TIMESTAMP: {time.strftime("%Y-%m-%d %H:%M:%S")}
CPU Load: {cpu.get('percent', 0.0)}% | Temp: {cpu.get('temp_c', 0.0)}C | Speed: {cpu.get('freq_current_mhz', 0.0)} MHz (Max {cpu.get('freq_max_mhz', 0.0)} MHz)
RAM Util: {ram.get('percent', 0.0)}% | Used: {ram.get('used_gb', 0.0)} GB | Total: {ram.get('total_gb', 0.0)} GB
GPU Load: {gpu.get('percent', 0.0)}% | Temp: {gpu.get('temp_c', 0.0)}C | Memory: {gpu.get('memory_used_gb', 0.0)} GB / {gpu.get('memory_total_gb', 0.0)} GB ({gpu.get('name', 'N/A')})
Network Traffic: DL: {network.get('download_mbs', 0.0)} MB/s | UL: {network.get('upload_mbs', 0.0)} MB/s
DNS Ping: {latency.get('dns_ping_ms', 0.0)} ms | Status: {latency.get('status', 'Offline')}

Active Docker Status: active={docker.get('active', False)} | Total Containers={docker.get('container_count', 0)} | Running={docker.get('running_count', 0)}
Docker Containers Detail:
"""
        for c in docker.get('containers', []):
            system_report += f"- Container ID: {c['id'][:8]} | Name: {c['name']} | Image: {c['image']} | Running: {c['running']} | CPU: {c['cpu_percent']}% | RAM: {c['ram_mb']} MB\n"

        system_report += "\nTop 5 CPU Consuming Processes:\n"
        for p in top_cpu[:5]:
            system_report += f"- PID: {p['pid']} | Name: {p['name']} | CPU: {p['cpu_percent']}% | RAM: {p['memory_mb']} MB | Command: {p['cmdline'][:100]}\n"

        system_report += "\nTop 5 RAM Consuming Processes:\n"
        for p in top_ram[:5]:
            system_report += f"- PID: {p['pid']} | Name: {p['name']} | CPU: {p['cpu_percent']}% | RAM: {p['memory_mb']} MB | Command: {p['cmdline'][:100]}\n"

        if python_procs:
            system_report += "\nActive Python Processes:\n"
            for p in python_procs[:5]:
                system_report += f"- PID: {p['pid']} | CPU: {p['cpu_percent']}% | RAM: {p['memory_mb']} MB | Command: {p['cmdline']}\n"

        system_report += "\nRecent Benchmark Runs:\n"
        for b in benchmarks[:3]:
            system_report += f"- Time: {b['timestamp']} | CPU Prime Score: {b['cpu_score']} (Threads: {b['cpu_threads']}) | Memory Bandwidth: {b['memory_bandwidth_gbs']} GB/s | Disk Write/Read: {b['disk_write_mbs']}/{b['disk_read_mbs']} MB/s\n"

        system_report += "\nRecent System Event Log Alerts:\n"
        for l in logs[:5]:
            system_report += f"- [{l['timestamp']}] [{l['level']}] [{l['source']}]: {l['message'][:150]}\n"

        # Combine report with system instructions
        prompt_with_context = f"""
You are AetherMonitor AI, a premium systems diagnostic intelligence. Below is the real-time system performance data of the client workstation.
Examine this report carefully and address the user's specific request.

USER QUESTION: {prompt}

--- DATA REPORT ---
{system_report}

--- INSTRUCTIONS ---
1. Be concise, professional, and technical in your response.
2. If the user complains about slowdowns, identify specific processes, Docker containers, or disk bandwidth locks.
3. Present your analysis using beautiful markdown. Keep headers (###) clear, and use markdown tables where helpful.
4. Recommend actionable steps (e.g. killing a process, restarting a Docker engine, clearing RAM space).
"""
        payload = {
            "contents": [
                {
                    "parts": [
                        {"text": prompt_with_context}
                    ]
                }
            ],
            "generationConfig": {
                "temperature": 0.2,
                "topK": 40,
                "topP": 0.95,
                "maxOutputTokens": 2048
            }
        }
        
        try:
            req_data = json.dumps(payload).encode('utf-8')
            req = urllib.request.Request(
                url, 
                data=req_data, 
                headers={'Content-Type': 'application/json'}
            )
            
            with urllib.request.urlopen(req, timeout=10.0) as response:
                res_body = response.read().decode('utf-8')
                res_json = json.loads(res_body)
                text = res_json['candidates'][0]['content']['parts'][0]['text']
                return text
        except Exception as e:
            # Fallback to local offline diagnostics
            err_msg = str(e)
            fallback_text = self._run_local_diagnostics(
                prompt, cpu, ram, gpu, network, latency, docker,
                top_cpu, top_ram, python_procs, benchmarks, logs
            )
            return f"⚠️ **Gemini API Call Failed ({err_msg}). Falling back to Offline Local Diagnostics:**\n\n{fallback_text}"

    def _run_local_diagnostics(self, prompt, cpu, ram, gpu, network, latency, docker, top_cpu, top_ram, python_procs, benchmarks, logs):
        prompt_lower = prompt.lower()
        
        # Rule 1: RAM Spike Heuristic
        if "spike" in prompt_lower or "ram" in prompt_lower or "memory" in prompt_lower:
            response = f"### 🧠 Offline RAM Diagnostic Report\n\n"
            response += f"Overall RAM utilization is currently at **{ram['percent']}%** of **{ram['total_gb']} GB** total memory.\n"
            response += f"- **Used Memory:** {ram['used_gb']} GB\n"
            response += f"- **Available Memory:** {ram['available_gb']} GB\n\n"
            
            response += "Here are the top RAM-allocating processes currently in memory:\n\n"
            response += "| PID | Process Name | RAM Usage | CPU Usage |\n"
            response += "| :--- | :--- | :--- | :--- |\n"
            for p in top_ram[:5]:
                response += f"| `{p['pid']}` | **{p['name']}** | {p['memory_mb']:.1f} MB | {p['cpu_percent']}% |\n"
            
            response += "\n💡 **Diagnostics & Advice:**\n"
            if ram['percent'] > 80.0:
                hog = top_ram[0]
                response += f"🚨 **High RAM Pressure Alert!** The system memory is heavily utilized at **{ram['percent']}%**.\n"
                response += f"The process `{hog['name']}` (PID: {hog['pid']}) is the primary consumer allocating **{hog['memory_mb']:.1f} MB**.\n"
                response += "Close unused applications or terminate this process to free memory."
            else:
                response += "System RAM consumption is within normal operational thresholds. No memory exhaustion risks detected."
                
            response += "\n\n*(Note: Configure your Gemini API Key in Settings to enable deep LLM reasoning)*"
            return response
            
        # Rule 2: Slowdown / Python Heuristic
        elif "python" in prompt_lower or "slow" in prompt_lower or "pc" in prompt_lower or "cpu" in prompt_lower:
            response = f"### 🖥️ Offline Process CPU Diagnostics\n\n"
            response += f"Overall CPU utilization is currently at **{cpu['percent']}%** running at **{cpu['freq_current_mhz']:.0f} MHz**.\n\n"
            
            if python_procs:
                response += "⚠️ **Found active Python scripts running on the system:**\n\n"
                response += "| PID | CPU % | RAM (MB) | Command Line Details |\n"
                response += "| :--- | :--- | :--- | :--- |\n"
                
                slowest_py = None
                max_cpu = -1.0
                for p in python_procs[:5]:
                    if p['cpu_percent'] > max_cpu:
                        max_cpu = p['cpu_percent']
                        slowest_py = p
                    cmd_trunc = p['cmdline'][:60] + "..." if len(p['cmdline']) > 60 else p['cmdline']
                    response += f"| `{p['pid']}` | **{p['cpu_percent']}%** | {p['memory_mb']:.1f} MB | `{cmd_trunc}` |\n"
                    
                if slowest_py and slowest_py['cpu_percent'] > 5.0:
                    response += f"\n🚨 **CPU slowdown culprit identified:** Python script **PID {slowest_py['pid']}** is actively consuming **{slowest_py['cpu_percent']}% CPU**.\n"
                    response += f"Full command: `{slowest_py['cmdline']}`\n"
                    response += "If this script is running in an unintended loop, termination is recommended."
                else:
                    response += "\n💡 Python processes are running in idle/low-load states (all consuming < 5% CPU). They do not represent a bottleneck."
            else:
                response += "✅ No Python processes detected in current execution table.\n\n"
                response += "Here are the top CPU-consuming system processes currently active:\n\n"
                response += "| PID | Process Name | CPU % | RAM Usage |\n"
                response += "| :--- | :--- | :--- | :--- |\n"
                for p in top_cpu[:5]:
                    response += f"| `{p['pid']}` | **{p['name']}** | **{p['cpu_percent']}%** | {p['memory_mb']:.1f} MB |\n"
                
                response += f"\n💡 `{top_cpu[0]['name']}` is the heaviest load contributor at {top_cpu[0]['cpu_percent']}% CPU."
                
            response += "\n\n*(Note: Configure your Gemini API Key in Settings to enable deep LLM reasoning)*"
            return response
            
        # Rule 3: Benchmark comparison Heuristic
        elif "compare" in prompt_lower or "benchmark" in prompt_lower or "history" in prompt_lower:
            response = f"### 📊 Offline Benchmark History Comparison\n\n"
            if len(benchmarks) < 1:
                return "❌ No historic benchmark runs found in SQLite. Go to the Benchmarks tab and run a benchmark now to generate scores!"
                
            latest = benchmarks[0]
            response += f"**Latest System Benchmark ({latest['timestamp']}):**\n"
            response += f"- **CPU Sieve Score:** {latest['cpu_score']:,} Operations\n"
            response += f"- **Cores Allocated:** {latest['cpu_threads']} Threads\n"
            response += f"- **Memory Copy Bandwidth:** {latest['memory_bandwidth_gbs']:.2f} GB/s\n"
            response += f"- **Disk I/O Speeds:** Write: {latest['disk_write_mbs']:.1f} MB/s | Read: {latest['disk_read_mbs']:.1f} MB/s\n\n"
            
            if len(benchmarks) > 1:
                prev = benchmarks[1]
                cpu_diff = ((latest['cpu_score'] - prev['cpu_score']) / prev['cpu_score']) * 100.0
                mem_diff = ((latest['memory_bandwidth_gbs'] - prev['memory_bandwidth_gbs']) / prev['memory_bandwidth_gbs']) * 100.0
                
                response += f"**Comparison with Previous Run ({prev['timestamp']}):**\n"
                cpu_dir = "📈 increased" if cpu_diff >= 0 else "📉 decreased"
                mem_dir = "📈 improved" if mem_diff >= 0 else "📉 degraded"
                
                response += f"- CPU Sieve Score has {cpu_dir} by **{abs(cpu_diff):.1f}%**.\n"
                response += f"- Memory Bandwidth has {mem_dir} by **{abs(mem_diff):.1f}%**.\n"
            else:
                response += "💡 Run another benchmark to compare scores over time and track performance drifts."
                
            return response
            
        # Rule 4: Docker Containers Heuristic
        elif "docker" in prompt_lower or "container" in prompt_lower:
            response = f"### 🐳 Offline Docker Container Diagnostics\n\n"
            if not docker.get('active', False):
                response += "❌ **Docker Daemon status is Offline.** Verify that Docker Desktop or dockerd service is running on the host machine."
                return response
                
            response += f"Docker Engine is online. System has **{docker['container_count']} total containers** (with **{docker['running_count']} active**).\n\n"
            
            if docker['containers']:
                response += "| ID | Name | Image | State | CPU | RAM |\n"
                response += "| :--- | :--- | :--- | :--- | :--- | :--- |\n"
                for c in docker['containers'][:5]:
                    state_lbl = "🟢 Running" if c['running'] else "🔴 Stopped"
                    response += f"| `{c['id'][:8]}` | **{c['name']}** | `{c['image']}` | {state_lbl} | {c['cpu_percent']}% | {c['ram_mb']:.1f} MB |\n"
            else:
                response += "No container allocations mapped to Docker Engine socket."
            return response

        # Default fallback query rule
        response = f"### 🛡️ AetherMonitor Core Diagnostics Dashboard\n\n"
        response += f"Greetings. Below is a summarized offline analysis of current system metrics:\n\n"
        response += f"- **CPU:** Average utilization is **{cpu['percent']}%** running at **{cpu['freq_current_mhz']:.0f} MHz** with core temperature **{cpu['temp_c']}C**.\n"
        response += f"- **RAM:** Memory usage is at **{ram['percent']}%** ({ram['used_gb']:.2f} GB used / {ram['total_gb']:.2f} GB total).\n"
        response += f"- **GPU:** Load status is **{gpu['percent']}%** at temperature **{gpu['temp_c']}C** (Memory: {gpu['memory_used_gb']:.2f} GB used).\n"
        response += f"- **Network:** Connectivity status is **{latency['status']}** with a DNS ping of **{latency['dns_ping_ms']:.1f} ms**.\n"
        if docker.get('active', False):
            response += f"- **Docker:** Container agent is operational, reporting **{docker['running_count']} running** of {docker['container_count']} total containers.\n"
        else:
            response += f"- **Docker:** Agent reports Docker daemon is currently unresponsive/unloaded.\n"
            
        response += f"\n💡 **AI Diagnostics Recommendation:** Overall performance is nominal. "
        if cpu['percent'] > 75.0:
            response += "System is under high CPU loads. Check the Processes tab to terminate heavy tasks."
        elif ram['percent'] > 85.0:
            response += "Memory utilization is critical. Close unneeded software workloads."
        else:
            response += "Workstation parameters are fully optimal."
            
        response += "\n\n*(Note: To unlock open-ended conversational diagnostics and comprehensive log audits, please enter your Gemini API Key in the Settings panel.)*"
        return response

if __name__ == "__main__":
    engine = AIDiagnosticEngine()
    print("Testing offline engine CPU query:")
    print(engine.analyze("slow system"))