import hashlib
import hmac
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
CURRENT_VERSION = (ROOT / "version.txt").read_text(encoding="utf-8").strip()
SCRIPT = ROOT / "tools/update_firmware.py"
spec = importlib.util.spec_from_file_location("ota_update", SCRIPT)
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


class OTAScriptTest(unittest.TestCase):
    def test_authentication_binds_mode_image_and_size(self):
        key = bytes(range(32))
        nonce = "0a" * 16
        digest = hashlib.sha256(b"firmware").hexdigest()
        version = CURRENT_VERSION
        for silent in (False, True):
            headers = ota.auth_headers(key, nonce, digest, 8, silent, version)
            message = f"{nonce}\n{digest}\n8\n{int(silent)}\n{version}".encode()
            self.assertEqual(headers["X-OTA-MAC"], hmac.new(key, message, hashlib.sha256).hexdigest())
            self.assertEqual(headers["X-OTA-Silent"], str(int(silent)))
            self.assertEqual(headers["X-OTA-Size"], "8")
            self.assertEqual(headers["X-OTA-Version"], version)
        self.assertNotEqual(ota.auth_headers(key, nonce, digest, 8, True, version)["X-OTA-MAC"],
                            ota.auth_headers(key, nonce, digest, 9, True, version)["X-OTA-MAC"])
        self.assertNotEqual(ota.auth_headers(key, nonce, digest, 8, True, version)["X-OTA-MAC"],
                            ota.auth_headers(key, nonce, digest, 8, True, "0.0.0")["X-OTA-MAC"])
        with self.assertRaises(ValueError):
            ota.auth_headers(key, "not-a-challenge", digest, 8, True, version)
        with self.assertRaises(ValueError):
            ota.auth_headers(key, nonce, digest, 8, True, "01.10.0")

    def test_key_permissions_and_format(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "secret"
            path.write_text("ab" * 32 + "\n")
            path.chmod(0o600)
            self.assertEqual(ota.read_key(path), bytes.fromhex("ab" * 32))
            path.chmod(0o644)
            with self.assertRaises(RuntimeError):
                ota.read_key(path)
            path.chmod(0o600)
            path.write_text("invalid\n")
            with self.assertRaises(RuntimeError):
                ota.read_key(path)

    def test_confirm_prepares_before_streaming_and_silent_skips_prepare(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.bin"
            image.write_bytes(b"firmware")
            for silent in (False, True):
                events = []
                statuses = iter([
                    {"running_slot": 131072, "running_version": "1.9.0"},
                    *([] if silent else [
                        {"state": "awaiting_confirmation"},
                        {"state": "approved_upload"},
                    ]),
                    {"running_slot": 4325376, "running_version": CURRENT_VERSION, "running_valid": True},
                ])

                def fake_request(host, method, path, **kwargs):
                    events.append(("request", method, path, kwargs))
                    if path.endswith("challenge"):
                        return {"nonce": "ab" * 16}
                    if path.endswith("prepare"):
                        self.assertEqual(kwargs["body"], b"")
                        self.assertEqual(kwargs["headers"]["Content-Length"], "0")
                        return {"state": "awaiting_confirmation"}
                    result = next(statuses)
                    events.append(("status", result))
                    return result

                def fake_upload(host, upload_path, headers):
                    events.append(("upload", upload_path.read_bytes(), headers))

                with patch.object(ota, "request", side_effect=fake_request), \
                     patch.object(ota, "upload_image", side_effect=fake_upload), \
                     patch.object(ota.time, "sleep"):
                    ota.send("192.168.1.20", image, b"x" * 32, silent)
                upload_index = next(index for index, event in enumerate(events) if event[0] == "upload")
                upload = events[upload_index]
                self.assertEqual(upload[1], b"firmware")
                self.assertEqual(upload[2]["X-OTA-Silent"], "1" if silent else "0")
                self.assertEqual(upload[2]["Content-Length"], "8")
                self.assertEqual(upload[2]["X-OTA-Version"], CURRENT_VERSION)
                self.assertEqual(upload[2]["X-OTA-SHA256"], hashlib.sha256(b"firmware").hexdigest())
                prepare_indices = [i for i, event in enumerate(events)
                                   if event[0] == "request" and event[2].endswith("prepare")]
                if silent:
                    self.assertEqual(prepare_indices, [])
                else:
                    self.assertEqual(len(prepare_indices), 1)
                    self.assertLess(prepare_indices[0], upload_index)
                    approved_index = next(index for index, event in enumerate(events)
                                           if event[0] == "status" and
                                           event[1].get("state") == "approved_upload")
                    self.assertLess(approved_index, upload_index)

    def test_cancelled_confirmation_never_streams_image(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.bin"
            image.write_bytes(b"firmware")
            statuses = iter([{"running_slot": 131072, "running_version": "1.9.0"},
                             {"state": "idle"}])
            uploads = []

            def fake_request(host, method, path, **kwargs):
                if path.endswith("challenge"):
                    return {"nonce": "ab" * 16}
                if path.endswith("prepare"):
                    return {"state": "awaiting_confirmation"}
                return next(statuses)

            with patch.object(ota, "request", side_effect=fake_request), \
                 patch.object(ota, "upload_image", side_effect=lambda *args: uploads.append(args)), \
                 patch.object(ota.time, "sleep"):
                with self.assertRaisesRegex(RuntimeError, "cancelled or confirmation expired"):
                    ota.send("192.168.1.20", image, b"x" * 32, False)
            self.assertEqual(uploads, [])


if __name__ == "__main__":
    unittest.main()
