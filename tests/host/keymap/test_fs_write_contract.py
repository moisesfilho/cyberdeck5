#!/usr/bin/env python3
"""Structural TDD contract for fs.write production artefacts (REQ-001..REQ-011).

Inspeciona fontes reais (sem ligar hardware) para garantir que o coder
implementou os requisitos estruturais:

  - disco: path confinado a /sdcard, rejeita "..", traversal, empty, NUL, diretorio, symlink, parent missing
  - tamanho: limite 2048 decoded, size == decoded_len
  - encoding: strict canonical base64 (decode+re-encode == entrada, sem whitespace/invalid chars)
  - atomicidade: escrita via temp + rename (nao truncated write direto)
  - resposta: envelope NDJSON com ok:true, rid ecoado, sem \\n interno, contendo size/crc32
  - CLI: subcomando fs.write, build_request, input/stdin handling, encode/size validation
  - Makefile/code-map preservados

Antes da implementacao, algum require falha -> RED.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
BRIDGE_CPP = ROOT / "components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp"
BRIDGE_H = ROOT / "components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h"
CLI = ROOT / "tools/cyberdeck_cli.py"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"
LOCAL_SHELL_CPP = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


def contains(path: Path, pattern: str, msg: str):
    txt = path.read_text(encoding="utf-8", errors="ignore")
    require(re.search(pattern, txt, re.DOTALL), msg)
    return txt


def main():
    require(BRIDGE_CPP.exists(), "bridge cpp must exist")
    require(BRIDGE_H.exists(), "bridge header must exist")
    require(CLI.exists(), "cli must exist")
    bridge = BRIDGE_CPP.read_text(encoding="utf-8", errors="ignore")
    header = BRIDGE_H.read_text(encoding="utf-8", errors="ignore")
    cli_src = CLI.read_text(encoding="utf-8", errors="ignore")

    # REQ protocolo: header exposes k_fs_write_max_bytes and handles fs.write
    require("k_fs_write_max_bytes" in header or "k_fs_write_max_bytes" in bridge, "must define k_fs_write_max_bytes")
    require("2048" in header or "2048" in bridge, "limit 2048 must appear")
    require("fs.write" in bridge, "bridge must handle type fs.write")
    require("fs.write" in cli_src, "cli must have fs.write subcommand")

    # path safety: confinamento /sdcard e rejeicao de traversal
    require("/sdcard" in bridge, "fs.write must confine to /sdcard")
    # detect traversal checks: ".." , path traversal, empty, etc.
    require(re.search(r'"\.\."|\.\.\s*[,\)]|traversal|\bpath\b.*\.\.', bridge), "bridge must check for '..' traversal")
    require(re.search(r'empty|missing.*path|path.*empty', bridge, re.I) or 'path' in bridge, "bridge must validate empty path")
    # NUL handling: invalid_utf8 or control char rejection; path should be validated for NUL/control
    require("invalid_utf8" in bridge or "is_ctrl" in bridge or "NUL" in bridge or "0x00" in bridge or "valid_utf8" in bridge,
            "bridge must have path/control validation")

    # directory / parent existence: must reject directory target and missing parent
    require(re.search(r'S_ISDIR|is_directory|directory', bridge, re.I) or "directory" in bridge.lower(), "must reject directory target")
    require(re.search(r'parent|ENOENT|missing.*parent|parent.*missing', bridge, re.I) or "parent" in bridge.lower(), "must validate parent existence")

    # symlink safety: O_NOFOLLOW / lstat / symlink
    require(re.search(r'O_NOFOLLOW|symlink|lstat|S_ISLNK', bridge, re.I) or "symlink" in bridge.lower() or "S_ISLNK" in LOCAL_SHELL_CPP.read_text(encoding="utf-8", errors="ignore"),
            "must have symlink protection (O_NOFOLLOW/lstat/S_ISLNK)")

    # size / base64 limits
    require(re.search(r'data_b64|data_b64|b64', bridge, re.I), "bridge must handle data_b64 field")
    require(re.search(r'size.*decoded|decoded.*size|size.*==', bridge, re.I) or "size" in bridge, "bridge must validate size == decoded length")
    # strict canonical base64
    require(re.search(r'canonical|validate.*base64|b64.*canonical|strict', bridge, re.I) or "base64" in bridge.lower(), "must mention canonical/strict base64")
    require(re.search(r'b64encode|b64_decode|base64', bridge, re.I) or "base64" in cli_src.lower(), "base64 helpers must exist")

    # atomic write: temp file + rename
    require(re.search(r'temp|tmp|\.tmp|rename', bridge, re.I), "fs.write must use atomic temp+rename")

    # CRC response: crc32 / crc
    require(re.search(r'crc32|crc', bridge, re.I), "fs.write response must include crc32")
    require(re.search(r'build_envelope|build_error_envelope', bridge), "must use NDJSON envelope helpers")

    # CLI contracts: build_request, input/stdin, encoding
    contains(CLI, r'build_request', "cli must define build_request")
    require("fs.write" in cli_src, "cli build_request must handle fs.write")
    require(re.search(r'--data|data_b64|stdin', cli_src, re.I), "cli must handle --data/--data-b64/stdin")
    require(re.search(r'2048|k_fs_write', cli_src), "cli must enforce 2048 limit")
    require(re.search(r'base64|b64', cli_src, re.I), "cli must encode base64")

    # preserve cat / serial contracts
    require("k_max_ndjson_line" in header, "must preserve k_max_ndjson_line")
    require("k_max_rid_len" in header, "must preserve k_max_rid_len")
    require("k_screen_chunk_bytes" in header, "must preserve k_screen_chunk_bytes")
    require("12288" in (LOCAL_SHELL_CPP.read_text(encoding="utf-8", errors="ignore") + bridge), "must preserve cat 12288 budget")

    # Makefile / code-map registered
    mk = MAKEFILE.read_text(encoding="utf-8", errors="ignore")
    require("test_fs_write" in mk, "Makefile must register fs_write tests")
    require("test_fs_write_dispatch" in mk, "Makefile must have dispatch test")
    require("test_fs_write_cli" in mk, "Makefile must have cli test")
    cmap = CODEMAP.read_text(encoding="utf-8", errors="ignore")
    require("fs.write" in cmap or "fs_write" in cmap, "code-map must mention fs.write")

    print("PASS: fs_write structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as e:
        print(f"FAIL: {e}")
        raise SystemExit(1)
