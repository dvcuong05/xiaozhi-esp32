/* flasher.js — Firmware Flash tab
 * Depends on: app.js (socket + AppState loaded first)
 */

const flashTerm = document.getElementById('flash-terminal');
const btnFlash  = document.getElementById('btn-flash');
const btnErase  = document.getElementById('btn-erase');
const btnInfo   = document.getElementById('btn-chip-info');

// ─── Terminal helpers ─────────────────────────────────────────────────────────
function termClear(msg) {
    flashTerm.innerHTML = '';
    if (msg) termLine(msg, 'info');
}

function termLine(text, type) {
    const span = document.createElement('span');
    span.textContent = text;
    if (type === 'error')   span.style.color = 'var(--color-danger)';
    if (type === 'success') span.style.color = 'var(--color-working)';
    if (type === 'info')    { span.style.color = 'var(--color-primary)'; span.style.fontWeight = '600'; }
    flashTerm.appendChild(span);
    flashTerm.scrollTop = flashTerm.scrollHeight;
}

// ─── Port dropdown ────────────────────────────────────────────────────────────
window.addEventListener('portsUpdated', () => {
    const sel  = document.getElementById('flash-port-select');
    const prev = sel.value;
    sel.innerHTML = '';

    const working = [...window.AppState.workingPorts]
        .sort((a, b) => (b.is_esp32 ? 1 : 0) - (a.is_esp32 ? 1 : 0));

    if (working.length === 0) {
        sel.innerHTML = '<option value="">No working ports found</option>';
    } else {
        working.forEach(p => {
            const opt = document.createElement('option');
            opt.value = p.name;
            opt.textContent = `${p.name}${p.is_esp32 ? ' [ESP32]' : ''} — ${p.description}`;
            sel.appendChild(opt);
        });
        if (prev && sel.querySelector(`option[value="${prev}"]`)) sel.value = prev;
    }
});

// ─── Verify esptool on load ───────────────────────────────────────────────────
async function checkEsptool() {
    try {
        const res  = await fetch('/api/flash/verify');
        const data = await res.json();
        if (data.ok) {
            termLine(`[DevTool] esptool ready: ${data.output.split('\n')[0]}\n`, 'success');
        } else {
            termLine(`[DevTool] WARNING: esptool not found!\n`, 'error');
            termLine(`          Python: ${data.python}\n`, 'error');
            termLine(`          Run: pip install esptool\n`, 'error');
        }
    } catch (e) {
        termLine(`[DevTool] Could not verify esptool: ${e}\n`, 'info');
    }
}
checkEsptool();

// ─── Browse build/ ────────────────────────────────────────────────────────────
document.getElementById('btn-browse-build').addEventListener('click', async () => {
    try {
        const res  = await fetch('/api/flash/list-bins');
        const data = await res.json();
        if (!data.bins || data.bins.length === 0) {
            alert(`No .bin files found in:\n${data.build_dir}`);
            return;
        }
        const opts = data.bins.map((b, i) => `${i + 1}. ${b}`).join('\n');
        const input = prompt(`Select binary from build/:\n\n${opts}\n\nEnter number:`);
        if (!input) return;
        const idx = parseInt(input, 10);
        if (!isNaN(idx) && idx >= 1 && idx <= data.bins.length) {
            document.getElementById('flash-firmware-input').value = data.bins[idx - 1];
        }
    } catch (e) {
        termLine(`[DevTool] Browse error: ${e}\n`, 'error');
    }
});

// ─── Helpers to lock/unlock buttons during operation ────────────────────────
function setFlashing(active) {
    btnFlash.disabled = active;
    btnErase.disabled = active;
    btnInfo.disabled  = active;
    btnFlash.textContent = active ? 'Flashing...' : 'Start Flash';
}

// ─── Flash ────────────────────────────────────────────────────────────────────
btnFlash.addEventListener('click', async () => {
    const port     = document.getElementById('flash-port-select').value;
    const chip     = document.getElementById('flash-chip-select').value;
    const baud     = document.getElementById('flash-baud-select').value;
    const firmware = document.getElementById('flash-firmware-input').value.trim();

    if (!port)     { alert('Select a port first');           return; }
    if (!firmware) { alert('Specify the firmware .bin path'); return; }

    termClear(`[DevTool] Starting flash...\n[DevTool] Port: ${port}  Chip: ${chip}  Baud: ${baud}\n[DevTool] File: ${firmware}\n\n`);
    setFlashing(true);

    try {
        const res  = await fetch('/api/flash/', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ port, chip, baud, firmware }),
        });
        const data = await res.json();
        if (data.error) termLine(`\n[DevTool] Error: ${data.error}\n`, 'error');
        // Output streams via 'flash_progress' WebSocket event — re-enable on 'finished' signal
    } catch (e) {
        termLine(`[DevTool] Request failed: ${e}\n`, 'error');
        setFlashing(false);
    }
});

// ─── Erase ────────────────────────────────────────────────────────────────────
btnErase.addEventListener('click', async () => {
    const port = document.getElementById('flash-port-select').value;
    const chip = document.getElementById('flash-chip-select').value;
    if (!port) { alert('Select a port first'); return; }
    if (!confirm(`Erase ALL flash on ${port} (${chip})?\n\nThis cannot be undone.`)) return;

    termClear(`[DevTool] Erasing flash on ${port} (${chip})...\n\n`);
    setFlashing(true);

    try {
        const res  = await fetch('/api/flash/erase', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ port, chip }),
        });
        const data = await res.json();
        if (data.error) termLine(`[DevTool] Error: ${data.error}\n`, 'error');
    } catch (e) {
        termLine(`[DevTool] Request failed: ${e}\n`, 'error');
        setFlashing(false);
    }
});

// ─── Chip info ────────────────────────────────────────────────────────────────
btnInfo.addEventListener('click', async () => {
    const port = document.getElementById('flash-port-select').value;
    if (!port) { alert('Select a port first'); return; }

    termClear(`[DevTool] Reading chip info from ${port}...\n\n`);
    btnInfo.disabled = true;
    btnInfo.textContent = 'Reading...';

    try {
        const res  = await fetch('/api/flash/info', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ port }),
        });
        const data = await res.json();
        const out  = data.output || data.error || 'No output.';
        out.split('\n').forEach(line => {
            flashTerm.appendChild(document.createTextNode(line + '\n'));
        });
        flashTerm.scrollTop = flashTerm.scrollHeight;
    } catch (e) {
        termLine(`[DevTool] Request failed: ${e}\n`, 'error');
    } finally {
        btnInfo.disabled = false;
        btnInfo.textContent = 'Chip Info';
    }
});

// ─── WebSocket: real-time flash progress ──────────────────────────────────────
socket.on('flash_progress', (msg) => {
    const line = msg.data || '';
    flashTerm.appendChild(document.createTextNode(line));
    flashTerm.scrollTop = flashTerm.scrollHeight;

    // Re-enable buttons when process finishes
    if (line.includes('[DevTool] Process finished:')) {
        setFlashing(false);
        if (line.includes('OK')) {
            termLine('\n[DevTool] Flash successful!\n', 'success');
        } else {
            termLine('\n[DevTool] Flash failed. Check output above.\n', 'error');
        }
    }
});
