// Settings module
import { ajaxRequest } from './utils.js';

export function loadSettings() {
    return ajaxRequest('GET', '/api/settings')
        .then(data => {
            if (data && data.gemini_api_key) {
                const keyInput = document.getElementById('settings-key-input');
                if (keyInput) keyInput.value = data.gemini_api_key;
            }
        });
}

export function saveSettings() {
    const keyInput = document.getElementById('settings-key-input');
    const status = document.getElementById('settings-status-txt');
    if (!keyInput || !status) return;

    const key = keyInput.value;

    return ajaxRequest('POST', '/api/settings', { gemini_api_key: key })
        .then(res => {
            if (res && res.status === 'success') {
                status.innerText = "Settings updated successfully!";
                status.className = "status-msg";
                status.classList.remove('hide');
                setTimeout(() => status.classList.add('hide'), 4000);
            } else {
                status.innerText = "Failed to update configuration.";
                status.className = "status-msg resource-hog";
                status.classList.remove('hide');
            }
        })
        .catch(() => {
            status.innerText = "Error completing POST settings API request.";
            status.className = "status-msg resource-hog";
            status.classList.remove('hide');
        });
}
