"""Send a command and/or capture the D1 Mini UART without entering bootloader."""
import argparse
from pathlib import Path
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", required=True)
parser.add_argument("--seconds", type=float, default=20)
parser.add_argument("--command", action="append", default=[])
parser.add_argument("--reset", action="store_true")
parser.add_argument("--log", type=Path)
args = parser.parse_args()
if not 0 < args.seconds <= 600:
    parser.error("--seconds must be between 0 and 600")

uart = serial.Serial(port=None, baudrate=115200, timeout=0.2)
uart.dtr = False
uart.rts = False
uart.port = args.port
uart.open()
log = args.log.open("ab") if args.log else None
try:
    if args.reset:
        uart.rts = True
        time.sleep(0.15)
        uart.rts = False
        time.sleep(2)
    for command in args.command:
        uart.write((command + "\n").encode("ascii"))
        uart.flush()
        time.sleep(0.15)
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        chunk = uart.read(uart.in_waiting or 1)
        if chunk:
            if log:
                log.write(chunk)
                log.flush()
            print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
finally:
    uart.close()
    if log:
        log.close()
