/**
 * pin-diagram.js — ESP32 Interactive Chip Pin Inspector & Hardware Diagnostic
 *
 * Visualizes the ESP32 chip/module with pins color-coded:
 *   - 🟢 working : declared in code AND chip connected
 *   - 🟡 used    : declared in code BUT chip not connected
 *   - ⚫ unused  : unassigned GPIO
 *   - 🔴 power   : 3V3 / 5V
 *   - ⚪ gnd     : Ground
 *
 * Phase 1 & 2 Extensions:
 *   - Live Boot & Runtime Telemetry detection (Display, Audio, Camera, Buttons, PMU)
 *   - Hardware Verified pulsing badges for responding peripherals
 *   - Warning indicator for declared peripherals that failed to respond (I2C NACK)
 *   - 1-Click "Reboot & Probe Hardware"
 *   - Deep Pin Diagnostic Modal
 */

(() => {
    let pinData = null;
    let selectedPin = null;
    let currentFilter = 'all';
    let searchQuery = '';

    // Elements
    const boardSelect   = document.getElementById('pins-board-select');
    const chipSelect    = document.getElementById('pins-chip-select');
    const btnRefresh    = document.getElementById('btn-pins-refresh');
    const btnProbeBoot  = document.getElementById('btn-pins-probe-boot');
    const btnDiagModal  = document.getElementById('btn-pins-diag-modal');
    const container     = document.getElementById('pins-diagram-container');
    const searchInput   = document.getElementById('pins-search-input');
    const filterBtns    = document.querySelectorAll('.pin-filter-btn');

    // Stats
    const statWorking   = document.getElementById('stat-working-count');
    const statVerified  = document.getElementById('stat-verified-count');
    const statUsed      = document.getElementById('stat-used-count');
    const statUnused    = document.getElementById('stat-unused-count');
    const chipConnBadge = document.getElementById('pins-chip-connected-status');

    // Telemetry HUD
    const hudPills      = document.getElementById('hud-pills');

    // Sidebar
    const sidebarTitle  = document.getElementById('sidebar-pin-name');
    const sidebarBadge  = document.getElementById('sidebar-pin-badge');
    const sidebarBody   = document.getElementById('sidebar-pin-body');

    // Diagnostic Modal
    const diagModal     = document.getElementById('diag-modal');
    const btnDiagClose  = document.getElementById('btn-diag-close');
    const btnDiagProbe  = document.getElementById('btn-diag-run-probe');
    const btnDiagBuild  = document.getElementById('btn-diag-compile');
    const diagLog       = document.getElementById('diag-log');
    const diagLogOut    = document.getElementById('diag-log-output');

    // ── 1. Initialization ──────────────────────────────────────────────────
    async function init() {
        await loadBoards();
        await fetchPinStatus();
        setupListeners();
    }

    async function loadBoards() {
        try {
            const res = await fetch('/api/pins/boards');
            const boards = await res.json();
            boardSelect.innerHTML = '<option value="">-- All Project Boards --</option>';

            boards.forEach(b => {
                const opt = document.createElement('option');
                opt.value = b.id;
                opt.dataset.chip = b.chip;
                opt.textContent = `${b.label} (${b.chip.toUpperCase()})`;
                boardSelect.appendChild(opt);
            });

            // Default preference: bread-compact-wifi or bread-compact-esp32
            const defaultBoard = boards.find(b => b.label === 'bread-compact-esp32') ||
                                 boards.find(b => b.label.includes('bread-compact-wifi'));
            if (defaultBoard) {
                boardSelect.value = defaultBoard.id;
                chipSelect.value = defaultBoard.chip || 'esp32';
            }
        } catch (e) {
            console.error('[Pins] Error loading boards:', e);
        }
    }

    function setupListeners() {
        boardSelect.addEventListener('change', () => {
            const opt = boardSelect.selectedOptions[0];
            if (opt && opt.dataset.chip) {
                const detectedChip = opt.dataset.chip.toLowerCase();
                if (chipSelect.querySelector(`option[value="${detectedChip}"]`)) {
                    chipSelect.value = detectedChip;
                }
            }
            fetchPinStatus();
        });

        chipSelect.addEventListener('change', () => fetchPinStatus());
        btnRefresh.addEventListener('click', () => fetchPinStatus());

        // Probe Boot Button
        if (btnProbeBoot) {
            btnProbeBoot.addEventListener('click', runProbeBoot);
        }

        // Diagnostic Modal Controls
        if (btnDiagModal) {
            btnDiagModal.addEventListener('click', () => {
                const modalChipSelect = document.getElementById('diag-modal-chip-select');
                if (modalChipSelect && chipSelect.value) {
                    modalChipSelect.value = chipSelect.value;
                }
                diagModal.style.display = 'flex';
            });
        }
        if (btnDiagClose) {
            btnDiagClose.addEventListener('click', () => diagModal.style.display = 'none');
        }
        if (btnDiagProbe) {
            btnDiagProbe.addEventListener('click', async () => {
                diagModal.style.display = 'none';
                await runProbeBoot();
            });
        }
        if (btnDiagBuild) {
            btnDiagBuild.addEventListener('click', runBuildDiagnostic);
        }

        // Filter Buttons
        filterBtns.forEach(btn => {
            btn.addEventListener('click', () => {
                filterBtns.forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                currentFilter = btn.dataset.filter;
                applyFilters();
            });
        });

        searchInput.addEventListener('input', (e) => {
            searchQuery = e.target.value.trim().toLowerCase();
            applyFilters();
        });

        // Live WebSocket Telemetry Events
        socket.on('hardware_telemetry', (data) => {
            if (pinData) {
                pinData.telemetry = data;
                updateTelemetryHUD(data);
                // Refresh status silently to apply new pin indicators
                fetchPinStatus(true);
            }
        });

        socket.on('port_update', () => {
            const pinsTab = document.getElementById('pins-tab');
            if (pinsTab && pinsTab.classList.contains('active')) {
                fetchPinStatus(true);
            }
        });
    }

    // ── 2. Data Fetching ───────────────────────────────────────────────────
    async function fetchPinStatus(silent = false) {
        const chip = chipSelect.value || 'esp32';
        const board = boardSelect.value || '';

        if (!silent) {
            container.innerHTML = `<div class="pins-loading"><div class="spinner"></div><p>Analyzing GPIO usage & live hardware for ${chip.toUpperCase()}...</p></div>`;
        }

        try {
            const res = await fetch('/api/pins/status', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ chip, board })
            });

            if (!res.ok) {
                container.innerHTML = `<div class="empty-state"><p>❌ Error loading chip data for ${chip}</p></div>`;
                return;
            }

            pinData = await res.json();
            updateStatsBar();
            updateTelemetryHUD(pinData.telemetry);
            renderDiagram();

            // Auto-select detected board from live telemetry if available and user is on "All"
            if (pinData.telemetry && pinData.telemetry.detected_board && !boardSelect.value) {
                const match = Array.from(boardSelect.options).find(o => o.value.includes(pinData.telemetry.detected_board));
                if (match) {
                    boardSelect.value = match.value;
                    chipSelect.value = match.dataset.chip || chipSelect.value;
                }
            }

            // Keep selected pin active
            if (selectedPin) {
                const refreshed = pinData.pins.find(p => p.name === selectedPin.name);
                if (refreshed) {
                    selectPin(refreshed);
                } else {
                    resetSidebar();
                }
            }
        } catch (e) {
            container.innerHTML = `<div class="empty-state"><p>❌ Failed to connect: ${e}</p></div>`;
        }
    }

    function updateStatsBar() {
        if (!pinData) return;
        statWorking.textContent  = pinData.stats.working;
        if (statVerified) statVerified.textContent = pinData.stats.hardware_verified || 0;
        statUsed.textContent     = pinData.stats.used;
        statUnused.textContent   = pinData.stats.unused;

        if (pinData.chip_connected) {
            const portsStr = (pinData.connected_ports || []).join(', ') || 'Connected';
            chipConnBadge.className = 'pins-chip-status connected';
            chipConnBadge.innerHTML = `⚡ ${portsStr} Online`;
        } else {
            chipConnBadge.className = 'pins-chip-status disconnected';
            chipConnBadge.innerHTML = '⚠️ No Device Connected';
        }
    }

    function updateTelemetryHUD(telemetry) {
        if (!telemetry || !hudPills) return;

        const periphs = telemetry.peripherals || [];
        if (periphs.length === 0) {
            hudPills.innerHTML = '<span class="hud-empty">No hardware boot telemetry captured yet. Click "Probe Live Hardware" above.</span>';
            return;
        }

        let html = '';
        if (telemetry.detected_board) {
            html += `<span class="hud-pill pill-board">&#129513; SKU: ${escapeHtml(telemetry.detected_board)} ${telemetry.app_version ? `(v${telemetry.app_version})` : ''}</span>`;
        }

        periphs.forEach(p => {
            let cls = 'pill-confirmed';
            let icon = '✓';
            if (p.status === 'failed') {
                cls = 'pill-failed';
                icon = '⚠️';
            } else if (p.status === 'active') {
                cls = 'pill-active';
                icon = '⚡';
            }
            const pinsLabel = p.pins && p.pins.length ? ` (${p.pins.map(g => 'G' + g).join(',')})` : '';
            html += `<span class="hud-pill ${cls}" title="${escapeHtml(p.detail || '')}">${icon} ${escapeHtml(p.name)}${pinsLabel}</span>`;
        });

        hudPills.innerHTML = html;
    }

    // ── 3. Probe Live Hardware & Diagnostic Actions ─────────────────────────
    async function runProbeBoot() {
        btnProbeBoot.disabled = true;
        const originalText = btnProbeBoot.innerHTML;
        btnProbeBoot.innerHTML = '⏳ Resetting & Probing...';

        container.innerHTML = `
            <div class="pins-loading">
                <div class="spinner"></div>
                <p>Pulsing DTR/RTS to reboot chip...</p>
                <p style="font-size:0.8rem;color:var(--text-muted)">Capturing live ESP-IDF boot logs to verify physical peripherals (I2C display, audio, camera, buttons)...</p>
            </div>
        `;

        try {
            const res = await fetch('/api/pins/probe_boot', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({})
            });
            const result = await res.json();

            if (result.status === 'error') {
                alert('Probe failed: ' + result.message);
            } else {
                // Auto-match board if detected SKU exists
                const telemetry = result.telemetry || {};
                if (telemetry.detected_board) {
                    const match = Array.from(boardSelect.options).find(o => o.value.includes(telemetry.detected_board));
                    if (match) {
                        boardSelect.value = match.value;
                        if (match.dataset.chip) chipSelect.value = match.dataset.chip;
                    }
                }
            }
        } catch (e) {
            alert('Error during hardware probe: ' + e);
        } finally {
            btnProbeBoot.disabled = false;
            btnProbeBoot.innerHTML = originalText;
            await fetchPinStatus();
        }
    }

    async function runBuildDiagnostic() {
        const modalChipSelect = document.getElementById('diag-modal-chip-select');
        const chip = (modalChipSelect ? modalChipSelect.value : null) || chipSelect.value || 'esp32s3';

        btnDiagBuild.disabled = true;
        btnDiagBuild.textContent = `🛠 Compiling [${chip.toUpperCase()}]...`;
        diagLog.style.display = 'block';
        diagLogOut.textContent = `Compiling standalone Diagnostic Firmware for [${chip.toUpperCase()}] with ESP-IDF...`;

        try {
            const res = await fetch('/api/pins/diagnostic/build', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ chip })
            });
            const d = await res.json();
            if (d.status === 'ok') {
                const targetName = (d.chip || chip).toUpperCase();
                diagLogOut.textContent = `✅ Build Succeeded for [${targetName}] (Merged Binary Ready)!\nTarget Architecture: ${targetName}\nBinary:\n${d.bin_path}\n\nBinary has been combined with bootloader & partitions for direct flash to 0x0.`;
                let flashBtn = document.getElementById('btn-flash-diag-now');
                if (!flashBtn) {
                    flashBtn = document.createElement('button');
                    flashBtn.id = 'btn-flash-diag-now';
                    flashBtn.className = 'btn btn-primary';
                    flashBtn.style.marginTop = '10px';
                    flashBtn.style.padding = '8px 16px';
                    flashBtn.style.fontWeight = 'bold';
                    flashBtn.style.display = 'block';
                    flashBtn.textContent = '⚡ Open in Flasher Tab';
                    diagLog.appendChild(flashBtn);
                }
                flashBtn.style.display = 'inline-block';
                flashBtn.onclick = () => {
                    const fInput = document.getElementById('flash-firmware-input');
                    const cSelect = document.getElementById('flash-chip-select');
                    if (fInput) fInput.value = d.bin_path;
                    if (cSelect) cSelect.value = (d.chip || chip);
                    const tabFlash = document.querySelector('.nav-btn[data-target="flash-tab"]');
                    if (tabFlash) tabFlash.click();
                };
            } else {
                diagLogOut.textContent = `❌ Build output:\n${d.output || d.message}`;
            }
        } catch (e) {
            diagLogOut.textContent = `❌ Error: ${e}`;
        } finally {
            btnDiagBuild.disabled = false;
            btnDiagBuild.textContent = '🛠 Build Diagnostic Firmware (.bin)';
        }
    }

    // ── 4. Filtering Logic ─────────────────────────────────────────────────
    function isPinMatching(pin) {
        if (currentFilter === 'configured') {
            if (pin.status !== 'working' && pin.status !== 'used') return false;
        } else if (currentFilter === 'verified') {
            if (!pin.hardware_verified) return false;
        } else if (currentFilter !== 'all') {
            if (!pin.tags || !pin.tags.includes(currentFilter)) return false;
        }

        if (searchQuery) {
            const inName = pin.name.toLowerCase().includes(searchQuery);
            const inGpio = pin.num >= 0 && `gpio${pin.num}`.includes(searchQuery);
            const inAlt = pin.alt && pin.alt.some(a => a.toLowerCase().includes(searchQuery));
            const inMacros = pin.references && pin.references.some(r => r.macro.toLowerCase().includes(searchQuery));
            const inPeriph = pin.hardware_peripheral && pin.hardware_peripheral.toLowerCase().includes(searchQuery);
            if (!inName && !inGpio && !inAlt && !inMacros && !inPeriph) return false;
        }

        return true;
    }

    function applyFilters() {
        if (!pinData) return;
        const pinElements = container.querySelectorAll('.pin-group');
        pinElements.forEach(el => {
            const pinName = el.dataset.pinName;
            const pin = pinData.pins.find(p => p.name === pinName);
            if (!pin) return;

            if (isPinMatching(pin)) {
                el.classList.remove('dimmed');
                el.classList.add('highlighted');
            } else {
                el.classList.add('dimmed');
                el.classList.remove('highlighted');
            }
        });
    }

    // ── 5. SVG Diagram Renderer ────────────────────────────────────────────
    function renderDiagram() {
        if (!pinData) return;

        const leftPins  = pinData.pins.filter(p => p.side === 'left');
        const rightPins = pinData.pins.filter(p => p.side === 'right');
        const maxRows   = Math.max(leftPins.length, rightPins.length);

        const rowHeight    = 34;
        const chipBodyW    = 240;
        const pinWidth     = 42;
        const labelAreaW   = 245;
        const topMargin    = 70;
        const bottomMargin = 50;

        const totalW = labelAreaW + pinWidth + chipBodyW + pinWidth + labelAreaW;
        const totalH = topMargin + (maxRows * rowHeight) + bottomMargin;

        const chipX = labelAreaW + pinWidth;
        const chipY = topMargin;
        const chipH = maxRows * rowHeight;

        let svg = `
        <svg viewBox="0 0 ${totalW} ${totalH}" width="100%" height="${totalH}px" xmlns="http://www.w3.org/2000/svg" class="chip-svg">
            <defs>
                <filter id="glow-verified" x="-40%" y="-40%" width="180%" height="180%">
                    <feDropShadow dx="0" dy="0" stdDeviation="6" flood-color="#00ff88" flood-opacity="0.9"/>
                </filter>
                <filter id="glow-working" x="-30%" y="-30%" width="160%" height="160%">
                    <feDropShadow dx="0" dy="0" stdDeviation="4" flood-color="#00d26a" flood-opacity="0.8"/>
                </filter>
                <filter id="glow-warn" x="-30%" y="-30%" width="160%" height="160%">
                    <feDropShadow dx="0" dy="0" stdDeviation="5" flood-color="#f85149" flood-opacity="0.8"/>
                </filter>
                <filter id="glow-used" x="-30%" y="-30%" width="160%" height="160%">
                    <feDropShadow dx="0" dy="0" stdDeviation="4" flood-color="#f0a500" flood-opacity="0.7"/>
                </filter>
                <filter id="chip-shadow" x="-5%" y="-5%" width="110%" height="110%">
                    <feDropShadow dx="0" dy="6" stdDeviation="8" flood-color="#000" flood-opacity="0.6"/>
                </filter>
                <linearGradient id="ic-gradient" x1="0" y1="0" x2="1" y2="1">
                    <stop offset="0%" stop-color="#1e242c"/>
                    <stop offset="50%" stop-color="#161b22"/>
                    <stop offset="100%" stop-color="#0d1117"/>
                </linearGradient>
            </defs>

            <!-- ── Chip Module Body ── -->
            <rect x="${chipX}" y="${chipY}" width="${chipBodyW}" height="${chipH}" rx="12" ry="12"
                  fill="url(#ic-gradient)" stroke="#30363d" stroke-width="2" filter="url(#chip-shadow)"/>

            <!-- Chip Top Notch -->
            <path d="M ${chipX + chipBodyW/2 - 18} ${chipY} A 18 18 0 0 0 ${chipX + chipBodyW/2 + 18} ${chipY}" fill="#0d1117" stroke="#30363d" stroke-width="2"/>

            <!-- Pin 1 Dot Marker -->
            <circle cx="${chipX + 24}" cy="${chipY + 24}" r="4" fill="#c19628" opacity="0.8"/>

            <!-- Espressif Brand and Chip Model Text -->
            <g class="chip-center-text" text-anchor="middle">
                <text x="${chipX + chipBodyW/2}" y="${chipY + chipH/2 - 28}" fill="#8b949e" font-size="11" font-weight="600" letter-spacing="2">ESPRESSIF</text>
                <text x="${chipX + chipBodyW/2}" y="${chipY + chipH/2 - 2}" fill="#e6edf3" font-size="16" font-weight="bold" letter-spacing="1">${escapeHtml(pinData.label.toUpperCase())}</text>
                <text x="${chipX + chipBodyW/2}" y="${chipY + chipH/2 + 20}" fill="#58a6ff" font-size="11">${escapeHtml(pinData.package)}</text>
                <text x="${chipX + chipBodyW/2}" y="${chipY + chipH/2 + 38}" fill="#6e7681" font-size="10">${escapeHtml(pinData.board ? pinData.board : 'Default Pinout')}</text>
            </g>
        `;

        leftPins.forEach((pin, i) => {
            const y = topMargin + (i * rowHeight) + (rowHeight / 2);
            svg += renderPinGroup(pin, 'left', chipX, y, labelAreaW, pinWidth);
        });

        rightPins.forEach((pin, i) => {
            const y = topMargin + (i * rowHeight) + (rowHeight / 2);
            svg += renderPinGroup(pin, 'right', chipX + chipBodyW, y, labelAreaW, pinWidth);
        });

        svg += `</svg>`;
        container.innerHTML = svg;

        container.querySelectorAll('.pin-group').forEach(el => {
            el.addEventListener('click', () => {
                const pinName = el.dataset.pinName;
                const pin = pinData.pins.find(p => p.name === pinName);
                if (pin) selectPin(pin);
            });
        });

        applyFilters();
    }

    function renderPinGroup(pin, side, edgeX, y, labelAreaW, pinWidth) {
        const isLeft = side === 'left';
        const color = getPinColor(pin);

        let glowFilter = '';
        if (pin.hardware_verified) {
            glowFilter = 'filter="url(#glow-verified)"';
        } else if (pin.hardware_state === 'not_detected') {
            glowFilter = 'filter="url(#glow-warn)"';
        } else if (pin.status === 'working') {
            glowFilter = 'filter="url(#glow-working)"';
        } else if (pin.status === 'used') {
            glowFilter = 'filter="url(#glow-used)"';
        }

        const padW = pinWidth - 6;
        const padH = 14;
        const padX = isLeft ? (edgeX - pinWidth + 3) : (edgeX + 3);
        const padY = y - padH/2;

        const wireStart = isLeft ? padX : (padX + padW);
        const wireEnd   = isLeft ? (edgeX - pinWidth - 16) : (edgeX + pinWidth + 16);

        const textX = isLeft ? (wireEnd - 8) : (wireEnd + 8);
        const textAnchor = isLeft ? 'end' : 'start';

        const hasRef = pin.references && pin.references.length > 0;
        const macroText = hasRef ? pin.references[0].macro : '';

        const isSelected = selectedPin && selectedPin.name === pin.name;
        const selectedCls = isSelected ? 'selected-pin' : '';

        // Status tag text
        let hwBadge = '';
        if (pin.hardware_verified) {
            hwBadge = `<tspan fill="#00ff88" font-weight="bold"> [✓ HW ACTIVE]</tspan>`;
        } else if (pin.hardware_state === 'not_detected') {
            hwBadge = `<tspan fill="#f85149" font-weight="bold"> [⚠️ NO ACK]</tspan>`;
        }

        return `
        <g class="pin-group status-${pin.status} ${selectedCls}" data-pin-name="${pin.name}" cursor="pointer">
            <rect x="${isLeft ? 0 : edgeX}" y="${y - 16}" width="${labelAreaW + pinWidth}" height="32" fill="transparent"/>

            <!-- Lead Wire -->
            <line x1="${wireStart}" y1="${y}" x2="${wireEnd}" y2="${y}" stroke="${color}" stroke-width="2" opacity="0.65"/>

            <!-- Physical IC Pin Pad -->
            <rect x="${padX}" y="${padY}" width="${padW}" height="${padH}" rx="3" ry="3"
                  fill="${color}" stroke="#1e242c" stroke-width="1.5" ${glowFilter}/>

            <!-- Active Hardware Marker Ring on Pad -->
            ${pin.hardware_verified ? `
            <circle cx="${isLeft ? (padX + 5) : (padX + padW - 5)}" cy="${y}" r="3.5" fill="#ffffff">
                <animate attributeName="r" values="2;4;2" dur="2s" repeatCount="indefinite"/>
            </circle>` : ''}

            <!-- Failed / Absent Marker on Pad -->
            ${pin.hardware_state === 'not_detected' ? `
            <circle cx="${isLeft ? (padX + 5) : (padX + padW - 5)}" cy="${y}" r="3" fill="#f85149"/>` : ''}

            <!-- Pin Name -->
            <text x="${textX}" y="${y - 3}" fill="${isSelected ? '#ffffff' : color}" font-size="12" font-weight="bold" text-anchor="${textAnchor}">
                ${escapeHtml(pin.name)}${hwBadge}
            </text>

            <!-- Macro / Function -->
            ${macroText ? `
            <text x="${textX}" y="${y + 11}" fill="#e6edf3" font-size="10" font-family="monospace" font-weight="600" text-anchor="${textAnchor}">
                [${escapeHtml(macroText)}]
            </text>
            ` : (pin.alt && pin.alt.length > 0 ? `
            <text x="${textX}" y="${y + 11}" fill="#8b949e" font-size="9" text-anchor="${textAnchor}">
                ${escapeHtml(pin.alt.slice(0, 2).join(' / '))}
            </text>
            ` : '')}
        </g>
        `;
    }

    function getPinColor(pin) {
        if (pin.hardware_verified) return '#00ff88'; // Bright neon green for confirmed hardware
        if (pin.hardware_state === 'not_detected') return '#f85149'; // Red for failed hardware

        switch (pin.status) {
            case 'working': return '#00d26a';
            case 'used':    return '#f0a500';
            case 'unused':  return '#484f58';
            case 'power':   return '#f85149';
            case 'gnd':     return '#8b949e';
            case 'nc':      return '#30363d';
            default:        return '#484f58';
        }
    }

    // ── 6. Sidebar Details ─────────────────────────────────────────────────
    function selectPin(pin) {
        selectedPin = pin;

        container.querySelectorAll('.pin-group').forEach(el => {
            el.classList.toggle('selected-pin', el.dataset.pinName === pin.name);
        });

        sidebarTitle.textContent = `${pin.name} ${pin.num >= 0 ? `(#${pin.num})` : ''}`;

        let badgeStatus = pin.status.toUpperCase();
        if (pin.hardware_verified) badgeStatus = '✓ HW ACTIVE';
        else if (pin.hardware_state === 'not_detected') badgeStatus = '⚠️ NO ACK';

        sidebarBadge.className = `badge ${pin.hardware_verified ? 'working' : pin.status}`;
        sidebarBadge.textContent = badgeStatus;

        let html = '';

        // Hardware Verification Section (Phase 1 & Phase 2)
        if (pin.hardware_verified) {
            html += `
            <div class="sidebar-alert status-verified">
                <strong>⚡ Physical Hardware Verified!</strong><br>
                <span>${escapeHtml(pin.hardware_reason || 'Live peripheral active on this pin.')}</span>
            </div>`;
        } else if (pin.hardware_state === 'not_detected') {
            html += `
            <div class="sidebar-alert status-danger">
                <strong>⚠️ Hardware Missing / NACK</strong><br>
                <span>${escapeHtml(pin.hardware_reason || 'Driver reported transaction failure on I2C/SPI bus.')}</span>
            </div>`;
        } else if (pin.status === 'working') {
            html += `
            <div class="sidebar-alert status-working">
                🟢 <strong>WORKING:</strong> Assigned in firmware and device is connected via Serial. Click "Probe Live Hardware" to run bus verification.
            </div>`;
        } else if (pin.status === 'used') {
            html += `
            <div class="sidebar-alert status-used">
                🟡 <strong>USED:</strong> Assigned in firmware, but no physical device is currently connected.
            </div>`;
        } else if (pin.status === 'unused') {
            html += `
            <div class="sidebar-alert status-unused">
                ⚫ <strong>UNUSED:</strong> Free GPIO, not referenced in project firmware headers.
            </div>`;
        } else {
            html += `<div class="sidebar-alert status-unused">🔵 <strong>${pin.type.toUpperCase()}:</strong> System power, ground, or reset.</div>`;
        }

        // Functions & Tags
        if (pin.alt && pin.alt.length > 0) {
            html += `
            <div class="sidebar-section">
                <h4>Alternate Functions</h4>
                <div class="tag-list">
                    ${pin.alt.map(a => `<span class="pin-alt-tag">${escapeHtml(a)}</span>`).join('')}
                </div>
            </div>`;
        }

        if (pin.tags && pin.tags.length > 0) {
            html += `
            <div class="sidebar-section">
                <h4>Hardware Capabilities</h4>
                <div class="tag-list">
                    ${pin.tags.map(t => `<span class="pin-cap-tag cap-${t}">${t.toUpperCase()}</span>`).join('')}
                </div>
            </div>`;
        }

        // Code References
        if (pin.references && pin.references.length > 0) {
            html += `
            <div class="sidebar-section">
                <h4>Firmware References (${pin.references.length})</h4>
                <div class="refs-list">
                    ${pin.references.map(r => `
                        <div class="ref-card">
                            <div class="ref-macro">${escapeHtml(r.macro)}</div>
                            <div class="ref-file">${escapeHtml(r.file)}:${r.line}</div>
                            ${r.detail ? `<pre class="ref-code"><code>${escapeHtml(r.detail)}</code></pre>` : ''}
                        </div>
                    `).join('')}
                </div>
            </div>`;
        }

        sidebarBody.innerHTML = html;
    }

    function resetSidebar() {
        sidebarTitle.textContent = 'Select a pin';
        sidebarBadge.className = 'badge';
        sidebarBadge.textContent = '--';
        sidebarBody.innerHTML = '<p class="text-muted">Click any pin on the chip diagram to inspect its assigned macros, function tags, and firmware code references.</p>';
    }

    function escapeHtml(str) {
        if (!str) return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
