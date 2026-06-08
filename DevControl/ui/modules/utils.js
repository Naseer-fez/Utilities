// Network & helper utilities module

const host = window.location.host || '127.0.0.1:5821';
const httpProtocol = window.location.protocol;
const wsProtocol = httpProtocol === 'https:' ? 'wss:' : 'ws:';

export const baseUrl = `${httpProtocol}//${host}`;
export const wsUrl = `${wsProtocol}//${host}/ws`;

export function ajaxRequest(method, endpoint, bodyData = null) {
    const url = `${baseUrl}${endpoint}`;
    const options = {
        method: method,
        headers: {
            'Content-Type': 'application/json'
        }
    };

    if (bodyData) {
        options.body = JSON.stringify(bodyData);
    }

    return fetch(url, options)
        .then(res => {
            if (!res.ok) throw new Error(`HTTP status error: ${res.status}`);
            return res.json();
        })
        .catch(err => {
            console.error(`Fetch API Error [${method} ${endpoint}]:`, err);
            return null;
        });
}

export function escapeHtml(text) {
    if (!text) return '';
    return text
        .toString()
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

export function jsonParseSafely(str) {
    try {
        return JSON.parse(str);
    } catch (e) {
        return null;
    }
}
