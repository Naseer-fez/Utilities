// Processes module
import { state } from './state.js';
import { ajaxRequest, escapeHtml } from './utils.js';

export function fetchProcesses() {
    return ajaxRequest('GET', '/api/processes')
        .then(data => {
            if (data) {
                state.processes = data;
                renderProcessTable();
            }
        });
}

export function renderProcessTable() {
    const tbody = document.getElementById('proc-tbody');
    if (!tbody) return;

    const searchInput = document.getElementById('proc-search');
    const filterVal = searchInput ? searchInput.value.toLowerCase() : '';
    
    let filtered = state.processes;
    if (filterVal) {
        filtered = state.processes.filter(p => 
            p.name.toLowerCase().includes(filterVal) || 
            p.cmdline.toLowerCase().includes(filterVal) ||
            p.pid.toString().includes(filterVal)
        );
    }

    if (filtered.length === 0) {
        tbody.innerHTML = `<tr><td colspan="5" class="text-center">No active process allocations matching "${filterVal}"</td></tr>`;
        return;
    }

    let html = '';
    filtered.forEach(p => {
        const isPy = p.name.toLowerCase().includes('python') || p.cmdline.toLowerCase().includes('python');
        const rowClass = isPy ? 'resource-active' : '';
        const cpuClass = p.cpu_percent > 10.0 ? 'resource-hog' : '';
        const ramClass = p.memory_mb > 400.0 ? 'resource-hog' : '';
        
        // Safe escape of cmdline
        const cmdSafe = escapeHtml(p.cmdline);
        
        html += `<tr class="${rowClass}">
            <td>${p.pid}</td>
            <td><strong>${p.name}</strong></td>
            <td class="${cpuClass}">${p.cpu_percent.toFixed(1)}%</td>
            <td class="${ramClass}">${p.memory_mb.toFixed(1)} MB</td>
            <td class="text-truncate" title="${cmdSafe}">${cmdSafe}</td>
        </tr>`;
    });
    tbody.innerHTML = html;
}
