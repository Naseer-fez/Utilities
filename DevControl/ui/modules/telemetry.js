// Telemetry client module
import { state } from './state.js';
import { wsUrl, jsonParseSafely } from './utils.js';
import { pushChartMetrics } from './charts.js';

export function connectWebSocket() {
    console.log(`Connecting to WebSocket: ${wsUrl}`);
    const indicator = document.querySelector('.status-indicator');
    const statusText = document.querySelector('.status-text');

    state.ws = new WebSocket(wsUrl);

    state.ws.onopen = () => {
        if (indicator) {
            indicator.className = 'status-indicator online';
        }
        if (statusText) {
            statusText.innerText = 'TELEMETRY ONLINE';
        }
    };

    state.ws.onmessage = (event) => {
        try {
            const data = jsonParseSafely(event.data);
            if (data) {
                updateDashboardMetrics(data);
            }
        } catch (err) {
            console.error("Error reading websocket telemetry packet:", err);
        }
    };

    state.ws.onclose = () => {
        if (indicator) {
            indicator.className = 'status-indicator offline';
        }
        if (statusText) {
            statusText.innerText = 'DISCONNECTED - RECONNECTING';
        }
        // Attempt reconnection after 3 seconds
        setTimeout(connectWebSocket, 3000);
    };
}

export function updateDashboardMetrics(data) {
    // 1. CPU Update
    const cpuCircle = document.getElementById('cpu-circle');
    const cpuPercent = Math.min(100, Math.max(0, data.cpu.percent));
    if (cpuCircle) {
        updateCircleStroke(cpuCircle, cpuPercent);
    }
    
    const cpuPctText = document.getElementById('cpu-percent-txt');
    if (cpuPctText) {
        cpuPctText.innerText = Math.round(cpuPercent);
    }

    const cpuFreqText = document.getElementById('cpu-freq-txt');
    if (cpuFreqText) {
        cpuFreqText.innerText = `${data.cpu.freq_current_mhz.toFixed(0)} MHz`;
    }

    const cpuTempBadge = document.getElementById('cpu-temp-badge');
    if (cpuTempBadge) {
        cpuTempBadge.innerText = `${data.cpu.temp_c}°C`;
    }

    // 2. RAM Update
    const ramCircle = document.getElementById('ram-circle');
    const ramPercent = Math.min(100, Math.max(0, data.ram.percent));
    if (ramCircle) {
        updateCircleStroke(ramCircle, ramPercent);
    }
    
    const ramPctText = document.getElementById('ram-percent-txt');
    if (ramPctText) {
        ramPctText.innerText = Math.round(ramPercent);
    }

    const ramUsedText = document.getElementById('ram-used-txt');
    if (ramUsedText) {
        ramUsedText.innerText = `${data.ram.used_gb.toFixed(1)} / ${data.ram.total_gb.toFixed(1)} GB`;
    }

    // 3. GPU Update
    const gpuCircle = document.getElementById('gpu-circle');
    const gpuPercent = Math.min(100, Math.max(0, data.gpu.percent));
    if (gpuCircle) {
        updateCircleStroke(gpuCircle, gpuPercent);
    }
    
    const gpuPctText = document.getElementById('gpu-percent-txt');
    if (gpuPctText) {
        gpuPctText.innerText = Math.round(gpuPercent);
    }
    
    const gpuTemp = document.getElementById('gpu-temp-badge');
    if (gpuTemp) {
        gpuTemp.innerText = `${data.gpu.temp_c}°C`;
    }
    
    const gpuName = document.getElementById('gpu-name-txt');
    if (gpuName) {
        gpuName.innerText = data.gpu.name;
    }
    
    const gpuMem = document.getElementById('gpu-mem-txt');
    if (gpuMem) {
        gpuMem.innerText = `${data.gpu.memory_used_gb.toFixed(2)} / ${data.gpu.memory_total_gb.toFixed(1)} GB`;
    }

    // 4. Network Update
    const netDown = document.getElementById('net-down-txt');
    if (netDown) {
        netDown.innerHTML = `${data.network.download_mbs.toFixed(3)} <span class="unit">MB/s</span>`;
    }
    
    const netUp = document.getElementById('net-up-txt');
    if (netUp) {
        netUp.innerHTML = `${data.network.upload_mbs.toFixed(3)} <span class="unit">MB/s</span>`;
    }
    
    // Update Latency tab Primary DNS latency display
    const dnsText = document.getElementById('dns-latency-txt');
    if (dnsText) {
        dnsText.innerHTML = `${data.latency.dns_ping_ms.toFixed(1)} <span class="unit">ms</span>`;
    }

    // 5. Historical Charts Dynamic Update
    const timeLabel = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    pushChartMetrics(timeLabel, cpuPercent, ramPercent, gpuPercent, data.network.download_mbs, data.network.upload_mbs);
}

export function updateCircleStroke(circle, percent) {
    // Circumference is 2 * pi * r = 2 * 3.14159 * 45 = 282.743
    const circumference = 282.743;
    const offset = circumference - (percent / 100) * circumference;
    circle.style.strokeDashoffset = offset;
}
