import hashlib
import hmac
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/cyberdeck_http_cli.py"
spec = importlib.util.spec_from_file_location("control_http_cli", SCRIPT)
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)

KEY = bytes(range(32))
NONCE = "0a" * 16


def expected_mac(method, uri, body):
    message = f"{NONCE}\n{method}\n{uri}\n{hashlib.sha256(body).hexdigest()}".encode("ascii")
    return hmac.new(KEY, message, hashlib.sha256).hexdigest()


class ControlCliTest(unittest.TestCase):
    def test_signed_headers_bind_method_uri_and_body(self):
        for method, uri, body in (
            ("POST", "/control/type", b"help"),
            ("GET", "/control/terminal", b""),
        ):
            headers = cli.signed_headers(KEY, NONCE, method, uri, body)
            self.assertEqual(headers["X-CTL-Nonce"], NONCE)
            self.assertEqual(headers["X-CTL-MAC"], expected_mac(method, uri, body))
        base = cli.signed_headers(KEY, NONCE, "POST", "/control/type", b"help")["X-CTL-MAC"]
        self.assertNotEqual(base, cli.signed_headers(KEY, NONCE, "POST", "/control/type", b"helP")["X-CTL-MAC"])
        self.assertNotEqual(base, cli.signed_headers(KEY, NONCE, "GET", "/control/type", b"help")["X-CTL-MAC"])
        self.assertNotEqual(base, cli.signed_headers(KEY, NONCE, "POST", "/control/term", b"help")["X-CTL-MAC"])
        with self.assertRaises(ValueError):
            cli.signed_headers(KEY, "not-a-challenge", "POST", "/control/type", b"help")
        with self.assertRaises(ValueError):
            cli.signed_headers(b"short", NONCE, "POST", "/control/type", b"help")

    def test_key_file_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "control.key"
            cli.write_key(path, KEY.hex())
            self.assertEqual(path.read_text().strip(), KEY.hex())
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(cli.read_key(path), KEY)
            with self.assertRaises(FileExistsError):
                cli.write_key(path, KEY.hex())
            path.chmod(0o644)
            with self.assertRaises(RuntimeError):
                cli.read_key(path)
            path.chmod(0o600)
            path.write_text(KEY.hex().upper() + "\n")
            with self.assertRaises(ValueError):
                cli.read_key(path)
            path.write_text("abc\n")
            with self.assertRaises(ValueError):
                cli.read_key(path)

    def test_type_rejects_empty_and_oversized_before_network(self):
        client = cli.session("127.0.0.1", KEY)
        with patch.object(client, "request", side_effect=AssertionError("network used")):
            with self.assertRaises(ValueError):
                client.type("")
            with self.assertRaises(ValueError):
                client.type("a" * (cli.CONTROL_MAX_BYTES + 1))
            with self.assertRaises(ValueError):
                client.type(b"bytes")  # type: ignore[arg-type]

    def test_type_signs_body_and_parses_acceptance(self):
        client = cli.session("127.0.0.1", KEY)
        captured = {}

        def fake_request(method, uri, body=b"", headers=None):
            if uri == "/control/challenge":
                return 200, b'{"nonce":"' + NONCE.encode() + b'","ttl_ms":60000}'
            captured["method"] = method
            captured["uri"] = uri
            captured["body"] = body
            captured["headers"] = headers
            return 202, b'{"accepted":true,"bytes":8,"lines":2}'

        with patch.object(client, "request", side_effect=fake_request):
            status, content = client.type("help\nwifi")
        self.assertEqual(status, 202)
        self.assertIn(b"accepted", content)
        self.assertEqual(captured["method"], "POST")
        self.assertEqual(captured["uri"], "/control/type")
        self.assertEqual(captured["body"], b"help\nwifi")
        self.assertEqual(captured["headers"]["Content-Length"], "9")
        self.assertEqual(captured["headers"]["X-CTL-MAC"],
                         expected_mac("POST", "/control/type", b"help\nwifi"))

    def test_terminal_signs_an_empty_body(self):
        client = cli.session("127.0.0.1", KEY)
        captured = {}

        def fake_request(method, uri, body=b"", headers=None):
            if uri == "/control/challenge":
                return 200, b'{"nonce":"' + NONCE.encode() + b'"}'
            captured["method"] = method
            captured["headers"] = headers
            captured["body"] = body
            return 200, b'{"text":"/$ pwd\\n/sdcard","bytes":16,"truncated":false}'

        with patch.object(client, "request", side_effect=fake_request):
            status, _ = client.terminal()
        self.assertEqual(status, 200)
        self.assertEqual(captured["method"], "GET")
        self.assertEqual(captured["body"], b"")
        self.assertEqual(captured["headers"]["X-CTL-MAC"],
                         expected_mac("GET", "/control/terminal", b""))

    def test_provision_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "control.key"
            path.write_text(KEY.hex() + "\n")
            with self.assertRaises(RuntimeError):
                cli.provision("/dev/null", path)


if __name__ == "__main__":
    unittest.main()
