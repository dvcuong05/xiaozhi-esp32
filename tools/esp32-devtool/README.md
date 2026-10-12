# ⚡ ESP32 DevTool

A local web-based developer tool for ESP32 firmware projects. Provides:

- **Port Visualizer** — real-time view of all serial ports with color-coded status
- **Firmware Flasher** — flash `.bin` files via `esptool` with live progress output
- **Serial Monitor** — real-time terminal output from any connected ESP32

---

## Quick Start

```bash
cd tools/esp32-devtool

# Install dependencies (one-time)
pip install -r requirements.txt

# Run the tool
python app.py
```

The browser opens automatically at **http://localhost:5173**.

> **Note**: If `python` resolves to the ESP-IDF Python (check with `python --version`), use the full path to your system Python 3.11+:
> ```bash
> C:\Users\<you>\AppData\Local\Programs\Python\Python313\python.exe app.py
> ```

---

## Port Status Legend

| Status | Color | Meaning |
|--------|-------|---------|
| **WORKING** | 🟢 Green | A physical device is **actually plugged in** right now (USB-UART detected by OS) |
| **USED** | 🟡 Amber | Port name is referenced in project firmware/config files but **no device is connected** |
| **RESERVED** | ⚫ Gray | Port exists in the OS (Bluetooth virtual COM, software modem, etc.) but is not a real hardware device |

> `WORKING` is set **if and only if** a physical USB/UART device is enumerated by the OS (`serial.tools.list_ports`). Bluetooth virtual ports are always `RESERVED`, never `WORKING`.

---

## Features

### Port Visualizer
- Auto-refreshes every 2 seconds via WebSocket
- Filter: All / Working / Used / Reserved
- Shows VID:PID, manufacturer, serial number
- ⚡ badge for detected ESP32-family chips (CP210x, CH340/CH343, FTDI, Espressif native USB)
- One-click **Monitor** or **Flash** from any working port card

### Firmware Flasher
- Chip selector: ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-P4
- Baud rates: 115200 / 460800 / 921600
- **Browse build/** — lists all `.bin` files in `build/`
- Live flash progress streamed to terminal
- **Erase Flash** with confirmation dialog
- **Read Chip Info** via `esptool chip_id`

### Serial Monitor
- Baud: 9600 / 74880 / 115200 / 230400 / 460800 / 921600
- Error lines highlighted in red, warnings in amber
- Filter / search output in real-time
- Auto-scroll toggle
- Download log as `.txt`

---

## Architecture

```
tools/esp32-devtool/
├── app.py                    # Flask + SocketIO entry point
├── requirements.txt
├── core/
│   ├── port_scanner.py       # Port detection & working/used/reserved logic
│   ├── project_analyzer.py   # Scans project config for port references
│   ├── firmware_flasher.py   # esptool subprocess wrapper
│   └── serial_monitor.py     # Threaded serial reader → WebSocket
├── api/
│   ├── ports.py              # GET /api/ports, POST /api/ports/probe
│   ├── flash.py              # POST /api/flash/, /erase, /info, GET /list-bins
│   └── monitor.py            # POST /api/monitor/start, /stop
└── static/
    ├── index.html
    ├── css/style.css
    └── js/
        ├── app.js             # Socket + AppState init (loads first)
        ├── port-visualizer.js
        ├── flasher.js
        └── monitor.js
```

---

## Detected ESP32 USB Chips

| VID | Chip | Common Boards |
|-----|------|---------------|
| `10C4` | Silicon Labs CP210x | Most common ESP32 dev boards |
| `1A86` | WCH CH340 / CH341 / CH343 | Cheap Chinese boards |
| `0403` | FTDI FT232 | Classic boards |
| `303A` | Espressif native USB | ESP32-S3 / C3 / C6 CDC mode |
| `067B` | Prolific PL2303 | Older adapters |
