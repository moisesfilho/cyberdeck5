#!/usr/bin/env python3
"""Send an authenticated firmware image to a Tab5 on the local network.

Initial provisioning (USB, once): --provision --port /dev/ttyACM0.
Subsequent uploads need no cable. The key is stored in a private local file.
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
import re


PROJECT_VERSION = Path(__file__).resolve().parents[1] / "version.txt"


def request(host, method, path, body=None, headers=None, timeout=15):
    connection = http.client.HTTPConnection(host, 8080, timeout=timeout)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        content = response.read(2048)
        if response.status >= 300:
            raise RuntimeError(f"{path}: HTTP {response.status}: {content.decode(errors='replace')}")
        return json.loads(content)
    finally:
        connection.close()


def provision(port, secret_path):
    import serial

    if secret_path.exists():
        raise RuntimeError(f"Refusing to overwrite existing key: {secret_path}")
    with serial.Serial(port, 115200, timeout=3) as link:
        link.write(b'{"rid":"ota-provision","type":"ota.key"}\n')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            line = link.readline()
            try:
                frame = json.loads(line)
            except (ValueError, UnicodeDecodeError):
                continue
            if frame.get("rid") != "ota-provision":
                continue
            if not frame.get("ok"):
                raise RuntimeError("Device refused provisioning")
            key = frame["result"]["key"]
            if len(key) != 64 or any(c not in "0123456789abcdef" for c in key):
                raise RuntimeError("Invalid secret returned by device")
            descriptor = os.open(secret_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "w") as output:
                output.write(key + "\n")
            print(f"Device secret saved to {secret_path} (0600)")
            return
    raise RuntimeError("No provisioning response from USB Serial-JTAG")


def read_key(path):
    metadata = path.lstat()
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_mode & 0o077:
        raise RuntimeError("Key must be a private regular file (chmod 600)")
    value = path.read_text().strip()
    if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        raise RuntimeError("Expected a 64-character lowercase hex key")
    return bytes.fromhex(value)


def auth_headers(key, nonce, digest, size, silent, version):
    if len(nonce) != 32 or any(c not in "0123456789abcdef" for c in nonce):
        raise ValueError("Invalid device challenge")
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version):
        raise ValueError("Expected firmware version MAJOR.MINOR.PATCH")
    mode = "1" if silent else "0"
    message = f"{nonce}\n{digest}\n{size}\n{mode}\n{version}".encode("ascii")
    return {
        "Content-Type": "application/octet-stream",
        "Content-Length": str(size),
        "X-OTA-Nonce": nonce,
        "X-OTA-SHA256": digest,
        "X-OTA-Size": str(size),
        "X-OTA-Version": version,
        "X-OTA-MAC": hmac.new(key, message, hashlib.sha256).hexdigest(),
        "X-OTA-Silent": mode,
    }


def upload_image(host, image, headers):
    connection = http.client.HTTPConnection(host, 8080, timeout=90)
    try:
        with image.open("rb") as source:
            connection.request("POST", "/ota", body=source, headers=headers)
            response = connection.getresponse()
            result = response.read(2048).decode(errors="replace")
            if response.status != 202:
                raise RuntimeError(f"Upload rejected (HTTP {response.status}): {result}")
            print(result)
    finally:
        connection.close()


def send(host, image, key, silent):
    size = image.stat().st_size
    digest = hashlib.sha256()
    with image.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    before = request(host, "GET", "/ota/status")
    challenge = request(host, "GET", "/ota/challenge")["nonce"]
    sha = digest.hexdigest()
    version = PROJECT_VERSION.read_text().strip()
    headers = auth_headers(key, challenge, sha, size, silent, version)
    if not silent:
        prepare_headers = dict(headers)
        prepare_headers.pop("Content-Type")
        prepare_headers["Content-Length"] = "0"
        prepared = request(host, "POST", "/ota/prepare", body=b"", headers=prepare_headers)
        if prepared.get("state") != "awaiting_confirmation":
            raise RuntimeError(f"Unexpected prepare state: {prepared}")
        print(f"Prepared firmware {version} ({size} bytes, SHA-256 {sha[:8]}...). No image bytes sent yet.")
        print("Confirm on the Tab5 (Enter / Confirmar), or cancel (Esc / Cancelar).")
        deadline = time.monotonic() + 145
        while time.monotonic() < deadline:
            time.sleep(1)
            try:
                current = request(host, "GET", "/ota/status", timeout=5)
            except (OSError, TimeoutError):
                continue
            if current["state"] == "approved_upload":
                print("Confirmation accepted; sending firmware bytes now.")
                break
            if current["state"] == "failed":
                raise RuntimeError("Device rejected the prepared update")
            if current["state"] == "idle":
                raise RuntimeError("Update was cancelled or confirmation expired")
        else:
            raise RuntimeError("Confirmation status inconclusive; verify the device before retrying")
        upload_image(host, image, headers)
    else:
        upload_image(host, image, headers)

    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            current = request(host, "GET", "/ota/status", timeout=5)
        except (OSError, TimeoutError):
            continue  # Device may be rebooting.
        if current["running_slot"] != before["running_slot"] and current["running_valid"]:
            print(f"Device booted firmware {current['running_version']}")
            return
        if current["state"] == "failed":
            raise RuntimeError("Update failed during activation")
    raise RuntimeError("Update status inconclusive; verify the device before retrying")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", help="Tab5 LAN address")
    parser.add_argument("--firmware", type=Path, help="Firmware .bin image")
    parser.add_argument("--key-file", type=Path, default=Path.home() / ".config/cyberdeck5/ota.key")
    parser.add_argument("--provision", action="store_true", help="Fetch key once over USB Serial-JTAG")
    parser.add_argument("--port", default="/dev/ttyACM0")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--silent", action="store_true", help="Activate without physical confirmation")
    mode.add_argument("--confirm", action="store_true", help="Require confirmation on the Tab5")
    args = parser.parse_args()
    if args.provision:
        args.key_file.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        provision(args.port, args.key_file)
    else:
        if not args.host or not args.firmware or not (args.silent or args.confirm):
            parser.error("--host, --firmware and --silent or --confirm are required")
        send(args.host, args.firmware, read_key(args.key_file), args.silent)


if __name__ == "__main__":
    main()
