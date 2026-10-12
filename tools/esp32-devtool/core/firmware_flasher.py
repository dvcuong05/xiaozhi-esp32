import subprocess
import sys
import os
from pathlib import Path


def _find_esptool_python() -> str:
    """
    Find the Python executable that has esptool installed.
    Priority:
      1. The same interpreter running this script (app.py's Python)
      2. Explicit ESPTOOL_PYTHON env var
      3. Fallback to sys.executable
    """
    # Env override
    env_py = os.environ.get('ESPTOOL_PYTHON')
    if env_py and Path(env_py).is_file():
        return env_py

    # Try the current interpreter first (most likely correct when launched via `python app.py`)
    try:
        result = subprocess.run(
            [sys.executable, '-c', 'import esptool; print(esptool.__version__)'],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            return sys.executable
    except Exception:
        pass

    # Search common locations on Windows
    for candidate in [
        r'C:\Users\dvcuong\AppData\Local\Programs\Python\Python313\python.exe',
        r'C:\Users\dvcuong\AppData\Local\Programs\Python\Python312\python.exe',
        r'C:\Python313\python.exe',
    ]:
        if Path(candidate).is_file():
            try:
                r = subprocess.run([candidate, '-c', 'import esptool'],
                                   capture_output=True, timeout=5)
                if r.returncode == 0:
                    return candidate
            except Exception:
                pass

    return sys.executable   # Last resort


_PYTHON = _find_esptool_python()

# Shared env: force UTF-8 and unbuffered output so subprocess streams in real-time
_ENV = {**os.environ, 'PYTHONUNBUFFERED': '1', 'PYTHONIOENCODING': 'utf-8'}


class FirmwareFlasher:

    def get_boards(self) -> list:
        return ['esp32', 'esp32s3', 'esp32c3', 'esp32c6', 'esp32p4', 'esp32s31']

    # ── Public API ─────────────────────────────────────────────────────────────

    def flash(self, port: str, firmware_path: str, chip: str, baud: int, callback) -> None:
        """
        Flash firmware to the chip.
        Intelligently detects:
          1. Pre-merged binaries (flashed at 0x0).
          2. ESP-IDF build folders with flash_args (flashes complete bootloader + partition table + app).
          3. Standalone binaries.
        """
        p = Path(firmware_path)
        parent_dir = p.parent
        flash_args_file = parent_dir / 'flash_args'
        merged_candidate = parent_dir / 'merged-binary.bin'
        named_merged = parent_dir / f"{p.stem}_merged.bin"
        esptool_chip = 'esp32s3' if 's3' in chip.lower() else ('esp32c3' if 'c3' in chip.lower() else ('esp32c6' if 'c6' in chip.lower() else ('esp32p4' if 'p4' in chip.lower() else 'esp32')))

        # Check for merged binary
        target_bin = firmware_path
        if 'merged' in p.name.lower():
            target_bin = firmware_path
        elif named_merged.exists():
            callback(f'[DevTool] Auto-detected merged binary: {named_merged.name} (includes bootloader & partitions)\n')
            target_bin = str(named_merged)
        elif merged_candidate.exists():
            callback(f'[DevTool] Auto-detected merged binary: {merged_candidate.name} (includes bootloader & partitions)\n')
            target_bin = str(merged_candidate)
        elif flash_args_file.exists() and p.name not in ('bootloader.bin', 'partition-table.bin'):
            callback(f'[DevTool] Flashing complete package (bootloader, partition-table, app) via flash_args...\n')
            cmd = [
                _PYTHON, '-m', 'esptool',
                '--chip', esptool_chip,
                '--port', port,
                '--baud', str(baud),
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash',
                '@flash_args',
            ]
            self._run_streaming(cmd, callback, cwd=str(parent_dir))
            return

        # Flash at 0x0
        cmd = [
            _PYTHON, '-m', 'esptool',
            '--chip', esptool_chip,
            '--port', port,
            '--baud', str(baud),
            '--before', 'default_reset',
            '--after', 'hard_reset',
            'write_flash',
            '-z',
            '--flash_mode', 'dio',
            '--flash_freq', '40m',
            '--flash_size', 'detect',
            '0x0', target_bin,
        ]
        self._run_streaming(cmd, callback, cwd=str(parent_dir))

    def erase_flash(self, port: str, chip: str, callback) -> None:
        cmd = [
            _PYTHON, '-m', 'esptool',
            '--chip', chip,
            '--port', port,
            '--before', 'default_reset',
            'erase_flash',
        ]
        self._run_streaming(cmd, callback)

    def read_chip_info(self, port: str) -> dict:
        cmd = [_PYTHON, '-m', 'esptool', '--port', port, 'chip_id']
        try:
            out = subprocess.check_output(cmd, stderr=subprocess.STDOUT,
                                          text=True, env=_ENV, timeout=30)
            return {'status': 'success', 'output': out}
        except subprocess.CalledProcessError as e:
            return {'status': 'error', 'output': e.output or str(e)}
        except Exception as e:
            return {'status': 'error', 'output': str(e)}

    def verify_esptool(self) -> dict:
        """Check that esptool is reachable and return its version."""
        try:
            out = subprocess.check_output(
                [_PYTHON, '-m', 'esptool', 'version'],
                stderr=subprocess.STDOUT, text=True, env=_ENV, timeout=10
            )
            return {'ok': True, 'output': out.strip(), 'python': _PYTHON}
        except Exception as e:
            return {'ok': False, 'output': str(e), 'python': _PYTHON}

    # ── Internal ───────────────────────────────────────────────────────────────

    def _run_streaming(self, cmd: list, callback, cwd: str = None) -> None:
        """
        Run a subprocess and stream every line to callback() in real-time.
        Uses line-buffered mode + PYTHONUNBUFFERED to avoid output getting stuck.
        """
        callback(f'[CMD] {" ".join(cmd)}\n\n')
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,              # line-buffered
                universal_newlines=True,
                env=_ENV,
                cwd=cwd,
            )
            for line in proc.stdout:
                callback(line)
            proc.wait()
            status = 'OK' if proc.returncode == 0 else f'FAILED (exit {proc.returncode})'
            callback(f'\n[DevTool] Process finished: {status}\n')
        except FileNotFoundError:
            callback(f'[DevTool] ERROR: Could not find Python/esptool at: {_PYTHON}\n')
        except Exception as e:
            callback(f'[DevTool] ERROR: {e}\n')
