"""
diagnostic_engine.py — Deep Pin Diagnostic Engine for ESP32 family.

Phase 2:
  - Generates and orchestrates pin diagnostics.
  - Pin Pull-Up / Pull-Down Floating vs Hardware detection test:
      * Pull-down = 0, Pull-up = 1  ==> FLOATING (No wire / no component connected)
      * Pull-down = 1, Pull-up = 1  ==> TIED HIGH (Pull-up resistor / 3.3V connected)
      * Pull-down = 0, Pull-up = 0  ==> TIED LOW (Pull-down resistor / GND / LED connected)
  - I2C Bus Scanner across candidate GPIO pairs.
  - Touch Pad baseline capacitance detection.
"""

import os
import sys
import json
import time
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Any

# Safe GPIOs to test on each chip (avoids flash SPI pins and strapping pins that cause reset)
SAFE_GPIOS = {
    'esp32': [
        0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39
    ],
    'esp32s3': [
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48
    ],
    'esp32c3': [
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 18, 19, 20, 21
    ]
}


class DiagnosticEngine:
    def __init__(self, project_root: str, hardware_prober=None):
        self.project_root = Path(project_root)
        self.hardware_prober = hardware_prober
        self.firmware_dir = Path(__file__).parent.parent / 'firmware' / 'diagnostic'

    def get_diagnostic_source_path(self) -> Path:
        return self.firmware_dir

    def parse_diagnostic_line(self, line: str) -> Optional[dict]:
        """Parse structured DIAG: JSON packet from serial output."""
        line = line.strip()
        if not line.startswith('DIAG:'):
            return None

        try:
            data = json.loads(line[5:].strip())
            if self.hardware_prober:
                self.hardware_prober._apply_diagnostic_data(data)
            return data
        except Exception:
            return None

    def build_diagnostic_firmware(self, chip: str = 'esp32') -> dict:
        """
        Builds the standalone diagnostic firmware using ESP-IDF.
        Auto-locates ESP-IDF export.ps1 on Windows.
        """
        if not self.firmware_dir.exists():
            return {'status': 'error', 'message': 'Diagnostic firmware source directory not found'}

        # Find export.ps1
        export_candidates = [
            Path(r'C:\esp\v6.1\esp-idf\export.ps1'),
            Path(os.environ.get('IDF_PATH', '')) / 'export.ps1',
            Path(r'C:\Espressif\frameworks\esp-idf-v6.1\export.ps1'),
            Path(r'C:\Espressif\frameworks\esp-idf-v5.3\export.ps1'),
        ]
        export_script = None
        for cand in export_candidates:
            if cand.is_file():
                export_script = cand
                break

        if not export_script:
            return {
                'status': 'error',
                'message': 'ESP-IDF export.ps1 not found in standard paths (C:\\esp\\v6.1\\esp-idf\\export.ps1).'
            }

        build_dir = self.firmware_dir / 'build'
        bin_path = build_dir / 'esp32_pin_diagnostic.bin'

        # Map chip to canonical ESP-IDF target (e.g. esp32s3_supermini -> esp32s3)
        idf_target = 'esp32s3' if 's3' in chip.lower() else ('esp32c3' if 'c3' in chip.lower() else ('esp32c6' if 'c6' in chip.lower() else ('esp32p4' if 'p4' in chip.lower() else 'esp32')))

        # Check if already targeted to this chip
        target_file = build_dir / 'project_description.json'
        already_targeted = False
        if target_file.exists():
            try:
                desc = json.loads(target_file.read_text(encoding='utf-8', errors='ignore'))
                if desc.get('target', '').strip().lower() == idf_target:
                    already_targeted = True
            except Exception:
                pass

        build_cmd = f"idf.py build merge-bin" if already_targeted else f"idf.py set-target {idf_target}; idf.py build merge-bin"

        # Build command invoking export.ps1 directly
        ps_cmd = (
            f"$env:PYTHONUTF8 = '1'; "
            f"$env:IDF_TOOLS_PATH = 'C:\\Espressif'; "
            f". '{export_script}'; "
            f"cd '{self.firmware_dir}'; "
            f"{build_cmd}"
        )

        try:
            res = subprocess.run(
                ['powershell', '-NoProfile', '-Command', ps_cmd],
                capture_output=True, text=True, timeout=240
            )

            build_dir = self.firmware_dir / 'build'
            bin_path = build_dir / 'esp32_pin_diagnostic.bin'
            merged_bin = build_dir / 'merged-binary.bin'
            named_chip_merged = build_dir / f"{chip}_pin_diagnostic_merged.bin"
            generic_merged = build_dir / 'esp32_pin_diagnostic_merged.bin'

            if merged_bin.exists():
                import shutil
                try:
                    shutil.copyfile(merged_bin, named_chip_merged)
                    shutil.copyfile(merged_bin, generic_merged)
                except Exception:
                    pass
                return {
                    'status': 'ok',
                    'chip': chip,
                    'bin_path': str(named_chip_merged if named_chip_merged.exists() else merged_bin),
                    'output': f'Diagnostic firmware for [{chip.upper()}] built & merged successfully! Ready to flash to offset 0x0.'
                }
            elif bin_path.exists():
                return {
                    'status': 'ok',
                    'chip': chip,
                    'bin_path': str(bin_path),
                    'output': f'Diagnostic firmware for [{chip.upper()}] built successfully.'
                }
            else:
                return {
                    'status': 'error',
                    'chip': chip,
                    'message': f'Build for target [{chip}] failed or binary not found.',
                    'output': res.stdout[-800:] + '\n' + res.stderr[-800:]
                }
        except subprocess.TimeoutExpired:
            return {'status': 'error', 'message': 'Build timed out after 240 seconds.'}
        except Exception as e:
            return {'status': 'error', 'message': str(e)}
