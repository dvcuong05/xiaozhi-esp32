/* monitor.js — Serial Monitor tab
 * Depends on: app.js (socket + AppState loaded first)
 */

const monTerm   = document.getElementById('monitor-terminal');
const autoScroll = document.getElementById('monitor-autoscroll');
const filterIn  = document.getElementById('monitor-filter');
const btnStart  = document.getElementById('btn-monitor-start');
const btnStop   = document.getElementById('btn-monitor-stop');
const sendInput = document.getElementById('monitor-send-input');
const btnSend   = document.getElementById('btn-monitor-send');

let logData = [];
let filterText = '';

// ─── Port dropdown ────────────────────────────────────────────────────────────
window.addEventListener('portsUpdated', () => {
    const sel = document.getElementById('monitor-port-select');
    const prev = sel.value;
    sel.innerHTML = '';

    const working = window.AppState.workingPorts;
    if (working.length === 0) {
        sel.innerHTML = '<option value="">No working ports</option>';
    } else {
        working.forEach(p => {
            const opt = document.createElement('option');
            opt.value = p.name;
            opt.textContent = `${p.name} — ${p.description}`;
            sel.appendChild(opt);
        });
        if (prev && sel.querySelector(`option[value="${prev}"]`)) sel.value = prev;
    }
});

// ─── Terminal helpers ─────────────────────────────────────────────────────────
function termAppend(text) {
    logData.push(text);

    // Apply filter
    if (filterText && !text.toLowerCase().includes(filterText)) return;

    const span = document.createElement('span');
    const lower = text.toLowerCase();
    if (lower.includes('error') || lower.includes('err:')) {
        span.style.color = 'var(--color-danger)';
    } else if (lower.includes('warning') || lower.includes('warn')) {
        span.style.color = 'var(--color-used)';
    } else if (lower.includes('[devtool]')) {
        span.style.color = 'var(--color-primary)';
        span.style.fontWeight = '600';
    }
    span.textContent = text;
    monTerm.appendChild(span);

    if (autoScroll.checked) {
        monTerm.scrollTop = monTerm.scrollHeight;
    }
}

function reRender() {
    monTerm.innerHTML = '';
    logData.forEach(line => {
        if (!filterText || line.toLowerCase().includes(filterText)) {
            const span = document.createElement('span');
            const lower = line.toLowerCase();
            if (lower.includes('error') || lower.includes('err:'))
                span.style.color = 'var(--color-danger)';
            else if (lower.includes('warning') || lower.includes('warn'))
                span.style.color = 'var(--color-used)';
            else if (lower.includes('[devtool]')) {
                span.style.color = 'var(--color-primary)';
                span.style.fontWeight = '600';
            }
            span.textContent = line;
            monTerm.appendChild(span);
        }
    });
    if (autoScroll.checked) monTerm.scrollTop = monTerm.scrollHeight;
}

// ─── Connect ──────────────────────────────────────────────────────────────────
btnStart.addEventListener('click', async () => {
    const port = document.getElementById('monitor-port-select').value;
    const baud = document.getElementById('monitor-baud-select').value;
    if (!port) { alert('Select a port first'); return; }

    monTerm.innerHTML = '';
    logData = [];
    btnStart.disabled = true;

    try {
        const res = await fetch('/api/monitor/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ port, baud }),
        });
        const d = await res.json();
        if (d.error) termAppend(`[DevTool] Error: ${d.error}\n`);
    } catch (e) {
        termAppend(`[DevTool] Request failed: ${e}\n`);
        btnStart.disabled = false;
    }
});

// ─── Disconnect ───────────────────────────────────────────────────────────────
btnStop.addEventListener('click', async () => {
    try {
        await fetch('/api/monitor/stop', { method: 'POST' });
    } catch (e) {
        termAppend(`[DevTool] Stop failed: ${e}\n`);
    }
});

// ─── Send input ───────────────────────────────────────────────────────────────
function sendSerial() {
    const text = sendInput.value;
    if (!text) return;
    // Emit via WebSocket (faster than HTTP)
    socket.emit('monitor_send', { data: text + '\n' });
    termAppend(`>> ${text}\n`);
    sendInput.value = '';
}

btnSend.addEventListener('click', sendSerial);
sendInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') sendSerial();
});

// ─── Clear / Download ─────────────────────────────────────────────────────────
document.getElementById('btn-monitor-clear').addEventListener('click', () => {
    monTerm.innerHTML = '';
    logData = [];
});

document.getElementById('btn-monitor-download').addEventListener('click', () => {
    const blob = new Blob([logData.join('')], { type: 'text/plain' });
    const url  = URL.createObjectURL(blob);
    const a    = Object.assign(document.createElement('a'), {
        href: url,
        download: `serial-log-${new Date().toISOString().replace(/[:.]/g, '-')}.txt`,
    });
    a.click();
    URL.revokeObjectURL(url);
});

// ─── Filter ───────────────────────────────────────────────────────────────────
filterIn.addEventListener('input', () => {
    filterText = filterIn.value.toLowerCase();
    reRender();
});

// ─── Socket events ────────────────────────────────────────────────────────────
socket.on('monitor_data', (msg) => {
    termAppend(msg.data);
});

socket.on('monitor_status', (msg) => {
    if (msg.connected) {
        btnStart.disabled = true;
        btnStop.disabled  = false;
        sendInput.disabled = false;
        btnSend.disabled   = false;
    } else {
        btnStart.disabled = false;
        btnStop.disabled  = true;
        sendInput.disabled = true;
        btnSend.disabled   = true;
    }
});
