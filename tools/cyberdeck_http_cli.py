#!/usr/bin/env python3
"""Send authenticated commands to a Tab5 over the LAN.

The USB Serial-JTAG port stays free for `idf.py monitor`: this client only uses
HTTP. Provisioning happens once over USB and stores a dedicated key; the OTA key
is never reused.

    python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision
    python3 tools/cyberdeck_http_cli.py --host 192.168.0.50 type "help"
    python3 tools/cyberdeck_http_cli.py --host 192.168.0.50 terminal

Every request uses a single-use nonce from /control/challenge and an
HMAC-SHA256 over nonce, method, URI and the SHA-256 of the body. HTTP does not
provide confidentiality: use it only on a trusted LAN.
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
