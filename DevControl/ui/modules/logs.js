// Logs module
import { state } from './state.js';
import { ajaxRequest, escapeHtml } from './utils.js';

export function fetchLogs() {
    return ajaxRequest('GET', '/api/logs')
        .then(data => {
            if (data) {
                state.logs = data;
                renderLogsFeed();
            }
        });
}

export function renderLogsFeed() {
    const container = document.getElementById('logs-stream');
    if (!container) return;

    let filtered = state.logs;
    if (state.logFilter !== 'ALL') {
        filtered = state.logs.filter(l => l.level === state.logFilter);
    }

    if (filtered.length === 0) {
        container.innerHTML = `<div class="text-center padding-all">No system log alerts matching ${state.logFilter} level.</div>`;
        return;
    }

    let html = '';
    filtered.forEach(l => {
        html += `<div class="log-entry log-level-${l.level}">
            <div class="log-entry-meta">
                <span>${l.timestamp}</span>
                <span>SOURCE: ${l.source}</span>
                <span>SEVERITY: ${l.level}</span>
            </div>
            <div class="log-entry-msg">${escapeHtml(l.message)}</div>
        </div>`;
    });
    container.innerHTML = html;
}
