from flask import Blueprint, jsonify, request, current_app

monitor_api = Blueprint('monitor_api', __name__)


# ─── POST /api/monitor/start ──────────────────────────────────────────────────
@monitor_api.route('/start', methods=['POST'])
def start_monitor():
    data = request.json or {}
    port = data.get('port')
    baud = int(data.get('baud', 115200))

    if not port:
        return jsonify({'error': 'Port not provided'}), 400

    monitor = current_app.config['SERIAL_MONITOR']
    monitor.start(port, baud)
    return jsonify({'status': 'started', 'port': port, 'baud': baud})


# ─── POST /api/monitor/stop ───────────────────────────────────────────────────
@monitor_api.route('/stop', methods=['POST'])
def stop_monitor():
    monitor = current_app.config['SERIAL_MONITOR']
    monitor.stop()
    return jsonify({'status': 'stopped'})


# ─── GET /api/monitor/status ──────────────────────────────────────────────────
@monitor_api.route('/status', methods=['GET'])
def monitor_status():
    monitor = current_app.config['SERIAL_MONITOR']
    return jsonify({
        'connected': monitor.is_connected,
        'port':      monitor._current_port,
        'baud':      monitor._current_baud,
    })


# ─── POST /api/monitor/send ───────────────────────────────────────────────────
@monitor_api.route('/send', methods=['POST'])
def monitor_send():
    """Send a string to the currently monitored serial port."""
    data = request.json or {}
    text = data.get('data', '')
    if not text:
        return jsonify({'error': 'No data to send'}), 400

    monitor = current_app.config['SERIAL_MONITOR']
    ok = monitor.send(text)
    return jsonify({'sent': ok})
