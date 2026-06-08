/* -------------------------------------------------------------
 * AETHERMONITOR CLIENT BOOTSTRAPPER (MODULAR ES MODULES ENTRY)
 * ------------------------------------------------------------- */

import { state } from './modules/state.js';
import { initCharts } from './modules/charts.js';
import { connectWebSocket } from './modules/telemetry.js';
import { loadSettings, saveSettings } from './modules/settings.js';
import { startTabFetchers, switchTab } from './modules/tabs.js';
import { sendUserChatMsg, submitAIChatQuery } from './modules/chat.js';
import { executeBenchmark } from './modules/benchmarks.js';

document.addEventListener('DOMContentLoaded', () => {
    // 1. Initialize empty Chart.js canvases on DOM loads
    initCharts();
    
    // 2. Fetch and load API configurations
    loadSettings();
    
    // 3. Setup Navigation routing links
    document.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const tab = link.getAttribute('data-tab');
            switchTab(tab);
        });
    });
    
    // 4. Connect client to real-time WebSockets telemetry
    connectWebSocket();
    
    // 5. Fire background recurring fetchers for non-active tab content
    startTabFetchers();

    // 6. Connect Chat controls
    const chatInput = document.getElementById('chat-input');
    if (chatInput) {
        chatInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                sendUserChatMsg();
            }
        });
    }

    const chatSendBtn = document.getElementById('chat-send-btn');
    if (chatSendBtn) {
        chatSendBtn.addEventListener('click', () => {
            sendUserChatMsg();
        });
    }

    // Connect Chat suggestions trigger shortcuts
    document.querySelectorAll('.chat-shortcut-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const query = btn.getAttribute('data-query');
            submitAIChatQuery(query);
        });
    });

    // 7. Connect Settings action handlers
    const saveSettingsBtn = document.getElementById('save-settings-btn');
    if (saveSettingsBtn) {
        saveSettingsBtn.addEventListener('click', () => {
            saveSettings();
        });
    }

    // 8. Connect Benchmark triggering action
    const runBenchBtn = document.getElementById('run-bench-btn');
    if (runBenchBtn) {
        runBenchBtn.addEventListener('click', () => {
            executeBenchmark();
        });
    }

    // 9. Connect live process search filter
    const procSearch = document.getElementById('proc-search');
    if (procSearch) {
        procSearch.addEventListener('input', () => {
            import('./modules/processes.js').then(mod => {
                mod.renderProcessTable();
            });
        });
    }
    
    // 10. Connect Event logs filter buttons
    document.querySelectorAll('.log-filter-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.log-filter-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            state.logFilter = btn.getAttribute('data-filter');
            import('./modules/logs.js').then(mod => {
                mod.renderLogsFeed();
            });
        });
    });
});