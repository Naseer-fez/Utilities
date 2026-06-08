// Benchmarks diagnostics module
import { ajaxRequest } from './utils.js';
import { renderBenchmarkHistoryChart } from './charts.js';

export function executeBenchmark() {
    const loader = document.getElementById('bench-loader');
    const runBtn = document.getElementById('run-bench-btn');
    if (loader) loader.classList.remove('hide');
    if (runBtn) runBtn.disabled = true;

    return ajaxRequest('POST', '/api/benchmark/run')
        .then(data => {
            if (loader) loader.classList.add('hide');
            if (runBtn) runBtn.disabled = false;
            
            if (data) {
                displayBenchmarkResults(data, new Date().toLocaleString());
                fetchBenchmarkHistory();
            } else {
                alert("Benchmark run encountered an error. Verify that the C++ benchmark executable is fully compiled.");
            }
        })
        .catch(() => {
            if (loader) loader.classList.add('hide');
            if (runBtn) runBtn.disabled = false;
            alert("Benchmark call failed.");
        });
}

export function fetchBenchmarkHistory() {
    return ajaxRequest('GET', '/api/benchmark/history')
        .then(data => {
            if (data && data.length > 0) {
                const historyCard = document.getElementById('bench-history-card');
                if (historyCard) historyCard.classList.remove('hide');
                
                // Display latest run in box
                const latest = data[0];
                displayBenchmarkResults(latest, latest.timestamp);
                renderBenchmarkHistoryChart(data);
            }
        });
}

export function displayBenchmarkResults(data, timestamp) {
    const latestCard = document.getElementById('latest-bench-card');
    if (latestCard) latestCard.classList.remove('hide');
    
    const tsBadge = document.getElementById('bench-timestamp');
    if (tsBadge) tsBadge.innerText = timestamp;
    
    // Format cpu prime score to millions/thousands cleanly
    const score = data.cpu_score;
    let scoreLabel = score;
    if (score > 1000000) {
        scoreLabel = `${(score / 1000000).toFixed(1)}M`;
    } else if (score > 1000) {
        scoreLabel = `${(score / 1000).toFixed(1)}K`;
    }
    
    const cpuScore = document.getElementById('bench-cpu-score');
    if (cpuScore) cpuScore.innerText = scoreLabel;
    
    const cpuThreads = document.getElementById('bench-cpu-threads');
    if (cpuThreads) cpuThreads.innerText = data.cpu_threads;
    
    const memBandwidth = document.getElementById('bench-mem-bandwidth');
    if (memBandwidth) memBandwidth.innerHTML = `${data.memory_bandwidth_gbs.toFixed(2)} <span class="unit">GB/s</span>`;
    
    const diskWrite = document.getElementById('bench-disk-write');
    if (diskWrite) diskWrite.innerHTML = `${data.disk_write_mbs.toFixed(1)} <span class="unit">MB/s</span>`;
    
    const diskRead = document.getElementById('bench-disk-read');
    if (diskRead) diskRead.innerHTML = `${data.disk_read_mbs.toFixed(1)} <span class="unit">MB/s</span>`;
}
