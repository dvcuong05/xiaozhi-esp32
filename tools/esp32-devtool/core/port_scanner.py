from dataclasses import dataclass, field
from typing import Optional, List
import serial.tools.list_ports
import serial
from core.project_analyzer import ProjectAnalyzer


@dataclass
class PortInfo:
    name: str
    status: str           # "working" | "used" | "reserved"
    description: str
    vid: Optional[int]
    pid: Optional[int]
    manufacturer: Optional[str]
    serial_number: Optional[str]
    is_esp32: bool
    referenced_in: List[str] = field(default_factory=list)


# Known ESP32-family USB chip VID/PID pairs
ESP32_VID_PID = [
    (0x10C4, None),   # Silicon Labs CP210x  — ubiquitous on ESP32 boards
    (0x1A86, None),   # WCH CH340 / CH341 / CH343 — cheap/popular boards
    (0x0403, None),   # FTDI FT232           — classic boards
    (0x303A, None),   # Espressif native USB — ESP32-S3/C3/C6 CDC
    (0x067B, None),   # Prolific PL2303
]

# Substrings in the port description that indicate a virtual/non-hardware port
_VIRTUAL_PORT_KEYWORDS = [
    'bluetooth',
    'bth',            # Windows Bluetooth serial
    'virtual',
    'standard serial over bluetooth',
    'modem',          # Generic modem
]


class PortScanner:
    def __init__(self, project_analyzer: ProjectAnalyzer):
        self.project_analyzer = project_analyzer

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _is_esp32(self, vid: Optional[int], pid: Optional[int]) -> bool:
        if vid is None:
            return False
        for known_vid, known_pid in ESP32_VID_PID:
            if vid == known_vid and (known_pid is None or pid == known_pid):
                return True
        return False

    def _is_virtual_port(self, p) -> bool:
        """
        Detect virtual/Bluetooth ports that enumerate in list_ports but are
        not physical USB serial devices.  Criteria (any one suffices):
          1. No USB VID/PID (vid is None) AND description matches known virtual keywords.
          2. Description explicitly contains 'bluetooth' or 'bth' (case-insensitive).
        """
        desc = (p.description or '').lower()
        hwid = (p.hwid or '').lower()

        # If it has a real VID it's a USB device → never virtual
        if p.vid is not None:
            return False

        # No VID — check description / hwid for virtual indicators
        for kw in _VIRTUAL_PORT_KEYWORDS:
            if kw in desc or kw in hwid:
                return True

        return False

    def probe_port(self, port: str) -> bool:
        """
        Try to briefly open the port to verify accessibility.
        Used only by /api/ports/probe; scan_ports() does not call this.
        """
        try:
            s = serial.Serial(port, timeout=0.05)
            s.close()
            return True
        except Exception:
            return False

    def get_port_details(self, port: str) -> dict:
        for p in serial.tools.list_ports.comports():
            if p.device == port:
                return {
                    'vid': p.vid,
                    'pid': p.pid,
                    'manufacturer': p.manufacturer,
                    'description': p.description,
                    'serial_number': p.serial_number,
                }
        return {}

    # ------------------------------------------------------------------
    # Main scan
    # ------------------------------------------------------------------

    def scan_ports(self) -> List[PortInfo]:
        """
        Classify all serial ports into three states:

        working  — A real, physical hardware device is plugged in right now.
                   Detected by presence in list_ports AND not matching virtual-
                   port heuristics (Bluetooth, modem, etc.).

        used     — Port is declared in project firmware / config files but
                   there is no physical device currently attached.

        reserved — Port is known to the OS (e.g., Bluetooth virtual COM,
                   software modem) but has no real device and is not referenced
                   in project configs.
        """
        referenced_ports = self.project_analyzer.get_referenced_ports()

        physical_ports   = serial.tools.list_ports.comports()
        physical_port_names = {p.device for p in physical_ports}

        port_infos: List[PortInfo] = []

        for p in physical_ports:
            is_virtual = self._is_virtual_port(p)
            is_esp     = self._is_esp32(p.vid, p.pid)
            refs       = referenced_ports.get(p.device, [])

            if is_virtual:
                # Bluetooth / virtual — port exists in OS but no real device
                status = 'reserved'
            else:
                # Real USB/UART device physically connected
                status = 'working'

            port_infos.append(PortInfo(
                name=p.device,
                status=status,
                description=p.description or 'Unknown Device',
                vid=p.vid,
                pid=p.pid,
                manufacturer=p.manufacturer,
                serial_number=p.serial_number,
                is_esp32=is_esp,
                referenced_in=refs,
            ))

        # Ports referenced in project configs but NOT physically present → used
        for port_name, refs in referenced_ports.items():
            if port_name not in physical_port_names:
                port_infos.append(PortInfo(
                    name=port_name,
                    status='used',
                    description='Referenced in project config (not connected)',
                    vid=None,
                    pid=None,
                    manufacturer=None,
                    serial_number=None,
                    is_esp32=False,
                    referenced_in=refs,
                ))

        # Sort: working → used → reserved, then by port name
        order = {'working': 0, 'used': 1, 'reserved': 2}
        port_infos.sort(key=lambda x: (order.get(x.status, 9), x.name))

        return port_infos
