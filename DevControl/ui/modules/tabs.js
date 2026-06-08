// Tab Management module
import { state } from './state.js';
import { fetchProcesses } from './processes.js';
import { fetchDocker } from './docker.js';
import { fetchAPIs } from './apis.js';
import { fetchBenchmarkHistory } from './benchmarks.js';
import { fetchLogs } from './logs.js';

export function switchTab(tabId) {
    state.activeTab = tabId;
    
    // Update nav links active status
    document.querySelectorAll('.nav-link').forEach(link => {
        if (link.getAttribute('data-tab') === tabId) {
            link.classList.add('active');
        } else {
            link.classList.remove('active');
        }
    });

    // Show/hide panes
    document.querySelectorAll('.tab-pane').forEach(pane => {
        if (pane.id === `tab-${tabId}`) {
            pane.classList.add('active');
        } else {
            pane.classList.remove('active');
        }
    });

    // Trigger individual tab loads immediately
    if (tabId === 'processes') fetchProcesses();
    if (tabId === 'docker') fetchDocker();
    if (tabId === 'apis') fetchAPIs();
    if (tabId === 'benchmarks') fetchBenchmarkHistory();
    if (tabId === 'logs') fetchLogs();
}

export function startTabFetchers() {
    // Fetch background metrics periodically depending on the active tab
    setInterval(() => {
        if (state.activeTab === 'processes') fetchProcesses();
        if (state.activeTab === 'docker') fetchDocker();
        if (state.activeTab === 'apis') fetchAPIs();
        if (state.activeTab === 'logs') fetchLogs();
    }, 3000); // 3 seconds refresh rate
}
