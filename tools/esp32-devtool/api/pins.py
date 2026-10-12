"""
api/pins.py — Pin Inspector & Hardware Diagnostic endpoints.

GET  /api/pins/chips           → list of supported chips
GET  /api/pins/boards          → list of project boards with auto-detected chip
GET  /api/pins/layout          → pin layout for a chip (no status)
POST /api/pins/status          → pin layout + GPIO usage + hardware telemetry + working/used/unused status
POST /api/pins/probe_boot      → reboot chip via DTR/RTS and capture boot telemetry for 2.5s
GET  /api/pins/telemetry       → get live hardware telemetry data
POST /api/pins/diagnostic/build → compile diagnostic firmware
"""

from flask import Blueprint, jsonify, request, current_app
from core.chip_pins import CHIPS, list_chips, get_chip
from core.gpio_analyzer import GpioAnalyzer
import os

pins_api = Blueprint('pins_api', __name__)

_gpio_analyzer: GpioAnalyzer | None = None


def _get_analyzer() -> GpioAnalyzer:
    global _gpio_analyzer
    if _gpio_analyzer is None:
        project_root = current_app.config.get('PROJECT_ROOT', os.getcwd())
        _gpio_analyzer = GpioAnalyzer(project_root)
    return _gpio_analyzer


# ─── GET /api/pins/chips ──────────────────────────────────────────────────────
@pins_api.route('/chips', methods=['GET'])
def get_chips():
    return jsonify(list_chips())


# ─── GET /api/pins/boards ─────────────────────────────────────────────────────
@pins_api.route('/boards', methods=['GET'])
def get_boards():
    analyzer = _get_analyzer()
    boards = analyzer.get_board_list()
    return jsonify(boards)


# ─── GET /api/pins/layout?chip=esp32s3 ────────────────────────────────────────
@pins_api.route('/layout', methods=['GET'])
def get_layout():
    chip_id = request.args.get('chip', 'esp32s3').lower()
    chip = get_chip(chip_id)
    if not chip:
        return jsonify({'error': f'Unknown chip: {chip_id}'}), 404
    return jsonify({
        'chip':        chip_id,
        'label':       chip['label'],
        'package':     chip['package'],
        'gpio_count':  chip['gpio_count'],
        'pins':        chip['pins'],
    })


# ─── POST /api/pins/status ────────────────────────────────────────────────────
@pins_api.route('/status', methods=['POST'])
def get_pin_status():
    """
    Body: { "chip": "esp32s3", "board": "bread-compact-wifi" }

    Combines:
      1. Static chip pinout
      2. Project code GPIO usage
      3. Physical COM connection status
      4. Live Hardware Prober / Boot Log Telemetry (Phase 1 & Phase 2)
    """
    data      = request.json or {}
    chip_id   = data.get('chip', 'esp32s3').lower()
    board_dir = data.get('board', '')

    chip = get_chip(chip_id)
    if not chip:
        return jsonify({'error': f'Unknown chip: {chip_id}'}), 404

    # ── Check if ESP32 is physically connected ────────────────────────────
    port_scanner = current_app.config.get('PORT_SCANNER')
    chip_connected = False
    connected_ports = []
    if port_scanner:
        try:
            ports = port_scanner.scan_ports()
            connected_ports = [p.name for p in ports if p.status == 'working']
            chip_connected = len(connected_ports) > 0
        except Exception:
            pass

    # ── Analyze GPIO usage in project code ─────────────────────────────────
    analyzer = _get_analyzer()
    usage: dict = {}
    try:
        usage = analyzer.analyze(board_dir if board_dir else None)
    except Exception as e:
        print(f'[Pins] GPIO analysis error: {e}')

    # ── Live Hardware Telemetry Prober (Phase 1 & Phase 2) ────────────────
    prober = current_app.config.get('HARDWARE_PROBER')
    telemetry_data = prober.get_telemetry_data() if prober else {}

    # ── Build annotated pin list ──────────────────────────────────────────
    annotated = []
    for pin in chip['pins']:
        gpio_num = pin['num']
        refs_raw = usage.get(gpio_num, []) if gpio_num >= 0 else []

        refs = [
            {
                'gpio':   r.gpio,
                'macro':  r.macro,
                'file':   r.file,
                'line':   r.line,
                'detail': r.detail,
            }
            for r in refs_raw
        ]

        is_used = len(refs) > 0

        # Physical hardware verification status from prober
        hw_info = prober.get_pin_status(gpio_num) if (prober and gpio_num >= 0) else None
        hardware_verified = False
        hardware_state = 'unverified'
        hardware_reason = ''
        hardware_peripheral = ''

        if hw_info:
            hardware_verified = hw_info.get('has_hardware', False)
            hardware_state    = hw_info.get('state', 'unverified')
            hardware_reason   = hw_info.get('reason', '')
            hardware_peripheral = hw_info.get('peripheral', '')

        # Determine visual pin status
        if gpio_num < 0 or pin['type'] in ('power', 'gnd', 'nc'):
            status = pin['type']          # 'power' | 'gnd' | 'nc'
        elif is_used and chip_connected:
            status = 'working'
        elif is_used:
            status = 'used'
        else:
            status = 'unused'

        annotated.append({
            **pin,
            'status':              status,
            'references':          refs,
            'hardware_verified':   hardware_verified,
            'hardware_state':      hardware_state,
            'hardware_reason':     hardware_reason,
            'hardware_peripheral': hardware_peripheral,
        })

    # ── Stats ─────────────────────────────────────────────────────────────
    io_pins      = [p for p in annotated if p['type'] not in ('power', 'gnd', 'nc')]
    n_working    = sum(1 for p in io_pins if p['status'] == 'working')
    n_used       = sum(1 for p in io_pins if p['status'] == 'used')
    n_unused     = sum(1 for p in io_pins if p['status'] == 'unused')
    n_hw_verif   = sum(1 for p in io_pins if p.get('hardware_verified'))

    return jsonify({
        'chip':             chip_id,
        'label':            chip['label'],
        'package':          chip['package'],
        'board':            board_dir,
        'chip_connected':   chip_connected,
        'connected_ports':  connected_ports,
        'stats': {
            'working':           n_working,
            'used':              n_used,
            'unused':            n_unused,
            'hardware_verified': n_hw_verif,
        },
        'telemetry':        telemetry_data,
        'pins':             annotated,
    })


# ─── POST /api/pins/probe_boot ────────────────────────────────────────────────
@pins_api.route('/probe_boot', methods=['POST'])
def probe_boot():
    """
    Triggers an auto-reset (DTR/RTS pulse) on connected ESP32, captures 2.5s
    of startup boot logs, parses peripheral detection, and returns telemetry.
    """
    data = request.json or {}
    port = data.get('port')
    baud = int(data.get('baud', 115200))

    if not port:
        scanner = current_app.config.get('PORT_SCANNER')
        if scanner:
            working = [p.name for p in scanner.scan_ports() if p.status == 'working']
            if working:
                port = working[0]

    if not port:
        return jsonify({'status': 'error', 'message': 'No connected ESP32 port found'}), 400

    monitor = current_app.config.get('SERIAL_MONITOR')
    if not monitor:
        return jsonify({'status': 'error', 'message': 'Serial monitor not initialized'}), 500

    result = monitor.reboot_and_probe(port, baud, duration=2.5)
    return jsonify(result)


# ─── GET /api/pins/telemetry ──────────────────────────────────────────────────
@pins_api.route('/telemetry', methods=['GET'])
def get_telemetry():
    prober = current_app.config.get('HARDWARE_PROBER')
    if prober:
        return jsonify(prober.get_telemetry_data())
    return jsonify({})


# ─── POST /api/pins/diagnostic/build ──────────────────────────────────────────
@pins_api.route('/diagnostic/build', methods=['POST'])
def build_diagnostic():
    data = request.json or {}
    chip = data.get('chip', 'esp32s3')
    engine = current_app.config.get('DIAGNOSTIC_ENGINE')
    if not engine:
        return jsonify({'status': 'error', 'message': 'Diagnostic engine not initialized'}), 500

    res = engine.build_diagnostic_firmware(chip)
    return jsonify(res)
