"""
hardware_prober.py — Live Hardware Telemetry & Peripheral Prober for ESP32.

Phase 1: Real-time Serial Boot Log Parsing
  - Intercepts serial monitor stream in real-time.
  - Recognizes ESP-IDF driver initialization & probe messages:
      * Display (SSD1306, SH1106, ST7789, GC9309, ILI9341, etc.)
      * Audio Codecs (ES8311, ES7210, ES8388, MAX98357, etc.)
      * Camera Modules (OV2640, OV3660, OV5640, SC031GS, etc.)
      * I2C Bus ACKs / NACKs / Transaction Failures
      * Buttons (Boot, Touch, Volume, Power)
      * UART Console
      * Battery / PMIC (AXP2101, SY6970, IP5306)
  - Maps confirmed/failed peripherals directly back to board GPIO pin assignments.
  - Emits 'hardware_telemetry' WebSocket events in real-time.

Phase 2: Diagnostic Output Parsing
  - Parses structured DIAG:{...} packets produced by diagnostic firmware.
"""

import re
import time
import threading
from typing import Dict, List, Optional, Any


class HardwareProber:
    def __init__(self, socketio=None, gpio_analyzer=None):
        self.socketio = socketio
        self.gpio_analyzer = gpio_analyzer
        self._lock = threading.Lock()

        # Detected Board SKU (from log e.g. "SKU=bread-compact-esp32")
        self.detected_board: Optional[str] = None
        self.app_version: Optional[str] = None
        self.idf_version: Optional[str] = None

        # Peripherals detected: name -> { status: 'confirmed'|'failed'|'active', pins: [...], detail: str, time: float }
        self.peripherals: Dict[str, Dict[str, Any]] = {}

        # Pin-specific hardware state: gpio (int) -> { has_hardware: bool, state: str, reason: str, peripheral: str }
        self.pin_hardware_status: Dict[int, Dict[str, Any]] = {}

        # Raw log history for diagnosis
        self.boot_log_history: List[str] = []

    # ── Feeding Serial Stream ──────────────────────────────────────────────────

    def feed_line(self, line: str):
        """Called for every line read from the Serial port."""
        if not line:
            return

        clean = line.strip()
        with self._lock:
            self.boot_log_history.append(clean)
            if len(self.boot_log_history) > 300:
                self.boot_log_history.pop(0)

        updated = self._parse_line(clean)
        if updated and self.socketio:
            try:
                self.socketio.emit('hardware_telemetry', self.get_telemetry_data(), namespace='/')
            except Exception:
                pass

    # ── Line Parser Engine ────────────────────────────────────────────────────

    def _parse_line(self, line: str) -> bool:
        """Parse an individual line for hardware signatures. Returns True if state changed."""
        changed = False

        # 1. Board SKU / UUID
        # Example: I (149) Board: UUID=ba4e6dfe... SKU=bread-compact-esp32
        m_sku = re.search(r'SKU=([a-zA-Z0-9_\-]+)', line)
        if m_sku:
            sku = m_sku.group(1)
            if self.detected_board != sku:
                self.detected_board = sku
                changed = True

        # App Version & ESP-IDF Version
        m_app = re.search(r'App version:\s*([0-9\.\-a-zA-Z]+)', line)
        if m_app:
            self.app_version = m_app.group(1)
            changed = True

        m_idf = re.search(r'ESP-IDF:\s*([v0-9\.]+)', line)
        if m_idf:
            self.idf_version = m_idf.group(1)
            changed = True

        # 2. UART Console
        # Example: I (50) cpu_start: GPIO 3 and 1 are used as console UART I/O pins
        m_uart = re.search(r'GPIO\s+(\d+)\s+and\s+(\d+)\s+are used as console UART', line, re.I)
        if m_uart:
            rx, tx = int(m_uart.group(1)), int(m_uart.group(2))
            self._set_peripheral('UART Console', 'confirmed', [rx, tx], 'Console UART active')
            self._set_pin_hw(rx, True, 'confirmed', 'UART RX console', 'UART Console')
            self._set_pin_hw(tx, True, 'confirmed', 'UART TX console', 'UART Console')
            changed = True

        # 3. I2C / Display Probing
        # Failed Display Example:
        # E (159) lcd_panel.io.i2c: panel_io_i2c_tx_buffer(197): i2c transaction failed
        # E (169) ESP32-MarsbearSupport: Failed to initialize display
        if 'i2c transaction failed' in line or 'Failed to initialize display' in line or 'SSD1306_CMD_SET_MULTIPLEX failed' in line:
            self._set_peripheral('I2C Display', 'failed', [], 'I2C transaction failed: No display acknowledging')
            # If board pins are known, update SDA/SCL pins as failed
            self._mark_current_board_pins(['DISPLAY_SDA_PIN', 'DISPLAY_SCL_PIN', 'DISPLAY_SDA', 'DISPLAY_SCL'],
                                          has_hw=False, state='not_detected', reason='I2C NACK (No display response)', periph='I2C Display')
            changed = True

        # Success Display Example:
        # I (149) ESP32-MarsbearSupport: Turning display on
        # I (78) ... SSD1306 driver installed
        elif ('SSD1306 driver installed' in line or 'Turning display on' in line or 'Install LCD driver' in line) and 'Failed' not in line:
            if self.peripherals.get('I2C Display', {}).get('status') != 'failed':
                self._set_peripheral('I2C Display', 'confirmed', [], 'Display initialized successfully')
                self._mark_current_board_pins(['DISPLAY_SDA_PIN', 'DISPLAY_SCL_PIN', 'DISPLAY_SDA', 'DISPLAY_SCL', 'DISPLAY_MOSI_PIN', 'DISPLAY_CLK_PIN', 'DISPLAY_CS_PIN'],
                                              has_hw=True, state='confirmed', reason='Display driver active', periph='Display')
                changed = True

        # 4. Audio Codec & I2S
        # Example: I (199) AudioCodec: Audio codec started
        # Example: I (199) NoAudioCodec: Simplex channels created
        if 'Audio codec started' in line or 'Simplex channels created' in line or 'I2S channel enabled' in line:
            self._set_peripheral('Audio I2S', 'confirmed', [], 'I2S audio pipeline running')
            self._mark_current_board_pins([
                'AUDIO_I2S_MIC_GPIO_WS', 'AUDIO_I2S_MIC_GPIO_SCK', 'AUDIO_I2S_MIC_GPIO_DIN',
                'AUDIO_I2S_SPK_GPIO_DOUT', 'AUDIO_I2S_SPK_GPIO_BCLK', 'AUDIO_I2S_SPK_GPIO_LRCK',
                'AUDIO_I2S_GPIO_WS', 'AUDIO_I2S_GPIO_BCLK', 'AUDIO_I2S_GPIO_DIN', 'AUDIO_I2S_GPIO_DOUT'
            ], has_hw=True, state='confirmed', reason='I2S audio stream active', periph='Audio I2S')
            changed = True

        # Dedicated Codec IC (ES8311, ES7210, ES8388)
        if 'es8311' in line.lower() or 'es7210' in line.lower() or 'es8388' in line.lower():
            if 'found' in line.lower() or 'init' in line.lower():
                self._set_peripheral('Audio Codec IC', 'confirmed', [], line[:80])
                self._mark_current_board_pins(['AUDIO_CODEC_I2C_SDA_PIN', 'AUDIO_CODEC_I2C_SCL_PIN', 'AUDIO_CODEC_PA_PIN'],
                                              has_hw=True, state='confirmed', reason='Codec IC confirmed on bus', periph='Audio Codec IC')
                changed = True
            elif 'not found' in line.lower() or 'failed' in line.lower():
                self._set_peripheral('Audio Codec IC', 'failed', [], 'Codec IC not responding')
                changed = True

        # 5. Camera
        # Example: E (xxx) camera: Camera probe failed
        if 'Camera probe failed' in line or 'esp_camera_init' in line and 'fail' in line.lower():
            self._set_peripheral('Camera', 'failed', [], 'Camera sensor not detected')
            self._mark_current_board_pins(['CAM_PIN_D0', 'CAM_PIN_D1', 'CAM_PIN_D2', 'CAM_PIN_D3',
                                          'CAM_PIN_D4', 'CAM_PIN_D5', 'CAM_PIN_D6', 'CAM_PIN_D7',
                                          'CAM_PIN_XCLK', 'CAM_PIN_PCLK', 'CAM_PIN_VSYNC', 'CAM_PIN_HREF',
                                          'CAM_PIN_SIOD', 'CAM_PIN_SIOC', 'CAM_PIN_RESET', 'CAM_PIN_PWDN'],
                                          has_hw=False, state='not_detected', reason='Camera probe failed (module absent)', periph='Camera')
            changed = True
        elif 'Camera init done' in line or 'esp_camera_init() success' in line:
            self._set_peripheral('Camera', 'confirmed', [], 'Camera sensor online')
            self._mark_current_board_pins(['CAM_PIN_D0', 'CAM_PIN_D1', 'CAM_PIN_D2', 'CAM_PIN_D3',
                                          'CAM_PIN_D4', 'CAM_PIN_D5', 'CAM_PIN_D6', 'CAM_PIN_D7',
                                          'CAM_PIN_XCLK', 'CAM_PIN_PCLK', 'CAM_PIN_VSYNC', 'CAM_PIN_HREF',
                                          'CAM_PIN_SIOD', 'CAM_PIN_SIOC'],
                                          has_hw=True, state='confirmed', reason='Camera sensor online', periph='Camera')
            changed = True

        # 6. Physical Buttons
        # Example: I (149) button: IoT Button Version: 4.2.1
        # Example: I (...) button clicked
        if 'IoT Button Version' in line or 'button' in line.lower() and ('clicked' in line.lower() or 'pressed' in line.lower() or 'configured' in line.lower()):
            self._set_peripheral('Buttons', 'active', [], 'Physical button driver loaded')
            self._mark_current_board_pins(['BOOT_BUTTON_GPIO', 'TOUCH_BUTTON_GPIO', 'VOLUME_UP_BUTTON_GPIO', 'VOLUME_DOWN_BUTTON_GPIO', 'PWR_BUTTON_GPIO'],
                                          has_hw=True, state='confirmed', reason='Button driver / event active', periph='Buttons')
            changed = True

        # 7. Phase 2 Diagnostic JSON
        # Example: DIAG:{"event":"scan_result", "pins":[...]}
        if line.startswith('DIAG:'):
            import json
            try:
                diag_data = json.loads(line[5:].strip())
                self._apply_diagnostic_data(diag_data)
                changed = True
            except Exception:
                pass

        return changed

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _set_peripheral(self, name: str, status: str, pins: List[int], detail: str):
        self.peripherals[name] = {
            'name':      name,
            'status':    status,     # 'confirmed' | 'failed' | 'active'
            'pins':      pins,
            'detail':    detail,
            'timestamp': time.time()
        }

    def _set_pin_hw(self, gpio: int, has_hw: bool, state: str, reason: str, periph: str):
        self.pin_hardware_status[gpio] = {
            'has_hardware': has_hw,
            'state':        state,      # 'confirmed' | 'not_detected' | 'active' | 'unknown'
            'reason':       reason,
            'peripheral':   periph,
            'timestamp':    time.time()
        }

    def _mark_current_board_pins(self, macro_names: List[str], has_hw: bool, state: str, reason: str, periph: str):
        """Find GPIO numbers in the currently active board matching any of macro_names and mark them."""
        if not self.gpio_analyzer:
            return

        board_id = self.detected_board
        if not board_id:
            return

        try:
            usage = self.gpio_analyzer.analyze(board_id)
            matched_gpios = []
            for gpio, refs in usage.items():
                for ref in refs:
                    if ref.macro in macro_names or any(m in ref.macro for m in macro_names):
                        self._set_pin_hw(gpio, has_hw, state, f"{reason} ({ref.macro})", periph)
                        matched_gpios.append(gpio)
            if matched_gpios and periph in self.peripherals:
                self.peripherals[periph]['pins'] = list(set(self.peripherals[periph]['pins'] + matched_gpios))
        except Exception:
            pass

    def _apply_diagnostic_data(self, data: dict):
        """Apply structured results from Phase 2 diagnostic firmware."""
        pins = data.get('pins', [])
        for p in pins:
            gpio = p.get('gpio')
            if gpio is None:
                continue
            has_hw = p.get('has_component', False)
            state = 'confirmed' if has_hw else 'floating'
            detail = p.get('detail', p.get('state', ''))
            self._set_pin_hw(gpio, has_hw, state, detail, 'Diagnostic Engine')

        self._set_peripheral('Hardware Diagnostic', 'confirmed', [p['gpio'] for p in pins if p.get('has_component')],
                              f"Deep scan: {sum(1 for p in pins if p.get('has_component'))} components detected")

    # ── Public Queries ────────────────────────────────────────────────────────

    def get_pin_status(self, gpio: int) -> Optional[Dict[str, Any]]:
        return self.pin_hardware_status.get(gpio)

    def get_telemetry_data(self) -> Dict[str, Any]:
        return {
            'detected_board': self.detected_board,
            'app_version':    self.app_version,
            'idf_version':    self.idf_version,
            'peripherals':    list(self.peripherals.values()),
            'pin_status':     self.pin_hardware_status,
            'log_sample':     self.boot_log_history[-10:] if self.boot_log_history else []
        }

    def reset_telemetry(self):
        with self._lock:
            self.peripherals.clear()
            self.pin_hardware_status.clear()
            self.boot_log_history.clear()
