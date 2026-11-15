"""Load the RAM diagnostic over UART; never erase or program flash."""

import argparse
import pathlib
import subprocess
import sys
import time

import serial


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    project = pathlib.Path(__file__).resolve().parent
    config = (project / "sdkconfig").read_text(encoding="utf-8")
    if "\nCONFIG_APP_BUILD_TYPE_RAM=y\n" not in config:
        raise RuntimeError("Refusing to load a firmware which was not built for RAM")
    binary = project / "build" / "robot_display_diagnostic.bin"
    subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
         "--baud", "460800", "--no-stub", "--after", "no-reset", "load-ram", str(binary)],
        check=True,
    )
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        deadline = time.monotonic() + max(1, args.seconds)
        while time.monotonic() < deadline:
            line = connection.readline()
            if line:
                print(line.decode("utf-8", errors="replace").rstrip(), flush=True)
    finally:
        connection.close()
    print("RAM diagnostic remains running. Press RST or reconnect power to restore the stored firmware.")


if __name__ == "__main__":
    main()
