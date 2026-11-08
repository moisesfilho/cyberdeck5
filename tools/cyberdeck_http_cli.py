#!/usr/bin/env python3
"""Send authenticated commands to a Tab5 over the LAN.

The USB Serial-JTAG port stays free for `idf.py monitor`: this client only uses
HTTP. Provisioning happens once over USB and stores a dedicated key; the OTA key
is never reused.

    python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision
    python3 tools/cyberdeck_http_cli.py --host 192.168.0.50 type "help"
    python3 tools/cyberdeck_http_cli.py --host 192.168.0.50 terminal
    python3 tools/cyberdeck_http_cli.py --host 192.168.0.50 upload fixture.bmp

Every request uses a single-use nonce from /control/challenge and an
HMAC-SHA256 over nonce, method, URI and the SHA-256 of the body. HTTP does not
provide confidentiality: use it only on a trusted LAN.

`upload` sends one `.bmp` basename to the fixed device directory
`/sdcard/screenshots` (visible as `/screenshots/<name>`). The client refuses an
oversized/invalid file before opening a connection, computes the local SHA-256
and signs the upload-specific message (domain + nonce + method + URI + basename +
size + digest). The device re-verifies the digest of the streamed body and never
overwrites an existing file.
"""

import argparse
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import stat
import time

CONTROL_MAX_BYTES = 1024
UPLOAD_MAX_BYTES = 3 * 1024 * 1024
UPLOAD_URI = "/control/upload"
UPLOAD_DOMAIN = "cyberdeck-control-upload/v1"
UPLOAD_NAME_MAX = 128
UPLOAD_NAME_CHARS = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
DEFAULT_KEY_FILE = Path.home() / ".config/cyberdeck5/control.key"
DEFAULT_PORT = 80


def validate_key(value):
    if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        raise ValueError("Expected a 64-character lowercase hex key")
    return bytes.fromhex(value)


def read_key(path):
    metadata = Path(path).lstat()
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_mode & 0o077:
        raise RuntimeError("Key must be a private regular file (chmod 600)")
    return validate_key(Path(path).read_text().strip())


def write_key(path, value):
    """Create the key file exclusively with 0600; never overwrite."""
    key = validate_key(value)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w") as output:
        output.write(key.hex() + "\n")


def signed_headers(key, nonce, method, uri, body):
    if len(nonce) != 32 or any(c not in "0123456789abcdef" for c in nonce):
        raise ValueError("Invalid device challenge")
    if len(key) != 32:
        raise ValueError("Invalid key")
    digest = hashlib.sha256(body).hexdigest()
    message = f"{nonce}\n{method}\n{uri}\n{digest}".encode("ascii")
    return {
        "X-CTL-Nonce": nonce,
        "X-CTL-MAC": hmac.new(key, message, hashlib.sha256).hexdigest(),
    }


def upload_name_problem(name):
    """Return why `name` is not an acceptable device basename, or None."""
    if not isinstance(name, str):
        return "name must be a string"
    if len(name) < 5 or len(name) > UPLOAD_NAME_MAX:
        return f"name must be 5..{UPLOAD_NAME_MAX} characters"
    if name[0] not in UPLOAD_NAME_CHARS or not name[0].isascii() or name[0] in "._-":
        return "name must start with a letter or digit"
    if any(c not in UPLOAD_NAME_CHARS for c in name):
        return "name may only contain ASCII letters, digits, '.', '_' and '-'"
    if ".." in name:
        return "name must not contain '..'"
    if not name.lower().endswith(".bmp"):
        return "name must end with .bmp"
    return None


def upload_signed_headers(key, nonce, name, size, digest_hex):
    if len(nonce) != 32 or any(c not in "0123456789abcdef" for c in nonce):
        raise ValueError("Invalid device challenge")
    if len(key) != 32:
        raise ValueError("Invalid key")
    if len(digest_hex) != 64 or any(c not in "0123456789abcdef" for c in digest_hex):
        raise ValueError("Invalid SHA-256 digest")
    message = (f"{UPLOAD_DOMAIN}\n{nonce}\nPOST\n{UPLOAD_URI}\n{name}\n{size}\n"
               f"{digest_hex}").encode("ascii")
    return {
        "X-CTL-Nonce": nonce,
        "X-CTL-MAC": hmac.new(key, message, hashlib.sha256).hexdigest(),
    }


class session:
    def __init__(self, host, key, port=DEFAULT_PORT, timeout=15):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.key = key

    def request(self, method, uri, body=b"", headers=None):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)
        try:
            connection.request(method, uri, body=body, headers=headers or {})
            response = connection.getresponse()
            content = response.read(65536)
            return response.status, content
        finally:
            connection.close()

    def challenge(self):
        status, content = self.request("GET", "/control/challenge")
        if status != 200:
            raise RuntimeError(f"/control/challenge: HTTP {status}: {content.decode(errors='replace')}")
        try:
            payload = json.loads(content)
        except ValueError as error:
            raise RuntimeError(f"invalid challenge payload: {error}") from error
        return payload["nonce"], payload.get("ttl_ms")

    def type(self, text):
        if not isinstance(text, str):
            raise ValueError("text must be a string")
        body = text.encode("utf-8")
        if not body:
            raise ValueError("refusing to send empty text; send '\\n' to press Enter only")
        if len(body) > CONTROL_MAX_BYTES:
            raise ValueError(f"text is {len(body)} bytes; the device limit is {CONTROL_MAX_BYTES}")
        nonce, _ = self.challenge()
        headers = signed_headers(self.key, nonce, "POST", "/control/type", body)
        headers["Content-Type"] = "text/plain; charset=utf-8"
        headers["Content-Length"] = str(len(body))
        status, content = self.request("POST", "/control/type", body, headers)
        return status, content

    def terminal(self):
        nonce, _ = self.challenge()
        headers = signed_headers(self.key, nonce, "GET", "/control/terminal", b"")
        return self.request("GET", "/control/terminal", b"", headers)

    def upload(self, path, name=None):
        source = Path(path)
        if not source.is_file():
            raise ValueError(f"not a regular file: {source}")
        size = source.stat().st_size
        if size <= 0:
            raise ValueError("refusing to upload an empty file")
        if size > UPLOAD_MAX_BYTES:
            raise ValueError(f"file is {size} bytes; the device limit is {UPLOAD_MAX_BYTES}")
        target = name if name is not None else source.name
        problem = upload_name_problem(target)
        if problem is not None:
            raise ValueError(problem)
        body = source.read_bytes()
        if len(body) != size:
            raise ValueError("file changed while reading")
        digest = hashlib.sha256(body).hexdigest()
        nonce, _ = self.challenge()
        headers = upload_signed_headers(self.key, nonce, target, size, digest)
        headers.update({
            "Content-Type": "image/bmp",
            "Content-Length": str(size),
            "X-CTL-Name": target,
            "X-CTL-Size": str(size),
            "X-CTL-Digest": digest,
        })
        status, content = self.request("POST", UPLOAD_URI, body, headers)
        return status, content, digest


def provision(port, key_path):
    """Fetch the dedicated command key once over USB Serial-JTAG."""
    import serial

    key_path = Path(key_path)
    if key_path.exists():
        raise RuntimeError(f"Refusing to overwrite existing key: {key_path}")
    with serial.Serial(port, 115200, timeout=3) as link:
        link.write(b'{"rid":"ctl-provision","type":"control.key"}\n')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            line = link.readline()
            try:
                frame = json.loads(line)
            except (ValueError, UnicodeDecodeError):
                continue
            if frame.get("rid") != "ctl-provision":
                continue
            if not frame.get("ok"):
                raise RuntimeError("Device refused control key provisioning")
            key_path.parent.mkdir(parents=True, exist_ok=True)
            write_key(key_path, frame["result"]["key"])
            return key_path
    raise RuntimeError("No provisioning response from USB Serial-JTAG")


def parse_envelope(content):
    try:
        return json.loads(content)
    except ValueError as error:
        raise RuntimeError(f"invalid JSON response: {error}") from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", help="Tab5 LAN address")
    parser.add_argument("--http-port", type=int, default=DEFAULT_PORT,
                        help=f"device HTTP port (default {DEFAULT_PORT})")
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="USB Serial-JTAG port used by provision")
    parser.add_argument("--key-file", type=Path, default=DEFAULT_KEY_FILE)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--json", action="store_true", help="print raw JSON result")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("provision", help="fetch the key once over USB")

    type_parser = subparsers.add_parser("type", help="inject text + Enter (like ui.type)")
    type_parser.add_argument("text", help="text; use \\n for multiple lines")

    subparsers.add_parser("terminal", help="read the bounded terminal snapshot")
    subparsers.add_parser("status", help="check reachability and nonce provisioning")

    upload_parser = subparsers.add_parser(
        "upload", help="upload one .bmp basename to /screenshots (max 3 MiB)")
    upload_parser.add_argument("file", help="local .bmp fixture")
    upload_parser.add_argument("--name", help="device basename (default: local basename)")

    arguments = parser.parse_args(argv)

    if arguments.command == "provision":
        saved = provision(arguments.port, arguments.key_file)
        print(f"Control key saved to {saved} (0600)")
        return 0

    if not arguments.host:
        raise SystemExit("--host is required for this command")

    key = read_key(arguments.key_file)
    client = session(arguments.host, key, arguments.http_port, arguments.timeout)

    if arguments.command == "status":
        nonce, ttl = client.challenge()
        result = {"reachable": True, "nonce_chars": len(nonce), "ttl_ms": ttl}
        print(json.dumps(result))
        return 0

    if arguments.command == "type":
        status, content = client.type(arguments.text)
        envelope = parse_envelope(content)
        if status != 202:
            raise SystemExit(f"rejected (HTTP {status}): {json.dumps(envelope)}")
        print(json.dumps(envelope) if arguments.json else
              f"accepted for dispatch: {envelope.get('bytes')} bytes, "
              f"{envelope.get('lines')} line(s); verify with 'terminal'")
        return 0

    if arguments.command == "upload":
        status, content, digest = client.upload(arguments.file, arguments.name)
        envelope = parse_envelope(content)
        if not 200 <= status < 300:
            raise SystemExit(f"rejected (HTTP {status}): {json.dumps(envelope)}")
        if envelope.get("sha256") != digest:
            raise SystemExit("device reported a different SHA-256 than the local file")
        if arguments.json:
            print(json.dumps(envelope))
        else:
            print(f"uploaded {envelope.get('name')}: {envelope.get('bytes')} bytes "
                  f"sha256 {envelope.get('sha256')}")
            print(f"open with: image {envelope.get('path')}")
        return 0

    status, content = client.terminal()
    envelope = parse_envelope(content)
    if status != 200:
        raise SystemExit(f"rejected (HTTP {status}): {json.dumps(envelope)}")
    if arguments.json:
        print(json.dumps(envelope))
    else:
        suffix = " (truncated)" if envelope.get("truncated") else ""
        print(envelope.get("text", "") + suffix)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
