#!/usr/bin/env python3
"""Enroll one robot over verified HTTPS, then provision through USB UART."""

import argparse
import getpass
import json
import re
import ssl
import sys
import time
import urllib.request
import warnings
from urllib.parse import urlsplit


class ProvisionError(Exception):
    pass


def validate_uri(uri, scheme):
    try:
        parsed = urlsplit(uri)
        port = parsed.port
        valid = (parsed.scheme == scheme and parsed.hostname and
                 re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?", parsed.hostname) and
                 not parsed.username and not parsed.password and not parsed.query and
                 not parsed.fragment and (port is None or 1 <= port <= 65535) and
                 all(32 < ord(c) < 127 and c not in '\\"' for c in uri))
    except ValueError:
        valid = False
    if not valid or len(uri) > 256:
        raise ProvisionError("URI không hợp lệ; cần HTTPS/WSS, hostname hoặc IPv4, không userinfo/query/fragment.")
    return uri


def validate_credentials(data):
    if not isinstance(data, dict):
        raise ProvisionError("Phản hồi enrollment không hợp lệ.")
    device_id, token = data.get("device_id"), data.get("token")
    if (not isinstance(device_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device_id) or
            not isinstance(token, str) or not 32 <= len(token) <= 128 or
            any(ord(c) < 33 or ord(c) > 126 for c in token)):
        raise ProvisionError("Credential enrollment không hợp lệ.")
    return {"device_id": device_id, "token": token}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward a single-use code to a redirected origin or HTTP URL.
        return None


def enroll(server, code, ca_file=None):
    context = ssl.create_default_context(cafile=ca_file)
    opener = urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=context), NoRedirect())
    request = urllib.request.Request(
        server.rstrip("/") + "/device/enroll",
        data=json.dumps({"code": code}).encode("utf-8"),
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST")
    try:
        # No HTTP retry: the server may have consumed the code before a disconnect.
        with opener.open(request, timeout=20) as response:
            payload = response.read(4097)
            if len(payload) > 4096:
                raise ValueError()
        return validate_credentials(json.loads(payload))
    except Exception:
        # Exception bodies/URLs may contain sensitive data; never print them.
        raise ProvisionError("Enrollment thất bại; không tự redeem lại. Kiểm tra server và xin mã mới nếu cần.") from None


def open_serial(serial_module, port, baud, attempts):
    for attempt in range(attempts):
        try:
            connection = serial_module.Serial(port=None, baudrate=baud, timeout=0.25,
                                              write_timeout=3, rtscts=False, dsrdtr=False)
            # Set before open: never pulse DTR/RTS or invoke an ESP bootloader reset.
            connection.dtr = False
            connection.rts = False
            connection.port = port
            connection.open()
            return connection
        except (serial_module.SerialException, OSError):
            if 'connection' in locals():
                connection.close()
            if attempt + 1 < attempts:
                time.sleep(1)
    raise ProvisionError("Không mở được serial. Mã chưa được redeem nếu enrollment chưa bắt đầu.")


def exchange(connection, command, expected, timeout):
    connection.reset_input_buffer()
    if connection.write(command) != len(command):
        raise ProvisionError("Không gửi đủ dữ liệu serial.")
    deadline = time.monotonic() + timeout
    line = bytearray()
    overflow = False
    while time.monotonic() < deadline:
        chunk = connection.read(256)
        for byte in chunk:
            if byte == 10:
                if not overflow:
                    received = bytes(line).rstrip(b"\r")
                    if received.endswith(expected):
                        return
                    if b"KNSBRIDGE ERROR " in received:
                        raise ProvisionError("Firmware từ chối cấu hình hoặc ghi NVS thất bại.")
                line.clear()
                overflow = False
            elif len(line) < 2048 and not overflow:
                line.append(byte)
            else:
                overflow = True
                line.clear()
    raise ProvisionError("Hết thời gian chờ phản hồi serial.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Ví dụ COM6; đóng IDF Monitor trước")
    parser.add_argument("--server", required=True, help="HTTPS server gốc, không gồm /device/enroll")
    parser.add_argument("--uri", required=True, help="URI wss://.../device/bridge gửi cho robot")
    parser.add_argument("--ca-file", help="CA PEM tin cậy tùy chọn; luôn xác minh TLS/hostname")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--retries", type=int, default=5, help="Số lần thử kết nối serial (1..20)")
    args = parser.parse_args(argv)
    connection = None
    redeemed = False
    try:
        validate_uri(args.server, "https")
        validate_uri(args.uri, "wss")
        if not 1 <= args.retries <= 20 or args.baud <= 0:
            raise ProvisionError("Retries cần 1..20 và baud phải dương.")
        try:
            import serial
        except ImportError:
            raise ProvisionError("Cài pyserial trước: python -m pip install pyserial") from None
        print("Kiểm tra serial trước khi redeem; đóng monitor/app đang giữ COM. Không reset DTR.")
        connection = open_serial(serial, args.port, args.baud, args.retries)
        # Check the firmware is listening, not merely whether Windows can open COM.
        for attempt in range(args.retries):
            try:
                exchange(connection, b"\nKNSBRIDGE PING\n", b"KNSBRIDGE READY", 3)
                break
            except ProvisionError:
                if attempt + 1 == args.retries:
                    raise ProvisionError("Serial chưa READY; chưa redeem mã. Kiểm tra cổng và firmware.") from None
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            try:
                code = getpass.getpass("Mã enrollment dùng một lần (ẩn): ")
            except getpass.GetPassWarning:
                raise ProvisionError("Terminal không hỗ trợ nhập ẩn; hãy chạy trong PowerShell tương tác.") from None
        if not code or len(code) > 256:
            raise ProvisionError("Mã enrollment không hợp lệ.")
        credentials = enroll(args.server, code, args.ca_file)
        code = None
        redeemed = True
        command = ("KNSBRIDGE " + json.dumps({"uri": args.uri, **credentials},
                                            ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")
        print("Đã enroll; đang ghi cấu hình qua USB và chờ ACK sau NVS commit...")
        for attempt in range(args.retries):
            try:
                exchange(connection, b"\n" + command, b"KNSBRIDGE ACK", 10)
                print("Đã nhận ACK. Robot sẽ reboot; hãy kiểm tra trạng thái online trên server.")
                return 0
            except (serial.SerialException, OSError, ProvisionError):
                connection.close()
                if attempt + 1 == args.retries:
                    raise ProvisionError("Không xác nhận được ACK; robot có thể đã lưu cấu hình. Kiểm tra server trước khi cấp mã mới.") from None
                time.sleep(2)
                connection = open_serial(serial, args.port, args.baud, args.retries)
    except (ProvisionError, OSError) as error:
        # Only our fixed messages may be printed; never expose serial/HTTP exceptions.
        print(str(error) if isinstance(error, ProvisionError) else "Lỗi kết nối; không ghi thông tin bí mật.", file=sys.stderr)
        if redeemed:
            print("Mã đã redeem; không tự enroll lần nữa. Credential chỉ giữ trong RAM của lần chạy này.", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print("Đã dừng provisioning; kiểm tra server nếu mã đã được redeem.", file=sys.stderr)
        return 1
    finally:
        if connection is not None:
            connection.close()


if __name__ == "__main__":
    # Windows redirected consoles can default to cp1252, which cannot print Vietnamese.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
