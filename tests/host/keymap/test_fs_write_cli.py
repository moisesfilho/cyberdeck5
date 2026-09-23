#!/usr/bin/env python3
"""TDD RED contract for CLI fs.write (REQ-001..REQ-011).

Cobre CLI build_request/encoding/input/stdin antes da producao.
Sem hardware/pyserial. O CLI deve prover:

  - subcomando `fs.write` em make_parser()
  - build_request(args) -> {"type":"fs.write","path":...,"data_b64":...,"size":...}
  - encoding: input bytes -> strict canonical Base64 (b64encode(b64decode(s))==s)
  - input: --data / --data-b64 / stdin (quando ausente, ler stdin)
  - limite 2048 decoded (CLI rejeita >2048 antes de enviar)
  - path safety: rejeita vazio/".." e nao envia traversal (ou delega mas nao crasha)
  - size == decoded len; size explicito divergente deve falhar

Antes da implementacao, make_parser nao tem fs.write -> RED.
"""
import sys
import base64
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

import tools.cyberdeck_cli as cli  # noqa: E402


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


def b64_canonical(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def test_parser_has_fs_write():
    p = cli.make_parser()
    # must have fs.write subcommand
    actions = [a for a in p._actions if hasattr(a, "choices")]
    choices = {}
    for a in p._actions:
        if hasattr(a, "choices") and a.choices:
            choices.update(a.choices)
    require("fs.write" in choices, "CLI parser must have subcommand fs.write")
    sub = choices["fs.write"]
    # must accept path argument
    arg_names = []
    for act in getattr(sub, "_actions", []):
        arg_names.extend(getattr(act, "option_strings", []))
        if hasattr(act, "dest"):
            arg_names.append(act.dest)
    require("path" in arg_names or "path" in str(arg_names), "fs.write must accept path")


def test_build_request_encoding():
    p = cli.make_parser()
    # case 1: --data raw bytes -> data_b64 canonical and size auto
    args = p.parse_args(["fs.write", "/sdcard/a.bin", "--data", "hello"])
    req = cli.build_request(args)
    require(req.get("type") == "fs.write", "build_request type must be fs.write")
    require(req.get("path") == "/sdcard/a.bin", "path must be preserved")
    require(req.get("data_b64") == b64_canonical(b"hello"), "data_b64 must be canonical base64 of --data")
    require(req.get("size") == 5, "size must equal decoded length")
    # strict canonical: re-encode must match
    decoded = base64.b64decode(req["data_b64"], validate=True)
    require(base64.b64encode(decoded).decode() == req["data_b64"], "data_b64 must be strict canonical")

    # case 2: --data-b64 already canonical
    good_b64 = b64_canonical(b"ab")
    args2 = p.parse_args(["fs.write", "/sdcard/b.bin", "--data-b64", good_b64])
    req2 = cli.build_request(args2)
    require(req2["data_b64"] == good_b64, "data_b64 passthrough must preserve canonical")
    require(req2["size"] == 2, "size 2 for 'ab'")

    # case 3: --data-b64 non-canonical must be rejected (either parse error or build_request raises)
    bad_b64 = "YWI=="  # "ab" with extra padding, not canonical
    try:
        args3 = p.parse_args(["fs.write", "/sdcard/c.bin", "--data-b64", bad_b64])
        req3 = cli.build_request(args3)
        # if it returns, it must have rejected
        require(False, "non-canonical base64 must be rejected")
    except (ValueError, RuntimeError, SystemExit):
        pass  # expected rejection

    # case 4: size explicit mismatch must be rejected
    try:
        args4 = p.parse_args(["fs.write", "/sdcard/d.bin", "--data", "hi", "--size", "999"])
        req4 = cli.build_request(args4)
        require(False, "size mismatch must be rejected")
    except (ValueError, RuntimeError, SystemExit):
        pass


def test_build_request_stdin_and_limits():
    p = cli.make_parser()
    # stdin: when no --data/--data-b64, CLI should read from stdin bytes
    # We simulate by passing --stdin or by mocking sys.stdin; here check that parser supports stdin mode
    # At minimum, calling with path only should either require data arg or read stdin without crash
    try:
        args = p.parse_args(["fs.write", "/sdcard/std.bin"])
        # if parser allows missing data, build_request should read stdin or fail cleanly
        # We provide fake stdin by monkeypatching
        import io
        fake = io.BytesIO(b"stdin-bytes")
        old_stdin = sys.stdin
        try:
            sys.stdin = io.TextIOWrapper(fake, encoding="utf-8")
            # Some impls expose helper read_fs_write_input(); try to detect
            if hasattr(cli, "read_fs_write_input"):
                data = cli.read_fs_write_input(args)
                require(data == b"stdin-bytes", "stdin helper must return bytes")
            else:
                req = cli.build_request(args)
                # if build_request succeeds without data, it must have consumed stdin
                require("data_b64" in req, "stdin mode must produce data_b64")
        finally:
            sys.stdin = old_stdin
    except SystemExit:
        # argparse may require --data; acceptable if stdin is optional flag
        pass

    # limit 2048: CLI must reject >2048 before sending
    big = b"A" * 2049
    big_b64 = b64_canonical(big)
    p2 = cli.make_parser()
    try:
        args_big = p2.parse_args(["fs.write", "/sdcard/big.bin", "--data-b64", big_b64, "--size", "2049"])
        req_big = cli.build_request(args_big)
        require(False, "decoded >2048 must be rejected by CLI")
    except (ValueError, RuntimeError, SystemExit):
        pass

    # exactly 2048 must be accepted
    ok_data = b"Z" * 2048
    ok_b64 = b64_canonical(ok_data)
    args_ok = p2.parse_args(["fs.write", "/sdcard/ok.bin", "--data-b64", ok_b64])
    req_ok = cli.build_request(args_ok)
    require(req_ok["size"] == 2048, "2048 must be accepted")
    require(req_ok["data_b64"] == ok_b64, "2048 canonical preserved")


def test_path_validation_cli():
    p = cli.make_parser()
    for bad in ["", "../escape.bin", "/etc/passwd", "/sdcard/../etc/passwd"]:
        try:
            args = p.parse_args(["fs.write", bad, "--data", "x"])
            req = cli.build_request(args)
            # CLI may delegate to firmware, but must not crash and should either reject or preserve path for firmware validation
            # At minimum, req path must equal input (firmware will reject)
            require(req["path"] == bad, "path passthrough")
            # But empty/traversal should ideally be rejected by CLI eagerly
            if bad == "" or ".." in bad or bad == "/etc/passwd":
                # if CLI is strict, it should have raised; if permissive, we mark as not rejected but firmware must
                pass
        except (ValueError, RuntimeError, SystemExit):
            pass  # eager rejection is acceptable


def main():
    test_parser_has_fs_write()
    test_build_request_encoding()
    test_build_request_stdin_and_limits()
    test_path_validation_cli()
    print("PASS: fs_write_cli")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as e:
        print(f"FAIL: {e}")
        raise SystemExit(1)
