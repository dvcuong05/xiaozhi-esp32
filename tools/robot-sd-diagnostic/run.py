"""Run the read-only SD diagnostic in RAM; never program flash."""
import argparse
import pathlib
import subprocess
import sys
import time
import serial

def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=35)
    args = parser.parse_args()
    project = pathlib.Path(__file__).resolve().parent
    if "\nCONFIG_APP_BUILD_TYPE_RAM=y\n" not in (project / "sdkconfig").read_text(encoding="utf-8"):
        raise RuntimeError("Refusing to load a non-RAM build")
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
        "--baud", "460800", "--no-stub", "--after", "no-reset", "load-ram",
        str(project / "build" / "robot_sd_diagnostic.bin")], check=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            line = connection.readline()
            if line:
                print(line.decode("utf-8", errors="replace").rstrip(), flush=True)
        # Restore the production image by resetting into normal boot mode.
        connection.dtr = False
        connection.rts = True
        time.sleep(0.1)
        connection.rts = False
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            line = connection.readline()
            if line:
                print(line.decode("utf-8", errors="replace").rstrip(), flush=True)
    finally:
        connection.close()

if __name__ == "__main__":
    main()
