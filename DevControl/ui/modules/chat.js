// AI Chat diagnostics module
import { ajaxRequest, escapeHtml } from './utils.js';

export function submitAIChatQuery(query) {
    const chatInput = document.getElementById('chat-input');
    if (chatInput) {
        chatInput.value = query;
        sendUserChatMsg();
    }
}

export function sendUserChatMsg() {
    const input = document.getElementById('chat-input');
    if (!input) return;
    const text = input.value.trim();
    if (!text) return;

    // Clear input bar
    input.value = '';

    // Add user text bubble
    appendChatMsg(text, 'user');
    
    // Append typing indicators
    const loaderId = appendChatTypingIndicator();

    // Send query to FastAPI AI Engine
    ajaxRequest('POST', '/api/chat', { prompt: text })
        .then(data => {
            removeChatTypingIndicator(loaderId);
            if (data && data.response) {
                appendChatMsg(data.response, 'system');
            } else {
                appendChatMsg("⚠️ **Diagnostics Error:** Encountered an issue compiling the heuristic diagnostic systems.", 'system');
            }
        })
        .catch(err => {
            removeChatTypingIndicator(loaderId);
            appendChatMsg(`⚠️ **Communications Error:** Web server could not complete AI query diagnostics. details: ${err}`, 'system');
        });
}

export function appendChatMsg(text, sender) {
    const container = document.getElementById('chat-messages-container');
    if (!container) return;

    const msgDiv = document.createElement('div');
    msgDiv.className = `message ${sender}-msg`;

    const icon = sender === 'user' ? 'fa-user' : 'fa-brain';
    const label = sender === 'user' ? 'User Terminal' : 'AetherMonitor AI Diagnostic';

    // Parse custom markdown layout inside JS
    const parsedHtml = parseMarkdownToHtml(text);

    msgDiv.innerHTML = `<div class="msg-header">
        <i class="fa-solid ${icon}"></i> <span>${label}</span>
    </div>
    <div class="msg-content">
        ${parsedHtml}
    </div>`;

    container.appendChild(msgDiv);
    container.scrollTop = container.scrollHeight;
}

function appendChatTypingIndicator() {
    const container = document.getElementById('chat-messages-container');
    if (!container) return null;

    const loaderId = 'loader_' + Date.now();
    const loaderDiv = document.createElement('div');
    loaderDiv.id = loaderId;
    loaderDiv.className = 'message system-msg typing-msg';
    loaderDiv.innerHTML = `<div class="msg-header">
        <i class="fa-solid fa-brain"></i> <span>AetherMonitor AI Diagnostic</span>
    </div>
    <div class="msg-content">
        <div class="typing-dots">
            <span></span><span></span><span></span>
        </div>
    </div>`;

    container.appendChild(loaderDiv);
    container.scrollTop = container.scrollHeight;
    return loaderId;
}

function removeChatTypingIndicator(loaderId) {
    if (!loaderId) return;
    const el = document.getElementById(loaderId);
    if (el) el.remove();
}

function parseMarkdownToHtml(text) {
    // Escapes code characters but preserves markdown formatting safely
    let html = escapeHtml(text);

    // Headers formatting
    html = html.replace(/^###\s+(.*)$/gm, '<h3>$1</h3>');
    html = html.replace(/^##\s+(.*)$/gm, '<h2>$1</h2>');
    html = html.replace(/^#\s+(.*)$/gm, '<h1>$1</h1>');

    // Bold markdown formatting
    html = html.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>');

    // Inline code formatting
    html = html.replace(/\`(.*?)\`/g, '<code>$1</code>');

    // Tables parsing (custom formatting for diagnostic reports)
    const lines = html.split('\n');
    const outputLines = [];
    let tableRows = [];
    let inTable = false;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim();
        if (line.startsWith('|')) {
            if (!inTable) {
                inTable = true;
                tableRows = [];
            }
            // Parse row cells
            const cells = line.split('|').slice(1, -1).map(c => c.trim());
            // Ignore separator rows (e.g. :---)
            if (cells.length > 0 && !cells[0].startsWith(':') && !cells[0].startsWith('-')) {
                tableRows.push(cells);
            }
        } else {
            if (inTable) {
                inTable = false;
                outputLines.push(renderHtmlTable(tableRows));
            }
            outputLines.push(lines[i]);
        }
    }
    if (inTable) {
        outputLines.push(renderHtmlTable(tableRows));
    }

    // Unordered list formatting
    const processedLines = [];
    let inList = false;
    for (let i = 0; i < outputLines.length; i++) {
        const line = outputLines[i];
        const listMatch = line.match(/^\s*[-*+]\s+(.*)$/);
        if (listMatch) {
            const itemContent = listMatch[1];
            if (!inList) {
                processedLines.push('<ul>');
                inList = true;
            }
            processedLines.push(`<li>${itemContent}</li>`);
        } else {
            if (inList) {
                processedLines.push('</ul>');
                inList = false;
            }
            processedLines.push(line);
        }
    }
    if (inList) {
        processedLines.push('</ul>');
    }

    // Combine back and replace double newlines with paragraph structures
    let finalHtml = processedLines.join('\n');
    finalHtml = finalHtml.replace(/\n\n/g, '</p><p>');
    finalHtml = `<p>${finalHtml}</p>`;

    // Remove empty paragraphs
    finalHtml = finalHtml.replace(/<p><\/p>/g, '');
    finalHtml = finalHtml.replace(/<p><ul/g, '<ul');
    finalHtml = finalHtml.replace(/ul><\/p>/g, 'ul>');
    
    return finalHtml;
}

export function renderHtmlTable(rows) {
    if (!rows || rows.length === 0) return '';
    
    let html = '<table><thead><tr>';
    
    // First row forms headers
    const headers = rows[0];
    headers.forEach(h => {
        html += `<th>${h}</th>`;
    });
    html += '</tr></thead><tbody>';
    
    // Subsequent rows form body
    for (let i = 1; i < rows.length; i++) {
        html += '<tr>';
        rows[i].forEach(cell => {
            html += `<td>${cell}</td>`;
        });
        html += '</tr>';
    }
    
    html += '</tbody></table>';
    return html;
}