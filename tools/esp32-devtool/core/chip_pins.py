"""
chip_pins.py — ESP32 family pin layout database.

Each chip entry defines:
  - package: physical package name
  - module:  common dev-module name used as reference
  - gpio_count: number of available GPIO
  - pins: list of PinDef dicts, ordered by position on the module

PinDef fields:
  num   : int   — GPIO number, or -1 for power/GND/NC
  name  : str   — primary pin name shown in diagram
  alt   : list  — alternate function names (UART, I2C, SPI, ADC, etc.)
  type  : str   — 'io' | 'io_input_only' | 'power' | 'gnd' | 'nc'
  side  : str   — 'left' | 'right'  (top-to-bottom order within side)
  tags  : list  — functional groups: 'uart','i2c','spi','adc','touch','jtag','boot'
"""

from typing import TypedDict, List, Optional


class PinDef(TypedDict):
    num:   int
    name:  str
    alt:   List[str]
    type:  str          # 'io' | 'io_input_only' | 'power' | 'gnd' | 'nc'
    side:  str          # 'left' | 'right'
    tags:  List[str]


def _p(num, name, alt, typ='io', side='left', tags=None) -> PinDef:
    return PinDef(num=num, name=name, alt=alt or [], type=typ,
                  side=side, tags=tags or [])


# ─── ESP32 (WROOM/DevKitC 38-pin module layout) ──────────────────────────────
#
# Standard ESP32 DevKitC v4 pinout.  Left pins top→bottom, right pins top→bottom.
# GND/3V3 pins at each end omitted for visual clarity in compact view but
# included here for completeness.

_ESP32_PINS: List[PinDef] = [
    # ── Left side (top → bottom) ──────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'left'),
    _p(-1,  '3V3',    [],                    'power','left'),
    _p(-1,  'EN',     ['RESET'],             'nc',   'left'),
    _p(36,  'GPIO36', ['ADC1_CH0','SVP'],    'io_input_only','left', ['adc']),
    _p(39,  'GPIO39', ['ADC1_CH3','SVN'],    'io_input_only','left', ['adc']),
    _p(34,  'GPIO34', ['ADC1_CH6'],          'io_input_only','left', ['adc']),
    _p(35,  'GPIO35', ['ADC1_CH7'],          'io_input_only','left', ['adc']),
    _p(32,  'GPIO32', ['ADC1_CH4','TOUCH9'], 'io','left', ['adc','touch']),
    _p(33,  'GPIO33', ['ADC1_CH5','TOUCH8'], 'io','left', ['adc','touch']),
    _p(25,  'GPIO25', ['ADC2_CH8','DAC1'],   'io','left', ['adc']),
    _p(26,  'GPIO26', ['ADC2_CH9','DAC2'],   'io','left', ['adc']),
    _p(27,  'GPIO27', ['ADC2_CH7','TOUCH7'], 'io','left', ['adc','touch']),
    _p(14,  'GPIO14', ['ADC2_CH6','TOUCH6','HSPI_CLK'], 'io','left', ['adc','touch','spi']),
    _p(12,  'GPIO12', ['ADC2_CH5','TOUCH5','HSPI_MISO'],'io','left', ['adc','touch','spi']),
    _p(-1,  'GND',    [],                    'gnd',  'left'),
    _p(13,  'GPIO13', ['ADC2_CH4','TOUCH4','HSPI_MOSI'],'io','left', ['adc','touch','spi']),
    _p(9,   'GPIO9',  ['SD2','U1RXD'],       'io','left', ['uart']),
    _p(10,  'GPIO10', ['SD3'],               'io','left'),
    _p(11,  'GPIO11', ['CMD'],               'io','left'),
    # ── Right side (top → bottom) ─────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'right'),
    _p(-1,  '5V',     [],                    'power','right'),
    _p(23,  'GPIO23', ['VSPI_MOSI'],         'io','right', ['spi']),
    _p(22,  'GPIO22', ['SCL','VSPI_CLK'],    'io','right', ['i2c','spi']),
    _p(1,   'GPIO1',  ['U0TXD','CLK_OUT3'], 'io','right', ['uart']),
    _p(3,   'GPIO3',  ['U0RXD','CLK_OUT2'], 'io','right', ['uart']),
    _p(21,  'GPIO21', ['SDA'],               'io','right', ['i2c']),
    _p(-1,  'GND',    [],                    'gnd',  'right'),
    _p(19,  'GPIO19', ['VSPI_MISO'],         'io','right', ['spi']),
    _p(18,  'GPIO18', ['VSPI_CLK'],          'io','right', ['spi']),
    _p(5,   'GPIO5',  ['VSPI_CS','TOUCH5'],  'io','right', ['spi','touch','boot']),
    _p(17,  'GPIO17', ['U2TXD'],             'io','right', ['uart']),
    _p(16,  'GPIO16', ['U2RXD'],             'io','right', ['uart']),
    _p(4,   'GPIO4',  ['ADC2_CH0','TOUCH0'], 'io','right', ['adc','touch']),
    _p(0,   'GPIO0',  ['ADC2_CH1','TOUCH1','BOOT'],'io','right', ['adc','touch','boot']),
    _p(2,   'GPIO2',  ['ADC2_CH2','TOUCH2'], 'io','right', ['adc','touch']),
    _p(15,  'GPIO15', ['ADC2_CH3','TOUCH3','HSPI_CS'],'io','right', ['adc','touch','spi']),
    _p(8,   'GPIO8',  ['SD1'],               'io','right'),
    _p(7,   'GPIO7',  ['SD0'],               'io','right'),
    _p(6,   'GPIO6',  ['CLK'],               'io','right'),
]


# ─── ESP32-S3 (WROOM-1 / DevKitC-1 38-pin module layout) ────────────────────

_ESP32S3_PINS: List[PinDef] = [
    # ── Left side ─────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                       'gnd',  'left'),
    _p(-1,  '3V3',    [],                       'power','left'),
    _p(-1,  'EN',     ['RESET'],                'nc',   'left'),
    _p(4,   'GPIO4',  ['ADC1_CH3','TOUCH4'],    'io','left', ['adc','touch']),
    _p(5,   'GPIO5',  ['ADC1_CH4','TOUCH5'],    'io','left', ['adc','touch']),
    _p(6,   'GPIO6',  ['ADC1_CH5','TOUCH6'],    'io','left', ['adc','touch']),
    _p(7,   'GPIO7',  ['ADC1_CH6','TOUCH7'],    'io','left', ['adc','touch']),
    _p(15,  'GPIO15', ['ADC2_CH4','TOUCH15'],   'io','left', ['adc','touch']),
    _p(16,  'GPIO16', ['ADC2_CH5','TOUCH16'],   'io','left', ['adc','touch']),
    _p(17,  'GPIO17', ['ADC2_CH6','TOUCH17'],   'io','left', ['adc','touch','uart']),
    _p(18,  'GPIO18', ['ADC2_CH7','TOUCH18'],   'io','left', ['adc','touch']),
    _p(8,   'GPIO8',  ['ADC1_CH7','TOUCH8'],    'io','left', ['adc','touch','i2c']),
    _p(3,   'GPIO3',  ['ADC1_CH2','TOUCH3'],    'io','left', ['adc','touch']),
    _p(46,  'GPIO46', ['STRAPPING'],            'io','left'),
    _p(9,   'GPIO9',  ['ADC1_CH8','TOUCH9'],    'io','left', ['adc','touch']),
    _p(10,  'GPIO10', ['ADC1_CH9','TOUCH10'],   'io','left', ['adc','touch']),
    _p(11,  'GPIO11', ['TOUCH11'],              'io','left', ['touch','spi']),
    _p(12,  'GPIO12', ['TOUCH12'],              'io','left', ['touch','spi']),
    _p(13,  'GPIO13', ['TOUCH13'],              'io','left', ['touch','spi']),
    # ── Right side ────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                       'gnd',  'right'),
    _p(43,  'GPIO43', ['U0TXD'],                'io','right', ['uart']),
    _p(44,  'GPIO44', ['U0RXD'],                'io','right', ['uart']),
    _p(1,   'GPIO1',  ['ADC1_CH0','TOUCH1'],    'io','right', ['adc','touch']),
    _p(2,   'GPIO2',  ['ADC1_CH1','TOUCH2'],    'io','right', ['adc','touch']),
    _p(42,  'GPIO42', ['MTMS'],                 'io','right', ['jtag']),
    _p(41,  'GPIO41', ['MTDI'],                 'io','right', ['jtag']),
    _p(40,  'GPIO40', ['MTDO'],                 'io','right', ['jtag']),
    _p(39,  'GPIO39', ['MTCK'],                 'io','right', ['jtag']),
    _p(38,  'GPIO38', [],                       'io','right'),
    _p(37,  'GPIO37', ['SPIDQS'],               'io','right'),
    _p(36,  'GPIO36', ['SPIIO7'],               'io','right'),
    _p(35,  'GPIO35', ['SPIIO6'],               'io','right'),
    _p(0,   'GPIO0',  ['BOOT'],                 'io','right', ['boot']),
    _p(45,  'GPIO45', ['STRAPPING'],            'io','right'),
    _p(48,  'GPIO48', ['NEOPIXEL'],             'io','right'),
    _p(47,  'GPIO47', [],                       'io','right'),
    _p(21,  'GPIO21', [],                       'io','right', ['i2c']),
    _p(20,  'GPIO20', ['ADC2_CH9','USB_D+'],    'io','right', ['adc']),
    _p(19,  'GPIO19', ['ADC2_CH8','USB_D-'],    'io','right', ['adc']),
]


# ─── ESP32-S3 Super Mini (Ultra-compact dual-row breadboard module) ───────────
#
# Common pinout for ESP32-S3 SuperMini / Zero clone:
# 16-18 broken-out GPIOs, on-board RGB WS2812 on GPIO 48, BOOT on GPIO 0, native USB on 19/20.

_ESP32S3_SUPERMINI_PINS: List[PinDef] = [
    # ── Left side (top → bottom) ──────────────────────────────────────────
    _p(-1,  '5V',     ['VBUS'],             'power','left'),
    _p(-1,  'GND',    [],                   'gnd',  'left'),
    _p(-1,  '3V3',    [],                   'power','left'),
    _p(1,   'GPIO1',  ['ADC1_CH0','TOUCH1'],'io',   'left', ['adc','touch']),
    _p(2,   'GPIO2',  ['ADC1_CH1','TOUCH2'],'io',   'left', ['adc','touch']),
    _p(3,   'GPIO3',  ['ADC1_CH2','TOUCH3'],'io',   'left', ['adc','touch']),
    _p(4,   'GPIO4',  ['ADC1_CH3','TOUCH4'],'io',   'left', ['adc','touch']),
    _p(5,   'GPIO5',  ['ADC1_CH4','TOUCH5'],'io',   'left', ['adc','touch']),
    _p(6,   'GPIO6',  ['ADC1_CH5','TOUCH6'],'io',   'left', ['adc','touch']),
    _p(7,   'GPIO7',  ['ADC1_CH6','TOUCH7'],'io',   'left', ['adc','touch']),
    _p(8,   'GPIO8',  ['ADC1_CH7','TOUCH8'],'io',   'left', ['adc','touch','i2c']),
    _p(9,   'GPIO9',  ['ADC1_CH8','TOUCH9'],'io',   'left', ['adc','touch']),
    # ── Right side (top → bottom) ─────────────────────────────────────────
    _p(10,  'GPIO10', ['TOUCH10'],          'io',   'right', ['touch']),
    _p(11,  'GPIO11', ['TOUCH11','FSPI'],   'io',   'right', ['touch','spi']),
    _p(12,  'GPIO12', ['TOUCH12','FSPI'],   'io',   'right', ['touch','spi']),
    _p(13,  'GPIO13', ['TOUCH13','FSPI'],   'io',   'right', ['touch','spi']),
    _p(14,  'GPIO14', ['TOUCH14','FSPI'],   'io',   'right', ['touch','spi']),
    _p(21,  'GPIO21', ['I2C_SDA'],          'io',   'right', ['i2c']),
    _p(47,  'GPIO47', [],                   'io',   'right'),
    _p(48,  'GPIO48', ['RGB_WS2812'],       'io',   'right'),
    _p(43,  'GPIO43', ['U0TXD'],            'io',   'right', ['uart']),
    _p(44,  'GPIO44', ['U0RXD'],            'io',   'right', ['uart']),
    _p(-1,  'GND',    [],                   'gnd',  'right'),
    _p(0,   'GPIO0',  ['BOOT_BTN'],         'io',   'right', ['boot']),
]


# ─── ESP32-C3 (WROOM-02 / DevKitM-1 22 GPIO, compact module) ────────────────

_ESP32C3_PINS: List[PinDef] = [
    # ── Left side ─────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'left'),
    _p(-1,  '3V3',    [],                    'power','left'),
    _p(-1,  'EN',     ['RESET'],             'nc',   'left'),
    _p(4,   'GPIO4',  ['ADC1_CH4','MTCK'],   'io','left', ['adc','jtag']),
    _p(5,   'GPIO5',  ['ADC2_CH0','MTDO'],   'io','left', ['adc','jtag']),
    _p(6,   'GPIO6',  ['MTDI','HSPI_MISO'],  'io','left', ['spi','jtag']),
    _p(7,   'GPIO7',  ['MTMS','HSPI_MOSI'],  'io','left', ['spi','jtag']),
    _p(8,   'GPIO8',  ['RXD0'],              'io','left', ['uart']),
    _p(9,   'GPIO9',  ['TXD0','BOOT'],       'io','left', ['uart','boot']),
    _p(10,  'GPIO10', ['FSPI_CS0'],          'io','left', ['spi']),
    # ── Right side ────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'right'),
    _p(21,  'GPIO21', ['U0TXD'],             'io','right', ['uart']),
    _p(20,  'GPIO20', ['U0RXD','USB_D+'],    'io','right', ['uart']),
    _p(19,  'GPIO19', ['USB_D-'],            'io','right'),
    _p(18,  'GPIO18', ['USB D-','CLK'],      'io','right'),
    _p(3,   'GPIO3',  ['ADC1_CH3'],          'io','right', ['adc']),
    _p(2,   'GPIO2',  ['ADC1_CH2','FSPIQ'],  'io','right', ['adc','spi']),
    _p(1,   'GPIO1',  ['ADC1_CH1','FSPID'],  'io','right', ['adc','spi']),
    _p(0,   'GPIO0',  ['ADC1_CH0','BOOT'],   'io','right', ['adc','boot']),
]


# ─── ESP32-C6 (DevKitC-1 30 GPIO) ───────────────────────────────────────────

_ESP32C6_PINS: List[PinDef] = [
    # ── Left side ─────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'left'),
    _p(-1,  '3V3',    [],                    'power','left'),
    _p(-1,  'EN',     ['RESET'],             'nc',   'left'),
    _p(4,   'GPIO4',  ['ADC1_CH4','MTCK'],   'io','left', ['adc','jtag']),
    _p(5,   'GPIO5',  ['ADC1_CH5','MTDO'],   'io','left', ['adc','jtag']),
    _p(6,   'GPIO6',  ['ADC1_CH6','MTDI'],   'io','left', ['adc','jtag']),
    _p(7,   'GPIO7',  ['ADC1_CH7','MTMS'],   'io','left', ['adc','jtag']),
    _p(0,   'GPIO0',  ['ADC1_CH0'],          'io','left', ['adc']),
    _p(1,   'GPIO1',  ['ADC1_CH1'],          'io','left', ['adc']),
    _p(2,   'GPIO2',  ['ADC1_CH2'],          'io','left', ['adc']),
    _p(3,   'GPIO3',  ['ADC1_CH3'],          'io','left', ['adc']),
    _p(15,  'GPIO15', ['XTAL32K_N'],         'io','left'),
    _p(14,  'GPIO14', ['XTAL32K_P'],         'io','left'),
    _p(8,   'GPIO8',  [],                    'io','left'),
    _p(9,   'GPIO9',  ['BOOT'],              'io','left', ['boot']),
    # ── Right side ────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'right'),
    _p(23,  'GPIO23', ['U0TXD'],             'io','right', ['uart']),
    _p(22,  'GPIO22', ['U0RXD'],             'io','right', ['uart']),
    _p(21,  'GPIO21', ['USB_D+'],            'io','right'),
    _p(20,  'GPIO20', ['USB_D-'],            'io','right'),
    _p(19,  'GPIO19', ['SPI_CS1'],           'io','right', ['spi']),
    _p(18,  'GPIO18', ['SPI_WP'],            'io','right', ['spi']),
    _p(10,  'GPIO10', ['SPI_CLK'],           'io','right', ['spi']),
    _p(11,  'GPIO11', ['SPI_MISO'],          'io','right', ['spi']),
    _p(12,  'GPIO12', ['SPI_MOSI'],          'io','right', ['spi']),
    _p(13,  'GPIO13', ['SPI_HD'],            'io','right', ['spi']),
    _p(16,  'GPIO16', ['U1TXD'],             'io','right', ['uart']),
    _p(17,  'GPIO17', ['U1RXD'],             'io','right', ['uart']),
    _p(24,  'GPIO24', ['SDIO_CLK'],          'io','right'),
    _p(25,  'GPIO25', ['SDIO_CMD'],          'io','right'),
]


# ─── ESP32-P4 (DevKit 54+ GPIO, using common subset) ────────────────────────

_ESP32P4_PINS: List[PinDef] = [
    # ── Left side ─────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'left'),
    _p(-1,  '3V3',    [],                    'power','left'),
    _p(-1,  '5V',     [],                    'power','left'),
    _p(-1,  'EN',     ['RESET'],             'nc',   'left'),
    _p(0,   'GPIO0',  ['ADC1_CH0'],          'io','left', ['adc']),
    _p(1,   'GPIO1',  ['ADC1_CH1'],          'io','left', ['adc']),
    _p(2,   'GPIO2',  ['ADC1_CH2'],          'io','left', ['adc']),
    _p(3,   'GPIO3',  ['ADC1_CH3'],          'io','left', ['adc']),
    _p(4,   'GPIO4',  ['ADC1_CH4'],          'io','left', ['adc']),
    _p(5,   'GPIO5',  ['ADC1_CH5'],          'io','left', ['adc']),
    _p(6,   'GPIO6',  [],                    'io','left'),
    _p(7,   'GPIO7',  [],                    'io','left'),
    _p(8,   'GPIO8',  ['SDA','I2C_SCL'],     'io','left', ['i2c']),
    _p(9,   'GPIO9',  ['SCL','I2C_SDA'],     'io','left', ['i2c']),
    _p(10,  'GPIO10', ['SPI_CS'],            'io','left', ['spi']),
    _p(11,  'GPIO11', ['SPI_CLK'],           'io','left', ['spi']),
    _p(12,  'GPIO12', ['SPI_MOSI'],          'io','left', ['spi']),
    _p(13,  'GPIO13', ['SPI_MISO'],          'io','left', ['spi']),
    _p(14,  'GPIO14', ['U0TXD'],             'io','left', ['uart']),
    _p(15,  'GPIO15', ['U0RXD'],             'io','left', ['uart']),
    _p(16,  'GPIO16', ['U1TXD'],             'io','left', ['uart']),
    _p(17,  'GPIO17', ['U1RXD'],             'io','left', ['uart']),
    # ── Right side ────────────────────────────────────────────────────────
    _p(-1,  'GND',    [],                    'gnd',  'right'),
    _p(35,  'GPIO35', ['MTCK'],              'io','right', ['jtag']),
    _p(36,  'GPIO36', ['MTDO'],              'io','right', ['jtag']),
    _p(37,  'GPIO37', ['MTDI'],              'io','right', ['jtag']),
    _p(38,  'GPIO38', ['MTMS'],              'io','right', ['jtag']),
    _p(18,  'GPIO18', [],                    'io','right'),
    _p(19,  'GPIO19', [],                    'io','right'),
    _p(20,  'GPIO20', [],                    'io','right'),
    _p(21,  'GPIO21', [],                    'io','right'),
    _p(22,  'GPIO22', [],                    'io','right'),
    _p(23,  'GPIO23', [],                    'io','right'),
    _p(24,  'GPIO24', [],                    'io','right'),
    _p(25,  'GPIO25', [],                    'io','right'),
    _p(26,  'GPIO26', [],                    'io','right'),
    _p(27,  'GPIO27', [],                    'io','right'),
    _p(28,  'GPIO28', [],                    'io','right'),
    _p(29,  'GPIO29', [],                    'io','right'),
    _p(30,  'GPIO30', [],                    'io','right'),
    _p(31,  'GPIO31', [],                    'io','right'),
    _p(32,  'GPIO32', [],                    'io','right'),
    _p(33,  'GPIO33', [],                    'io','right'),
    _p(34,  'GPIO34', [],                    'io','right'),
    _p(49,  'GPIO49', ['USB_D+'],            'io','right'),
    _p(50,  'GPIO50', ['USB_D-'],            'io','right'),
    _p(51,  'GPIO51', [],                    'io','right'),
    _p(52,  'GPIO52', [],                    'io','right'),
    _p(53,  'GPIO53', [],                    'io','right'),
    _p(54,  'GPIO54', [],                    'io','right'),
]


# ─── Master registry ─────────────────────────────────────────────────────────

CHIPS: dict = {
    'esp32': {
        'label':       'ESP32',
        'package':     'QFN48 / WROOM-32',
        'gpio_count':  34,
        'pins':        _ESP32_PINS,
    },
    'esp32s3': {
        'label':       'ESP32-S3',
        'package':     'QFN56 / WROOM-1',
        'gpio_count':  45,
        'pins':        _ESP32S3_PINS,
    },
    'esp32s3_supermini': {
        'label':       'ESP32-S3 Super Mini',
        'package':     'SuperMini DIL-24',
        'gpio_count':  18,
        'pins':        _ESP32S3_SUPERMINI_PINS,
    },
    'esp32s31': {
        'label':       'ESP32-S3 (cam)',
        'package':     'QFN56 / WROOM-1',
        'gpio_count':  45,
        'pins':        _ESP32S3_PINS,   # same silicon
    },
    'esp32c3': {
        'label':       'ESP32-C3',
        'package':     'QFN32 / WROOM-02',
        'gpio_count':  22,
        'pins':        _ESP32C3_PINS,
    },
    'esp32c6': {
        'label':       'ESP32-C6',
        'package':     'QFN40 / WROOM-1',
        'gpio_count':  30,
        'pins':        _ESP32C6_PINS,
    },
    'esp32p4': {
        'label':       'ESP32-P4',
        'package':     'QFN100',
        'gpio_count':  54,
        'pins':        _ESP32P4_PINS,
    },
}


def get_chip(chip_id: str) -> Optional[dict]:
    """Return chip definition or None if unknown."""
    k = chip_id.lower().replace('-', '_')
    if 'supermini' in k or 'super_mini' in k:
        return CHIPS.get('esp32s3_supermini')
    return CHIPS.get(chip_id.lower().replace('-', '')) or CHIPS.get(k)


def get_gpio_pins(chip_id: str) -> List[PinDef]:
    """Return only the IO pins (num >= 0) for a chip."""
    chip = get_chip(chip_id)
    if not chip:
        return []
    return [p for p in chip['pins'] if p['num'] >= 0]


def list_chips() -> List[dict]:
    """Return summary list of all chips (without full pin data)."""
    return [
        {'id': k, 'label': v['label'], 'package': v['package'], 'gpio_count': v['gpio_count']}
        for k, v in CHIPS.items()
    ]
