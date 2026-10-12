#!/usr/bin/env python3
"""
ESP32 DevTool — main entry point.
Run with:  python app.py
Requires:  pip install -r requirements.txt
"""

import os
import threading
import time
import webbrowser
from flask import Flask, send_from_directory
from flask_socketio import SocketIO

from core.port_scanner import PortScanner
from core.project_analyzer import ProjectAnalyzer
from core.firmware_flasher import FirmwareFlasher
from core.serial_monitor import SerialMonitor
from api.ports import ports_api
from api.flash import flash_api
from api.monitor import monitor_api
from api.pins import pins_api

# ─── App setup ────────────────────────────────────────────────────────────────
app = Flask(__name__, static_folder='static')
app.config['SECRET_KEY'] = 'esp32-devtool-secret'

# 'threading' mode — works reliably on Windows without eventlet patches.
# IMPORTANT: when emitting from background threads, always pass namespace='/'
socketio = SocketIO(app, async_mode='threading', cors_allowed_origins='*',
                    logger=False, engineio_logger=False)

from core.gpio_analyzer import GpioAnalyzer
from core.hardware_prober import HardwareProber
from core.diagnostic_engine import DiagnosticEngine

# ─── Core services ─────────────────────────────────────────────────────────────
_project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

project_analyzer  = ProjectAnalyzer(_project_root)
gpio_analyzer     = GpioAnalyzer(_project_root)
hardware_prober   = HardwareProber(socketio, gpio_analyzer)
diagnostic_engine = DiagnosticEngine(_project_root, hardware_prober)
port_scanner      = PortScanner(project_analyzer)
firmware_flasher  = FirmwareFlasher()
serial_monitor    = SerialMonitor(socketio, hardware_prober)

app.config.update(
    PORT_SCANNER      = port_scanner,
    PROJECT_ANALYZER  = project_analyzer,
    GPIO_ANALYZER     = gpio_analyzer,
    HARDWARE_PROBER   = hardware_prober,
    DIAGNOSTIC_ENGINE = diagnostic_engine,
    FIRMWARE_FLASHER  = firmware_flasher,
    SERIAL_MONITOR    = serial_monitor,
    SOCKETIO          = socketio,
    PROJECT_ROOT      = _project_root,
)

# ─── Blueprints ────────────────────────────────────────────────────────────────
app.register_blueprint(ports_api,   url_prefix='/api/ports')
app.register_blueprint(flash_api,   url_prefix='/api/flash')
app.register_blueprint(monitor_api, url_prefix='/api/monitor')
app.register_blueprint(pins_api,    url_prefix='/api/pins')


# ─── Static ────────────────────────────────────────────────────────────────────
@app.route('/')
def index():
    return send_from_directory('static', 'index.html')


# ─── WebSocket events ──────────────────────────────────────────────────────────
@socketio.on('connect')
def on_connect():
    # Send current port list immediately on new connection
    try:
        ports = port_scanner.scan_ports()
        socketio.emit('port_update', [p.__dict__ for p in ports], namespace='/')
    except Exception:
        pass

@socketio.on('monitor_send')
def on_monitor_send(data):
    """Client sends a string to the serial port (interactive input)."""
    text = data.get('data', '')
    if text:
        serial_monitor.send(text)


# ─── Background port polling ───────────────────────────────────────────────────
def _port_polling_loop():
    """Emit 'port_update' to all connected clients every 2 seconds."""
    while True:
        time.sleep(2)
        try:
            ports = port_scanner.scan_ports()
            # namespace='/' is required when emitting from background threads
            socketio.emit('port_update', [p.__dict__ for p in ports], namespace='/')
        except Exception as e:
            print(f'[DevTool] Port poll error: {e}')


# ─── Entry point ───────────────────────────────────────────────────────────────
if __name__ == '__main__':
    HOST = '127.0.0.1'
    PORT = 5173
    url  = f'http://{HOST}:{PORT}'

    # Verify esptool availability at startup
    from core.firmware_flasher import _PYTHON
    esptool_check = firmware_flasher.verify_esptool()
    if esptool_check['ok']:
        print(f'[DevTool] esptool OK  ({esptool_check["output"].splitlines()[0]})')
    else:
        print(f'[DevTool] WARNING: esptool not found via {_PYTHON}')
        print(f'          {esptool_check["output"]}')
        print(f'          Flash feature will not work until esptool is installed.')

    polling = threading.Thread(target=_port_polling_loop, daemon=True, name='port-poll')
    polling.start()

    threading.Timer(1.2, lambda: webbrowser.open(url)).start()

    print(f'\n  [ESP32 DevTool]  {url}\n')
    socketio.run(app, host=HOST, port=PORT, debug=False,
                 use_reloader=False, allow_unsafe_werkzeug=True)
