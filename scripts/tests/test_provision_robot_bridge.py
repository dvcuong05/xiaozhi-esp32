import importlib.util
import io
import json
import os
import unittest
import warnings
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


provision = load("provision_robot_bridge", ROOT / "scripts/provision_robot_bridge.py")
build = load("bridge_build", ROOT / "scripts/build.py")


class ProvisionTests(unittest.TestCase):
    def test_uri_rejects_insecure_or_injected_endpoints(self):
        for uri in ("http://example.org", "https://user@example.org", "https://example.org?q=x",
                    "https://example.org/#secret", "https://example.org:0", "https://example.org\n"):
            with self.subTest(uri=uri), self.assertRaises(provision.ProvisionError):
                provision.validate_uri(uri, "https")
        self.assertEqual(provision.validate_uri("wss://example.org:18443/device/bridge", "wss"),
                         "wss://example.org:18443/device/bridge")

    def test_credentials_reject_header_injection_and_bad_id(self):
        for data in ({"device_id": "x\r\ny", "token": "t" * 32},
                     {"device_id": "x", "token": "t" * 31},
                     {"device_id": "x", "token": "t" * 129},
                     {"device_id": "x", "token": "t" * 32 + "\n"}, []):
            with self.subTest(data=data), self.assertRaises(provision.ProvisionError):
                provision.validate_credentials(data)

    def test_enrollment_contract_and_verified_tls(self):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = json.dumps(
            {"device_id": "robot_01", "token": "x" * 32}).encode()
        opener = mock.Mock()
        opener.open.return_value = response
        with mock.patch.object(provision.urllib.request, "build_opener", return_value=opener), \
                mock.patch.object(provision.ssl, "create_default_context", wraps=provision.ssl.create_default_context) as tls:
            result = provision.enroll("https://example.org", "single-use")
        tls.assert_called_once_with(cafile=None)
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "https://example.org/device/enroll")
        self.assertEqual(json.loads(request.data), {"code": "single-use"})
        self.assertEqual(result["device_id"], "robot_01")
        self.assertEqual(opener.open.call_args.kwargs["timeout"], 20)

    def test_no_redirect_or_automatic_redeem_retry_or_secret_error(self):
        self.assertIsNone(provision.NoRedirect().redirect_request(None, None, 302, "", {}, "http://other"))
        opener = mock.Mock()
        opener.open.side_effect = OSError("SECRET RESPONSE")
        with mock.patch.object(provision.urllib.request, "build_opener", return_value=opener):
            with self.assertRaises(provision.ProvisionError) as error:
                provision.enroll("https://example.org", "secret-code")
        self.assertNotIn("SECRET", str(error.exception))
        self.assertNotIn("secret-code", str(error.exception))
        self.assertEqual(opener.open.call_count, 1)

    def test_no_dtr_reset_and_serial_retries(self):
        events = []

        class Port:
            def __setattr__(self, key, value):
                events.append((key, value))
                object.__setattr__(self, key, value)

            def open(self):
                events.append(("open", None))

        port = Port()
        serial = SimpleNamespace(Serial=mock.Mock(side_effect=[OSError(), port]), SerialException=OSError)
        with mock.patch.object(provision.time, "sleep"):
            self.assertIs(provision.open_serial(serial, "COM6", 115200, 2), port)
        self.assertEqual(events[:4], [("dtr", False), ("rts", False), ("port", "COM6"), ("open", None)])
        self.assertEqual(serial.Serial.call_args.kwargs["write_timeout"], 3)
        self.assertEqual(serial.Serial.call_args.kwargs["timeout"], 0.25)

    def test_serial_log_noise_and_oversize_cannot_fake_ack(self):
        port = mock.Mock()
        port.write.return_value = 1
        port.read.side_effect = [b"log\n" + b"z" * 2049 + b"KNSBRIDGE ACK\n", b"KNSBRIDGE ACK\n"]
        provision.exchange(port, b"x", b"KNSBRIDGE ACK", 1)
        self.assertEqual(port.read.call_count, 2)

    def test_serial_timeout_and_storage_failure(self):
        port = mock.Mock()
        port.write.return_value = 1
        port.read.return_value = b"KNSBRIDGE ERROR storage\n"
        with self.assertRaises(provision.ProvisionError):
            provision.exchange(port, b"x", b"KNSBRIDGE ACK", 1)
        with mock.patch.object(provision.time, "monotonic", side_effect=[0, 2]):
            with self.assertRaises(provision.ProvisionError):
                provision.exchange(port, b"x", b"KNSBRIDGE ACK", 1)

    def test_unavailable_serial_does_not_redeem_or_prompt(self):
        with mock.patch.dict("sys.modules", {"serial": mock.Mock()}), \
                mock.patch.object(provision, "open_serial", side_effect=provision.ProvisionError("unavailable")), \
                mock.patch.object(provision, "enroll") as enroll, \
                mock.patch.object(provision.getpass, "getpass") as prompt, \
                mock.patch("sys.stdout", new_callable=io.StringIO), \
                mock.patch("sys.stderr", new_callable=io.StringIO):
            result = provision.main(["--port", "COM6", "--server", "https://example.org",
                                     "--uri", "wss://example.org/device/bridge"])
        self.assertEqual(result, 1)
        enroll.assert_not_called()
        prompt.assert_not_called()

    def test_runtime_build_clears_stale_token_and_legacy_is_explicit(self):
        options = ["CONFIG_XIAOZHI_MCP_DEVICE_WSS_ENABLE=y"]
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(build._device_bridge_token_options(options),
                             ['CONFIG_XIAOZHI_MCP_DEVICE_WSS_TOKEN=""'])
        with mock.patch.dict(os.environ, {"XIAOZHI_MCP_DEVICE_WSS_TOKEN": "t" * 32}, clear=True):
            self.assertIn("t" * 32, build._device_bridge_token_options(options)[0])

    def test_not_ready_does_not_consume_code(self):
        with mock.patch.dict("sys.modules", {"serial": mock.Mock()}), \
                mock.patch.object(provision, "open_serial", return_value=mock.Mock()), \
                mock.patch.object(provision, "exchange", side_effect=provision.ProvisionError("timeout")), \
                mock.patch.object(provision, "enroll") as enroll, \
                mock.patch.object(provision.getpass, "getpass") as prompt, \
                mock.patch("sys.stdout", new_callable=io.StringIO), \
                mock.patch("sys.stderr", new_callable=io.StringIO):
            result = provision.main(["--port", "COM6", "--server", "https://example.org",
                                     "--uri", "wss://example.org/device/bridge", "--retries", "2"])
        self.assertEqual(result, 1)
        enroll.assert_not_called()
        prompt.assert_not_called()

    def test_hidden_input_warning_stops_before_echo_or_enrollment(self):
        echoed = mock.Mock()

        def unsupported_terminal(*args, **kwargs):
            warnings.warn("terminal cannot hide input", provision.getpass.GetPassWarning)
            echoed()  # getpass fallback would echo only after issuing its warning.
            return "must-not-be-read"

        with mock.patch.dict("sys.modules", {"serial": mock.Mock()}), \
                mock.patch.object(provision, "open_serial", return_value=mock.Mock()), \
                mock.patch.object(provision, "exchange"), \
                mock.patch.object(provision.getpass, "getpass", side_effect=unsupported_terminal), \
                mock.patch.object(provision, "enroll") as enroll, \
                mock.patch("sys.stdout", new_callable=io.StringIO), \
                mock.patch("sys.stderr", new_callable=io.StringIO) as errors:
            result = provision.main(["--port", "COM6", "--server", "https://example.org",
                                     "--uri", "wss://example.org/device/bridge"])
        self.assertEqual(result, 1)
        echoed.assert_not_called()
        enroll.assert_not_called()
        self.assertIn("PowerShell", errors.getvalue())
        self.assertNotIn("must-not-be-read", errors.getvalue())

    def test_reconnect_resends_same_credentials_without_redeem_or_secret_output(self):
        serial = SimpleNamespace(SerialException=OSError)
        with mock.patch.dict("sys.modules", {"serial": serial}), \
                mock.patch.object(provision, "open_serial", return_value=mock.Mock()), \
                mock.patch.object(provision, "exchange", side_effect=[None, OSError(), None]) as exchange, \
                mock.patch.object(provision, "enroll", return_value={"device_id": "r1", "token": "s" * 32}) as enroll, \
                mock.patch.object(provision.getpass, "getpass", return_value="secret-code"), \
                mock.patch.object(provision.time, "sleep"), \
                mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            result = provision.main(["--port", "COM6", "--server", "https://example.org",
                                     "--uri", "wss://example.org/device/bridge"])
        self.assertEqual(result, 0)
        enroll.assert_called_once()
        self.assertEqual(exchange.call_args_list[1].args[1], exchange.call_args_list[2].args[1])
        self.assertNotIn("s" * 32, output.getvalue())
        self.assertNotIn("secret-code", output.getvalue())


if __name__ == "__main__":
    unittest.main()
