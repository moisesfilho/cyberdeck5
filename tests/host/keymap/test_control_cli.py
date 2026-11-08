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

    def test_upload_rejects_before_network(self):
        client = cli.session("127.0.0.1", KEY)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            missing = root / "missing.bmp"
            empty = root / "empty.bmp"
            empty.write_bytes(b"")
            oversized = root / "oversized.bmp"
            oversized.write_bytes(b"\0" * (cli.UPLOAD_MAX_BYTES + 1))
            good = root / "good.bmp"
            good.write_bytes(b"BM" + b"\0" * 8)
            with patch.object(client, "request", side_effect=AssertionError("network used")):
                with self.assertRaises(ValueError):
                    client.upload(missing)
                with self.assertRaises(ValueError):
                    client.upload(empty)
                with self.assertRaises(ValueError):
                    client.upload(oversized)
                with self.assertRaises(ValueError):
                    client.upload(good, "bad.png")
                with self.assertRaises(ValueError):
                    client.upload(good, "../escape.bmp")

    def test_upload_signs_domain_size_and_digest(self):
        client = cli.session("127.0.0.1", KEY)
        captured = {}
        payload = b"BM" + bytes(range(16))

        def fake_request(method, uri, body=b"", headers=None):
            if uri == "/control/challenge":
                return 200, b'{"nonce":"' + NONCE.encode() + b'"}'
            captured["method"] = method
            captured["uri"] = uri
            captured["body"] = body
            captured["headers"] = headers
            digest = hashlib.sha256(body).hexdigest()
            return 201, (
                b'{"name":"fixture.bmp","bytes":' + str(len(body)).encode()
                + b',"sha256":"' + digest.encode() + b'","path":"/screenshots/fixture.bmp"}')

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "fixture.bmp"
            source.write_bytes(payload)
            with patch.object(client, "request", side_effect=fake_request):
                status, content, digest = client.upload(source)
        self.assertEqual(status, 201)
        self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
        self.assertIn(b"/screenshots/fixture.bmp", content)
        self.assertEqual(captured["method"], "POST")
        self.assertEqual(captured["uri"], "/control/upload")
        self.assertEqual(captured["body"], payload)
        self.assertEqual(captured["headers"]["Content-Length"], str(len(payload)))
        self.assertEqual(captured["headers"]["X-CTL-Name"], "fixture.bmp")
        self.assertEqual(captured["headers"]["X-CTL-Size"], str(len(payload)))
        self.assertEqual(captured["headers"]["X-CTL-Digest"], digest)
        message = (f"{cli.UPLOAD_DOMAIN}\n{NONCE}\nPOST\n{cli.UPLOAD_URI}\nfixture.bmp\n"
                   f"{len(payload)}\n{digest}").encode("ascii")
        self.assertEqual(captured["headers"]["X-CTL-MAC"],
                         hmac.new(KEY, message, hashlib.sha256).hexdigest())

    def test_upload_mac_binds_basename(self):
        """TEST-UPLOAD-AUTH-12: o MAC de upload precisa cobrir o basename.

        Plano aprovado (planner 17:29): o nome viaja autenticado junto com o
        tamanho e o digest do corpo. Sem isso um intermediario troca
        `X-CTL-Name` e o dispositivo publica o corpo em outro nome, com MAC
        valido (misatribuicao silenciosa). Este caso usa o fluxo publico
        `session.upload` com TODO o resto identico (mesmo nonce, mesmo corpo,
        mesmo tamanho e digest) e varia apenas o basename.
        """
        payload = b"BM" + bytes(range(16))

        def upload_with(name):
            client = cli.session("127.0.0.1", KEY)
            captured = {}

            def fake_request(method, uri, body=b"", headers=None):
                if uri == "/control/challenge":
                    return 200, b'{"nonce":"' + NONCE.encode() + b'"}'
                captured["headers"] = headers
                captured["body"] = body
                digest = hashlib.sha256(body).hexdigest()
                return 201, (
                    b'{"name":"' + name.encode() + b'","bytes":' + str(len(body)).encode()
                    + b',"sha256":"' + digest.encode() + b'","path":"/screenshots/'
                    + name.encode() + b'"}')

            with tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / "local-source.bmp"
                source.write_bytes(payload)
                with patch.object(client, "request", side_effect=fake_request):
                    status, _, _ = client.upload(source, name)
            self.assertEqual(status, 201)
            return captured["headers"]

        first = upload_with("viewer-a.bmp")
        second = upload_with("viewer-b.bmp")
        again = upload_with("viewer-a.bmp")
        self.assertEqual(first["X-CTL-Name"], "viewer-a.bmp")
        self.assertEqual(second["X-CTL-Name"], "viewer-b.bmp")
        for field in ("X-CTL-Nonce", "X-CTL-Size", "X-CTL-Digest", "Content-Length"):
            self.assertEqual(first[field], second[field],
                             f"{field} must not depend on the basename")
        self.assertEqual(first["X-CTL-MAC"], again["X-CTL-MAC"],
                         "the upload MAC must stay deterministic for the same metadata")
        self.assertNotEqual(first["X-CTL-MAC"], second["X-CTL-MAC"],
                            "the upload MAC must bind the basename, not only size and digest")

    def test_upload_name_rules(self):
        self.assertIsNone(cli.upload_name_problem("viewer-upload-20261108-1736.bmp"))
        self.assertIsNone(cli.upload_name_problem("A.BMP"))
        self.assertIsNone(cli.upload_name_problem("a" * 124 + ".bmp"))
        for name in ("", "a.bm", "image.png", "image.bmp.exe", ".hidden.bmp", "..bmp",
                     "a..bmp", "a/b.bmp", "a b.bmp", "café.bmp", "a" * 125 + ".bmp",
                     "a.bmp\x00"):
            self.assertIsNotNone(cli.upload_name_problem(name), name)


if __name__ == "__main__":
    unittest.main()
