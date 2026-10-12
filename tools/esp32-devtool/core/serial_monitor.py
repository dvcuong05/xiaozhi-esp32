import serial
import threading
import time


class SerialMonitor:
    """
    Background serial reader that streams data to connected WebSocket clients
    via Flask-SocketIO's 'monitor_data' event.

    Thread safety: all shared state accessed under _lock.
    """

    def __init__(self, socketio, hardware_prober=None):
        self.socketio = socketio
        self.hardware_prober = hardware_prober
        self.serial_conn: serial.Serial | None = None
        self.running = False
        self._lock = threading.Lock()
        self.thread: threading.Thread | None = None
        self._current_port: str | None = None
        self._current_baud: int = 115200

    # ── Public ──────────────────────────────────────────────────────────────────

    @property
    def is_connected(self) -> bool:
        with self._lock:
            return self.running and self.serial_conn is not None

    def start(self, port: str, baud: int) -> None:
        """Open the serial port and start streaming data to WebSocket clients."""
        self.stop()   # tear down any existing connection first

        self._emit('monitor_data', {'data': f'[DevTool] Connecting to {port} @ {baud} baud...\n'})
        try:
            conn = serial.Serial(port, baud, timeout=0.1)
            with self._lock:
                self.serial_conn = conn
                self.running = True
                self._current_port = port
                self._current_baud = baud

            self.thread = threading.Thread(target=self._read_loop, daemon=True, name=f'monitor-{port}')
            self.thread.start()
            self._emit('monitor_data', {'data': f'[DevTool] Connected. Reading...\n'})
            self._emit('monitor_status', {'connected': True, 'port': port, 'baud': baud})
        except serial.SerialException as e:
            self._emit('monitor_data', {'data': f'[DevTool] ERROR: {e}\n'})
            self._emit('monitor_status', {'connected': False, 'port': port, 'error': str(e)})
        except Exception as e:
            self._emit('monitor_data', {'data': f'[DevTool] ERROR: {e}\n'})
            self._emit('monitor_status', {'connected': False, 'port': port, 'error': str(e)})

    def stop(self) -> None:
        """Stop monitoring and close the serial port."""
        with self._lock:
            was_running = self.running
            self.running = False
            conn = self.serial_conn
            self.serial_conn = None
            port = self._current_port

        # Let the read loop exit
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2.0)

        if conn and conn.is_open:
            try:
                conn.close()
            except Exception:
                pass

        if was_running:
            self._emit('monitor_data', {'data': f'[DevTool] Disconnected from {port}.\n'})
            self._emit('monitor_status', {'connected': False, 'port': port})

    def send(self, data: str) -> bool:
        """Send data to the serial port (for interactive use)."""
        with self._lock:
            conn = self.serial_conn if self.running else None
        if conn and conn.is_open:
            try:
                conn.write(data.encode('utf-8', errors='replace'))
                return True
            except Exception:
                pass
        return False

    # ── Private ─────────────────────────────────────────────────────────────────

    def _emit(self, event: str, data: dict) -> None:
        """Thread-safe emit to all connected WebSocket clients."""
        try:
            # namespace='/' is required when emitting from background threads
            # in Flask-SocketIO threading mode
            self.socketio.emit(event, data, namespace='/')
        except Exception:
            pass

    def _read_loop(self):
        """Runs in a daemon thread. Reads lines and emits 'monitor_data' events."""
        while True:
            with self._lock:
                if not self.running or self.serial_conn is None:
                    break
                conn = self.serial_conn

            try:
                raw = conn.readline()   # timeout=0.1 set at Serial() construction
                if raw:
                    text = raw.decode('utf-8', errors='replace')
                    self._emit('monitor_data', {'data': text})
                    if self.hardware_prober:
                        self.hardware_prober.feed_line(text)
            except serial.SerialException as e:
                self._emit('monitor_data', {'data': f'\n[DevTool] Serial disconnected: {e}\n'})
                self._emit('monitor_status', {'connected': False, 'error': str(e)})
                with self._lock:
                    self.running = False
                break
            except Exception:
                # Transient read error — keep going
                time.sleep(0.01)

    def reboot_and_probe(self, port: str, baud: int = 115200, duration: float = 2.5) -> dict:
        """
        Hardware Reset (via DTR/RTS) on connected ESP32, capturing startup boot logs
        for `duration` seconds to discover verified physical peripherals.
        """
        was_active = self.is_connected
        active_port = self._current_port

        # If monitor is running on this port, temporarily pause reading
        if was_active and active_port == port:
            self.stop()

        captured_lines = []
        try:
            conn = serial.Serial(port, baud, timeout=0.2)
            # ESP32 standard auto-reset pulse (DTR low, RTS pulse)
            conn.dtr = False
            conn.rts = True
            time.sleep(0.05)
            conn.rts = False
            conn.dtr = True
            time.sleep(0.05)
            conn.dtr = False

            t_end = time.time() + duration
            while time.time() < t_end:
                line = conn.readline()
                if line:
                    decoded = line.decode('utf-8', errors='replace').strip()
                    captured_lines.append(decoded)
                    if self.hardware_prober:
                        self.hardware_prober.feed_line(decoded)

            conn.close()
        except Exception as e:
            return {'status': 'error', 'message': str(e)}

        # Resume monitor if it was previously running
        if was_active and active_port == port:
            self.start(port, baud)

        return {
            'status': 'ok',
            'captured_lines_count': len(captured_lines),
            'telemetry': self.hardware_prober.get_telemetry_data() if self.hardware_prober else {}
        }
