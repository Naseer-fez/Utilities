import { state } from './state.js';

export function initCharts() {
    const telemetryCanvas = document.getElementById('telemetryChart');
    if (!telemetryCanvas) return;
    const ctx1 = telemetryCanvas.getContext('2d');
    
    // Custom translucent gradients for glassmorphism fills
    const gradientCpu = ctx1.createLinearGradient(0, 0, 0, 180);
    gradientCpu.addColorStop(0, 'rgba(6, 182, 212, 0.15)');
    gradientCpu.addColorStop(1, 'rgba(6, 182, 212, 0)');
    
    const gradientRam = ctx1.createLinearGradient(0, 0, 0, 180);
    gradientRam.addColorStop(0, 'rgba(139, 92, 246, 0.15)');
    gradientRam.addColorStop(1, 'rgba(139, 92, 246, 0)');
    
    const gradientGpu = ctx1.createLinearGradient(0, 0, 0, 180);
    gradientGpu.addColorStop(0, 'rgba(16, 185, 129, 0.15)');
    gradientGpu.addColorStop(1, 'rgba(16, 185, 129, 0)');

    state.telemetryChart = new Chart(ctx1, {
        type: 'line',
        data: {
            labels: state.chartData.labels,
            datasets: [
                {
                    label: 'CPU',
                    data: state.chartData.cpu,
                    borderColor: '#06b6d4',
                    backgroundColor: gradientCpu,
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 0
                },
                {
                    label: 'RAM',
                    data: state.chartData.ram,
                    borderColor: '#8b5cf6',
                    backgroundColor: gradientRam,
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 0
                },
                {
                    label: 'GPU',
                    data: state.chartData.gpu,
                    borderColor: '#10b981',
                    backgroundColor: gradientGpu,
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 0
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    labels: { color: '#94a3b8', font: { size: 10 } }
                }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255,255,255,0.02)' },
                    ticks: { color: '#64748b', font: { size: 8 } }
                },
                y: {
                    min: 0,
                    max: 100,
                    grid: { color: 'rgba(255,255,255,0.03)' },
                    ticks: { color: '#64748b' }
                }
            }
        }
    });

    // 2. Network speed sub-chart
    const networkCanvas = document.getElementById('networkChart');
    if (!networkCanvas) return;
    const ctx2 = networkCanvas.getContext('2d');
    state.networkChart = new Chart(ctx2, {
        type: 'line',
        data: {
            labels: state.chartData.labels,
            datasets: [
                {
                    label: 'DL',
                    data: state.chartData.netDown,
                    borderColor: '#06b6d4',
                    borderWidth: 1.5,
                    fill: false,
                    tension: 0.3,
                    pointRadius: 0
                },
                {
                    label: 'UL',
                    data: state.chartData.netUp,
                    borderColor: '#8b5cf6',
                    borderWidth: 1.5,
                    fill: false,
                    tension: 0.3,
                    pointRadius: 0
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: { legend: { display: false } },
            scales: {
                x: { display: false },
                y: {
                    grid: { display: false },
                    ticks: { display: false }
                }
            }
        }
    });
}

export function pushChartMetrics(label, cpu, ram, gpu, netDown, netUp) {
    const labels = state.chartData.labels;
    labels.push(label);
    state.chartData.cpu.push(cpu);
    state.chartData.ram.push(ram);
    state.chartData.gpu.push(gpu);
    state.chartData.netDown.push(netDown);
    state.chartData.netUp.push(netUp);

    // Maintain a rolling window of 15 telemetry points
    if (labels.length > 15) {
        labels.shift();
        state.chartData.cpu.shift();
        state.chartData.ram.shift();
        state.chartData.gpu.shift();
        state.chartData.netDown.shift();
        state.chartData.netUp.shift();
    }

    if (state.telemetryChart) state.telemetryChart.update('none');
    if (state.networkChart) state.networkChart.update('none');
}

export function renderBenchmarkHistoryChart(historyData) {
    if (!historyData || historyData.length === 0) return;
    const canvas = document.getElementById('benchmarkHistoryChart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    // Destroy previous Chart instance to prevent duplicates/flickering
    if (state.historyChart) {
        state.historyChart.destroy();
    }

    // Sort database history so that chronological order flows from left to right
    const dataset = [...historyData].reverse();
    
    const labels = dataset.map(d => {
        const date = new Date(d.timestamp);
        return date.toLocaleDateString([], { month: 'short', day: 'numeric' }) + ' ' + 
               date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    });
    
    const cpuScores = dataset.map(d => d.cpu_score / 1000000.0); // Show in Millions of calculation units
    const memSpeeds = dataset.map(d => d.memory_bandwidth_gbs);

    state.historyChart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'CPU Score (Millions)',
                    data: cpuScores,
                    backgroundColor: 'rgba(6, 182, 212, 0.4)',
                    borderColor: '#06b6d4',
                    borderWidth: 1.5,
                    yAxisID: 'y_cpu'
                },
                {
                    label: 'Memory Bandwidth (GB/s)',
                    data: memSpeeds,
                    type: 'line',
                    borderColor: '#8b5cf6',
                    borderWidth: 2.5,
                    tension: 0.3,
                    pointRadius: 4,
                    fill: false,
                    yAxisID: 'y_mem'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    labels: { color: '#94a3b8', font: { size: 10 } }
                }
            },
            scales: {
                x: {
                    grid: { display: false },
                    ticks: { color: '#64748b', font: { size: 8 } }
                },
                y_cpu: {
                    type: 'linear',
                    position: 'left',
                    grid: { color: 'rgba(255,255,255,0.03)' },
                    title: { display: true, text: 'CPU Prime (M)', color: '#06b6d4', font: { size: 10 } },
                    ticks: { color: '#64748b' }
                },
                y_mem: {
                    type: 'linear',
                    position: 'right',
                    grid: { display: false },
                    title: { display: true, text: 'Memory Speed (GB/s)', color: '#8b5cf6', font: { size: 10 } },
                    ticks: { color: '#64748b' }
                }
            }
        }
    });
}
