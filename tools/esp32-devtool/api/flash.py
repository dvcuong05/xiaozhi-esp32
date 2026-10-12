import os
import threading
from flask import Blueprint, jsonify, request, current_app
from pathlib import Path

flash_api = Blueprint('flash_api', __name__)


def _project_root() -> Path:
    """Two levels up from tools/esp32-devtool/."""
    return Path(__file__).resolve().parent.parent.parent.parent


# ─── POST /api/flash/ ──────────────────────────────────────────────────────────
@flash_api.route('/', methods=['POST'])
def flash_firmware():
    data     = request.json or {}
    port     = data.get('port')
    firmware = data.get('firmware')
    chip     = data.get('chip', 'esp32')
    baud     = int(data.get('baud', 460800))

    if not all([port, firmware, chip]):
        return jsonify({'error': 'Missing required fields: port, firmware, chip'}), 400

    flasher  = current_app.config['FIRMWARE_FLASHER']
    socketio = current_app.config['SOCKETIO']

    def emit_progress(line: str):
        # namespace='/' required when emitting from background threads
        socketio.emit('flash_progress', {'data': line}, namespace='/')

    def flash_task():
        try:
            flasher.flash(port, firmware, chip, baud, emit_progress)
        except Exception as e:
            emit_progress(f'\n[DevTool] Unexpected error: {e}\n')

    threading.Thread(target=flash_task, daemon=True, name='flash').start()
    return jsonify({'status': 'started'})


# ─── POST /api/flash/erase ────────────────────────────────────────────────────
@flash_api.route('/erase', methods=['POST'])
def erase_flash():
    data = request.json or {}
    port = data.get('port')
    chip = data.get('chip', 'esp32')

    if not port:
        return jsonify({'error': 'Port not provided'}), 400

    flasher  = current_app.config['FIRMWARE_FLASHER']
    socketio = current_app.config['SOCKETIO']

    def emit_progress(line: str):
        socketio.emit('flash_progress', {'data': line}, namespace='/')

    def erase_task():
        try:
            flasher.erase_flash(port, chip, emit_progress)
        except Exception as e:
            emit_progress(f'\n[DevTool] Unexpected error: {e}\n')

    threading.Thread(target=erase_task, daemon=True, name='erase').start()
    return jsonify({'status': 'started'})


# ─── POST /api/flash/info ─────────────────────────────────────────────────────
@flash_api.route('/info', methods=['POST'])
def chip_info():
    data = request.json or {}
    port = data.get('port')
    if not port:
        return jsonify({'error': 'Port not provided'}), 400

    flasher = current_app.config['FIRMWARE_FLASHER']
    result  = flasher.read_chip_info(port)
    return jsonify(result)


# ─── GET /api/flash/verify ────────────────────────────────────────────────────
@flash_api.route('/verify', methods=['GET'])
def verify_esptool():
    """Check that esptool is available and return its version."""
    flasher = current_app.config['FIRMWARE_FLASHER']
    return jsonify(flasher.verify_esptool())


# ─── GET /api/flash/list-bins ─────────────────────────────────────────────────
@flash_api.route('/list-bins', methods=['GET'])
def list_bins():
    """Return all .bin files found in project build/ and diagnostic firmware build/."""
    build_dir = _project_root() / 'build'
    diag_build_dir = Path(__file__).resolve().parent.parent / 'firmware' / 'diagnostic' / 'build'
    bins = []

    # Diagnostic firmware first if built
    if diag_build_dir.exists():
        for f in sorted(diag_build_dir.glob('*.bin')):
            bins.append(str(f))

    # Main firmware binaries
    if build_dir.exists():
        for f in sorted(build_dir.rglob('*.bin')):
            bins.append(str(f))

    return jsonify({'bins': bins, 'build_dir': str(build_dir)})

