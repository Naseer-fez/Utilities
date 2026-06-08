// APIs & latency checker module
import { ajaxRequest } from './utils.js';

export function fetchAPIs() {
    return ajaxRequest('GET', '/api/apis')
        .then(data => {
            if (!data) return;
            const container = document.getElementById('api-list-container');
            if (!container) return;

            let html = '';
            data.forEach(api => {
                const isOk = api.status === 'OK';
                const badgeClass = isOk ? 'online' : '';
                const statusText = isOk ? 'Operational' : 'Failed';
                const latencyColor = api.latency_ms > 400 ? 'resource-hog' : 'resource-active';
                
                html += `<div class="api-item">
                    <div class="api-item-info">
                        <span class="api-item-name">${api.name}</span>
                        <span class="api-item-url">${api.url}</span>
                    </div>
                    <div class="api-item-metrics">
                        <span class="api-item-latency ${latencyColor}">${api.latency_ms.toFixed(1)} ms</span>
                        <span class="status-indicator ${badgeClass}"></span> <span class="api-item-status-txt">${statusText}</span>
                    </div>
                </div>`;
            });
            container.innerHTML = html;
        });
}
