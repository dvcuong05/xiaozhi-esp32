"""
gpio_analyzer.py — Scan the xiaozhi-esp32 project source to identify which
GPIO pins are referenced in firmware code and configuration files.

Supported patterns (C/C++ and KConfig):
  #define AUDIO_I2S_GPIO_BCLK   GPIO_NUM_17
  #define MY_PIN                18
  CONFIG_EXAMPLE_GPIO=12
  gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT)
  .pin_bit_mask = (1ULL << GPIO_NUM_5)
  gpio_config_t: .pin_bit_mask = BIT(21)
"""

import os
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional
from pathlib import Path


@dataclass
class GpioRef:
    gpio:   int
    macro:  str           # the #define name or pattern matched
    file:   str           # relative path from project root
    line:   int
    detail: str = ''      # raw matched line (trimmed)


# ─── Regex patterns ───────────────────────────────────────────────────────────

# Pattern 1: #define SOME_NAME GPIO_NUM_X
_RE_DEFINE_GPIO_NUM = re.compile(
    r'#define\s+(\w+)\s+GPIO_NUM_(\d+)',
    re.MULTILINE
)

# Pattern 2: #define SOME_PIN_NAME  <integer>  (only for names containing PIN/GPIO/SCL/SDA/…)
_RE_DEFINE_INT_PIN = re.compile(
    r'#define\s+((?:\w*(?:GPIO|PIN|SCL|SDA|CLK|BCLK|LRCK|MOSI|MISO|DOUT|DIN|WS|SCK|CS|INT|RST|RESET|IRQ|LED|BTN|BUTTON)\w*))\s+(\d+)\b',
    re.IGNORECASE | re.MULTILINE
)

# Pattern 3: gpio_set_direction(GPIO_NUM_X, …)
_RE_CALL_GPIO_NUM = re.compile(
    r'gpio_\w+\s*\(\s*GPIO_NUM_(\d+)',
    re.MULTILINE
)

# Pattern 4: (1ULL << GPIO_NUM_X) or (1ULL << X) in pin_bit_mask context
_RE_BIT_SHIFT_GPIO = re.compile(
    r'\(1ULL?\s*<<\s*GPIO_NUM_(\d+)\)',
    re.MULTILINE
)
_RE_BIT_SHIFT_INT = re.compile(
    r'pin_bit_mask[^;]*\(1ULL?\s*<<\s*(\d+)\)',
    re.MULTILINE
)

# Pattern 5: sdkconfig: CONFIG_SOMETHING_GPIO=12
_RE_SDKCONFIG = re.compile(
    r'^(CONFIG_\w*(?:GPIO|PIN)\w*)\s*=\s*(\d+)',
    re.MULTILINE
)

# Pattern 6: bare GPIO_NUM_X anywhere
_RE_BARE_GPIO_NUM = re.compile(r'\bGPIO_NUM_(\d+)\b')

# Macro names to ignore (architecture limits / masks / ports, not physical pin assignments)
_IGNORED_MACRO_SUBSTRINGS = (
    'CONFIG_SOC_',
    '_MASK',
    '_COUNT',
    '_MAX',
    '_MIN',
    '_PORT',
    '_OFFSET',
    '_RANGE',
)


class GpioAnalyzer:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)

    # ── Public ────────────────────────────────────────────────────────────────

    def analyze(self, board_dir: Optional[str] = None) -> Dict[int, List[GpioRef]]:
        """
        Scan the project for GPIO references.

        If board_dir is given (e.g. 'bread-compact-wifi' or 'alientek/atk-dnesp32s3'),
        scan that board's directory AND project sdkconfig. Otherwise scan all boards.

        Returns: { gpio_num: [GpioRef, ...] }
        """
        result: Dict[int, List[GpioRef]] = {}

        # Always scan active sdkconfig (only sdkconfig, not .old or all defaults unless needed)
        for name in ('sdkconfig', 'sdkconfig.defaults'):
            p = self.project_root / name
            if p.is_file():
                self._scan_file(p, result, patterns=['sdkconfig'])

        # Scan board directory
        boards_root = self.project_root / 'main' / 'boards'
        targets = []

        if board_dir:
            board_clean = board_dir.strip('/\\').replace('\\', '/')
            # 1. Exact path under boards_root
            cand = boards_root / board_clean
            if cand.is_dir():
                targets = [cand]
            else:
                # 2. Match relative path or exact folder name
                for d in boards_root.rglob('*'):
                    if d.is_dir():
                        rel = str(d.relative_to(boards_root)).replace('\\', '/')
                        if rel.lower() == board_clean.lower() or d.name.lower() == board_clean.lower():
                            targets = [d]
                            break
        else:
            targets = [boards_root]

        for tgt in targets:
            for ext in ('*.h', '*.c', '*.cc', '*.cpp'):
                for fpath in tgt.rglob(ext):
                    self._scan_file(fpath, result)

        return result

    def get_used_gpios(self, board_dir: Optional[str] = None) -> set:
        """Return a set of GPIO numbers referenced in the project."""
        return set(self.analyze(board_dir).keys())

    def get_board_list(self) -> List[dict]:
        """Return list of boards with auto-detected chip target."""
        boards_root = self.project_root / 'main' / 'boards'
        result = []
        for config in sorted(boards_root.rglob('config.h')):
            board_dir = config.parent
            rel = board_dir.relative_to(boards_root)
            chip = self._detect_chip(board_dir)
            result.append({
                'id':    str(rel).replace(os.sep, '/'),
                'label': board_dir.name,
                'path':  str(board_dir),
                'chip':  chip,
            })
        return result

    # ── Private ───────────────────────────────────────────────────────────────

    def _detect_chip(self, board_dir: Path) -> str:
        """
        Guess chip target from board directory content.
        Looks for IDF_TARGET in cmake files, or chip keywords in filenames/content.
        """
        # Check CMakeLists.txt in board dir or parent
        for cmake in (board_dir / 'CMakeLists.txt', board_dir.parent / 'CMakeLists.txt'):
            if cmake.exists():
                txt = cmake.read_text(errors='ignore')
                m = re.search(r'set\s*\(\s*IDF_TARGET\s+"?(esp32\w*)"?', txt, re.IGNORECASE)
                if m:
                    return m.group(1).lower()

        # Check idf_component.yml or config.json hints
        for f in board_dir.glob('*.yml'):
            txt = f.read_text(errors='ignore')
            for chip in ('esp32s3', 'esp32s31', 'esp32c3', 'esp32c6', 'esp32p4', 'esp32'):
                if chip in txt.lower():
                    return chip

        # Infer from board directory name
        name = str(board_dir).lower()
        for chip in ('esp32s3', 'esp32s31', 'esp32c3', 'esp32c6', 'esp32p4'):
            if chip.replace('esp32', '') in name or chip in name:
                return chip

        # Fallback: look at the config.h for clues
        config_h = board_dir / 'config.h'
        if config_h.exists():
            txt = config_h.read_text(errors='ignore').lower()
            for chip in ('esp32s3', 'esp32c3', 'esp32c6', 'esp32p4'):
                if chip in txt:
                    return chip

        return 'esp32'  # default

    def _scan_file(self, fpath: Path, result: Dict[int, List[GpioRef]],
                   patterns: List[str] = None):
        try:
            text = fpath.read_text(encoding='utf-8', errors='ignore')
        except Exception:
            return

        rel = str(fpath.relative_to(self.project_root)).replace(os.sep, '/')
        lines = text.splitlines()

        def add(gpio: int, macro: str, line_num: int, detail: str = ''):
            if gpio < 0 or gpio > 60:   # sanity check
                return
            macro_upper = macro.upper()
            if any(ign in macro_upper for ign in _IGNORED_MACRO_SUBSTRINGS):
                return
            ref = GpioRef(gpio=gpio, macro=macro, file=rel,
                          line=line_num + 1, detail=detail.strip()[:120])
            if gpio not in result:
                result[gpio] = []
            # deduplicate by (file, line)
            if not any(r.file == rel and r.line == ref.line for r in result[gpio]):
                result[gpio].append(ref)

        # ── sdkconfig pattern ──────────────────────────────────────────────
        if patterns and 'sdkconfig' in patterns:
            for m in _RE_SDKCONFIG.finditer(text):
                gpio = int(m.group(2))
                lnum = text[:m.start()].count('\n')
                add(gpio, m.group(1), lnum, m.group(0))
            return

        # ── C/C++ patterns ─────────────────────────────────────────────────
        # Pass 1: line-by-line for accurate line numbers
        for i, line in enumerate(lines):
            stripped = line.strip()
            if not stripped or stripped.startswith('//'):
                continue

            # #define NAME GPIO_NUM_X
            for m in _RE_DEFINE_GPIO_NUM.finditer(line):
                add(int(m.group(2)), m.group(1), i, stripped)

            # #define PIN_NAME integer
            for m in _RE_DEFINE_INT_PIN.finditer(line):
                add(int(m.group(2)), m.group(1), i, stripped)

            # gpio_xxx(GPIO_NUM_X
            for m in _RE_CALL_GPIO_NUM.finditer(line):
                add(int(m.group(1)), f'gpio_call@{i+1}', i, stripped)

            # (1ULL << GPIO_NUM_X)
            for m in _RE_BIT_SHIFT_GPIO.finditer(line):
                add(int(m.group(1)), f'pin_bit_mask', i, stripped)

            # (1ULL << X) in pin_bit_mask lines
            for m in _RE_BIT_SHIFT_INT.finditer(line):
                add(int(m.group(1)), 'pin_bit_mask', i, stripped)

            # Bare GPIO_NUM_X (catch-all, lower priority)
            for m in _RE_BARE_GPIO_NUM.finditer(line):
                gpio = int(m.group(1))
                # Only add if not already found by more specific patterns
                if gpio not in result or not any(r.file == rel and r.line == i+1
                                                  for r in result.get(gpio, [])):
                    add(gpio, f'GPIO_NUM_{gpio}', i, stripped)
