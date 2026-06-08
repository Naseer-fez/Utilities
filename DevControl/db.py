import sqlite3
import os

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "aether_monitor.db")

def init_db():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Create benchmarks table
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS benchmarks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
        cpu_score REAL,
        cpu_threads INTEGER,
        memory_bandwidth_gbs REAL,
        disk_write_mbs REAL,
        disk_read_mbs REAL
    )
    """)
    
    # Create telemetry history table
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS telemetry_history (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
        cpu_avg REAL,
        ram_avg REAL,
        net_in_mb REAL,
        net_out_mb REAL,
        temp_max REAL
    )
    """)
    
    conn.commit()
    conn.close()

def save_benchmark(cpu_score, cpu_threads, memory_bandwidth_gbs, disk_write_mbs, disk_read_mbs):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
    INSERT INTO benchmarks (cpu_score, cpu_threads, memory_bandwidth_gbs, disk_write_mbs, disk_read_mbs)
    VALUES (?, ?, ?, ?, ?)
    """, (cpu_score, cpu_threads, memory_bandwidth_gbs, disk_write_mbs, disk_read_mbs))
    conn.commit()
    conn.close()

def get_benchmark_history(limit=50):
    conn = sqlite3.connect(DB_PATH)
    # Return as list of dicts for easy JSON serialization
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM benchmarks ORDER BY timestamp DESC LIMIT ?", (limit,))
    rows = cursor.fetchall()
    history = [dict(r) for r in rows]
    conn.close()
    return history

def save_telemetry(cpu_avg, ram_avg, net_in_mb, net_out_mb, temp_max):
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
    INSERT INTO telemetry_history (cpu_avg, ram_avg, net_in_mb, net_out_mb, temp_max)
    VALUES (?, ?, ?, ?, ?)
    """, (cpu_avg, ram_avg, net_in_mb, net_out_mb, temp_max))
    conn.commit()
    conn.close()

def get_telemetry_history(limit=100):
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM telemetry_history ORDER BY timestamp DESC LIMIT ?", (limit,))
    rows = cursor.fetchall()
    history = [dict(r) for r in rows]
    conn.close()
    return history

# Initialize on import
init_db()
