import sqlite3
import json
import os
from datetime import datetime, timedelta

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'aegis_monitor.db')

def get_db_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    conn = get_db_connection()
    cursor = conn.cursor()
    
    # Metrics history table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS metrics_history (
            timestamp TEXT PRIMARY KEY,
            cpu_util REAL,
            ram_util REAL,
            gpu_util REAL,
            temp_cpu REAL,
            temp_gpu REAL,
            net_sent_rate REAL,
            net_recv_rate REAL,
            latency REAL
        )
    ''')
    
    # Benchmarks table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS benchmarks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT,
            benchmark_type TEXT,
            score REAL,
            details TEXT
        )
    ''')
    
    # API endpoints table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS api_endpoints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            url TEXT UNIQUE,
            name TEXT,
            method TEXT DEFAULT 'GET',
            status TEXT,
            latency REAL,
            last_checked TEXT
        )
    ''')
    
    # System settings table
  
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def add_api_endpoint(url, name, method='GET'):
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute('''
            INSERT INTO api_endpoints (url, name, method)
            VALUES (?, ?, ?)
        ''', (url, name, method))
        conn.commit()
        success = True
    except sqlite3.IntegrityError:
        success = False
    conn.close()
    return success

def delete_api_endpoint(endpoint_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('DELETE FROM api_endpoints WHERE id = ?', (endpoint_id,))
    conn.commit()
    conn.close()

def update_api_status(endpoint_id, status, latency):
    conn = get_db_connection()
    cursor = conn.cursor()
    timestamp = datetime.now().isoformat()
    cursor.execute('''
        UPDATE api_endpoints 
        SET status = ?, latency = ?, last_checked = ?
        WHERE id = ?
    ''', (status, latency, timestamp, endpoint_id))
    conn.commit()
    conn.close()

def get_setting(key, default=''):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT value FROM system_settings WHERE key = ?', (key,))
    row = cursor.fetchone()
    conn.close()
    return row[0] if row else default

def save_setting(key, value):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('''
        INSERT OR REPLACE INTO system_settings (key, value)
        VALUES (?, ?)
    ''', (key, str(value)))
    conn.commit()
    conn.close()

if __name__ == '__main__':
    init_db()
    print("Database initialized.")
