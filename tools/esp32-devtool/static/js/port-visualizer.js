/* port-visualizer.js
 * Renders the port grid and updates it via WebSocket 'port_update' events.
 * Depends on: AppState and socket initialised in app.js (loaded first).
 */

let currentFilter = 'all';

// ─── Socket event ───────────────────────────────────────────────────────────
socket.on('port_update', (ports) => {
    window.AppState.ports = ports;
    window.AppState.workingPorts = ports.filter(p => p.status === 'working');

    renderPorts();
    updateStats();

    // Let flasher.js and monitor.js repopulate their port dropdowns
    window.dispatchEvent(new Event('portsUpdated'));
});

// ─── Filter buttons ─────────────────────────────────────────────────────────
document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
        document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
        e.currentTarget.classList.add('active');
        currentFilter = e.currentTarget.getAttribute('data-filter');
        renderPorts();
    });
});

// ─── Render ──────────────────────────────────────────────────────────────────
function renderPorts() {
    const grid = document.getElementById('port-grid');
    const filtered = window.AppState.ports.filter(p =>
        currentFilter === 'all' || p.status === currentFilter
    );

    if (filtered.length === 0) {
        grid.innerHTML = `<div class="empty-state">
            <p>${currentFilter === 'all' ? '🔍 No serial ports detected.' : `No <strong>${currentFilter}</strong> ports.`}</p>
        </div>`;
        return;
    }

    grid.innerHTML = '';
    filtered.forEach(port => {
        const card = buildCard(port);
        grid.appendChild(card);
    });
}

function buildCard(port) {
    const card = document.createElement('div');
    card.className = `port-card status-${port.status}`;

    // VID:PID display
    const vidPid = (port.vid != null && port.pid != null)
        ? `${port.vid.toString(16).padStart(4, '0')}:${port.pid.toString(16).padStart(4, '0')}`.toUpperCase()
        : 'N/A';

    const espBadge = port.is_esp32 ? ' <span title="ESP32 device detected" style="font-size:1rem">⚡</span>' : '';

    // Action buttons — only for physically present ports
    const actions = port.status === 'working'
        ? `<button class="btn btn-primary" onclick="openMonitor('${port.name}')">Monitor</button>
           <button class="btn" onclick="openFlasher('${port.name}')">Flash</button>`
        : '';

    // Files referencing this port
    const refHtml = port.referenced_in && port.referenced_in.length > 0
        ? `<div class="port-detail-row"><span>Used in:</span><span title="${port.referenced_in.join('\n')}">${port.referenced_in.length} file(s)</span></div>`
        : '';

    card.innerHTML = `
        <div class="port-header">
            <div class="port-name">${escapeHtml(port.name)}${espBadge}</div>
            <div class="badge ${port.status}">${port.status}</div>
        </div>
        <div class="port-details">
            <div>${escapeHtml(port.description)}</div>
            <div class="port-detail-row">
                <span>Manufacturer</span>
                <span>${escapeHtml(port.manufacturer || 'N/A')}</span>
            </div>
            <div class="port-detail-row">
                <span>VID:PID</span>
                <span>${vidPid}</span>
            </div>
            ${port.serial_number ? `<div class="port-detail-row"><span>S/N</span><span>${escapeHtml(port.serial_number)}</span></div>` : ''}
            ${refHtml}
        </div>
        ${actions ? `<div class="port-actions">${actions}</div>` : ''}
    `;

    return card;
}

// ─── Stats bar ───────────────────────────────────────────────────────────────
function updateStats() {
    let w = 0, u = 0, r = 0;
    window.AppState.ports.forEach(p => {
        if (p.status === 'working')       w++;
        else if (p.status === 'used')     u++;
        else if (p.status === 'reserved') r++;
    });
    document.getElementById('port-stats').innerHTML =
        `<span style="color:var(--color-working)">${w} working</span>, ` +
        `<span style="color:var(--color-used)">${u} used</span>, ` +
        `<span style="color:var(--color-reserved)">${r} reserved</span>`;
}

// ─── Navigation helpers ───────────────────────────────────────────────────────
window.openMonitor = function(portName) {
    document.querySelector('.nav-btn[data-target="monitor-tab"]').click();
    setTimeout(() => {
        const sel = document.getElementById('monitor-port-select');
        if (sel.querySelector(`option[value="${portName}"]`)) sel.value = portName;
        document.getElementById('btn-monitor-start').click();
    }, 120);
};

window.openFlasher = function(portName) {
    document.querySelector('.nav-btn[data-target="flash-tab"]').click();
    setTimeout(() => {
        const sel = document.getElementById('flash-port-select');
        if (sel.querySelector(`option[value="${portName}"]`)) sel.value = portName;
    }, 120);
};

// ─── Utility ──────────────────────────────────────────────────────────────────
function escapeHtml(str) {
    if (!str) return '';
    return str
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}
