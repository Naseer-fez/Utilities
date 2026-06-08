// Docker status module
import { ajaxRequest } from './utils.js';

export function fetchDocker() {
    return ajaxRequest('GET', '/api/docker')
        .then(data => {
            if (!data) return;
            
            const offlinePanel = document.getElementById('docker-offline');
            const onlinePanel = document.getElementById('docker-online');
            if (!offlinePanel || !onlinePanel) return;

            if (!data.active) {
                offlinePanel.classList.remove('hide');
                onlinePanel.classList.add('hide');
            } else {
                offlinePanel.classList.add('hide');
                onlinePanel.classList.remove('hide');
                
                const totalText = document.getElementById('docker-total-txt');
                const runningText = document.getElementById('docker-running-txt');
                const stoppedText = document.getElementById('docker-stopped-txt');
                
                if (totalText) totalText.innerText = data.container_count;
                if (runningText) runningText.innerText = data.running_count;
                if (stoppedText) stoppedText.innerText = data.container_count - data.running_count;
                
                const tbody = document.getElementById('docker-tbody');
                if (tbody) {
                    if (data.containers.length === 0) {
                        tbody.innerHTML = `<tr><td colspan="6" class="text-center">No active container records found.</td></tr>`;
                    } else {
                        let html = '';
                        data.containers.forEach(c => {
                            const badgeClass = c.running ? 'online' : '';
                            const statusLabel = c.running ? 'Running' : 'Stopped';
                            const trClass = c.running ? 'resource-active' : '';
                            
                            html += `<tr class="${trClass}">
                                <td><code>${c.id}</code></td>
                                <td><strong>${c.name}</strong></td>
                                <td><code>${c.image}</code></td>
                                <td><span class="status-indicator ${badgeClass}"></span> ${statusLabel}</td>
                                <td>${c.cpu_percent.toFixed(1)}%</td>
                                <td>${c.ram_mb.toFixed(1)} MB</td>
                            </tr>`;
                        });
                        tbody.innerHTML = html;
                    }
                }
            }
        });
}
