#!/usr/bin/env python3
"""CLI NDJSON da ponte manual USB Serial-JTAG do cyberdeck5 (REQ-002/003/007).

Modelada em tools/tab5_cli.py do tab5-os, com o protocolo desta ponte:

- requisicao:  {"rid": "...", "type": "<comando>", ...} + "\\n"
- resposta:    {"rid": "...", "ok": true, "result": {...}}   ou
               {"rid": "...", "ok": false, "error": "...", "error_code": "..."}
- screen.dump: frames {"rid","ok","event":"start|chunk|end", ...} com chunks
  Base64 canonico, size/chunks/crc32 declarados no start e validados aqui.

O mesmo stream USB Serial-JTAG carrega ESP_LOG e a resposta da ponte: linhas
que nao forem JSON ou nao casarem com o `rid` sao descartadas (tolerancia a
logs/leitura fragmentada, REQ-007/REQ-008). Sem pyserial no host de teste:
o transporte e injetavel (duck-typed) e o pyserial so e exigido em main().
"""
from __future__ import annotations

import argparse
import base64
import binascii
import itertools
import json
import sys
import time
import zlib

# Limite de BYTES decodificados por fs.write (REQ-001..REQ-011); espelha
# cyberdeck_serial::k_fs_write_max_bytes do firmware.
FS_WRITE_MAX_BYTES = 2048
FS_WRITE_MAX_PATH_BYTES = 240
FS_WRITE_ROOT = "/sdcard/"


class CyberdeckSession:
    """Correlaciona requisicoes NDJSON por `rid` (sem campo `action`)."""

    _rid_counter = itertools.count(1)

    def __init__(self, transport):
        self.transport = transport

    @classmethod
    def _new_rid(cls) -> str:
        return "cli-%d" % next(cls._rid_counter)

    def _discard_input(self):
        """Descarta bytes que pertencem ao console, nao a proxima resposta."""
        reset = getattr(self.transport, "reset_input_buffer", None)
        if callable(reset):
            reset()
        else:
            flush_input = getattr(self.transport, "flushInput", None)
            if callable(flush_input):
                flush_input()

        read = getattr(self.transport, "read", None)
        if not callable(read) or not hasattr(self.transport, "in_waiting"):
            return
        deadline = time.monotonic() + 0.05
        while time.monotonic() < deadline:
            count = getattr(self.transport, "in_waiting", 0)
            if callable(count):
                count = count()
            if not count:
                break
            read(count)

    @staticmethod
    def _matches(frame, request) -> bool:
        """Enquadra o frame na requisicao atual: dict com mesmo `rid`."""
        if not isinstance(frame, dict):
            return False
        if not isinstance(request, dict):
            return True
        if "rid" in request and frame.get("rid") != request.get("rid"):
            return False
        return True

    def _iter_frames(self):
        """Le linhas NDJSON de um transporte que pode fragmentar leituras."""
        pending = b""
        while True:
            raw = self.transport.readline()
            if not raw:
                break
            if isinstance(raw, str):
                raw = raw.encode()
            else:
                raw = bytes(raw)
            pending += raw
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                try:
                    frame = json.loads(line.decode().strip())
                except (TypeError, ValueError, UnicodeDecodeError):
                    # Console/ESP_LOG e linhas malformadas nao sao frames.
                    continue
                if isinstance(frame, dict):
                    yield frame

    @staticmethod
    def _validate_chunks(chunks, expected):
        for index in sorted(chunks):
            if not isinstance(index, int) or index < 0 or index >= expected:
                raise RuntimeError("screen.dump: indice de chunk invalido %s" % index)
            frame = chunks[index]
            try:
                decoded = base64.b64decode(frame["b64"], validate=True)
            except (ValueError, binascii.Error, TypeError) as exc:
                raise RuntimeError("screen.dump: Base64 invalido no chunk %s" % index) from exc
            if base64.b64encode(decoded).decode("ascii") != frame["b64"]:
                raise RuntimeError("screen.dump: Base64 nao canonico no chunk %s" % index)
        missing = [i for i in range(expected) if i not in chunks]
        if missing:
            raise RuntimeError("screen.dump: chunks ausentes=%s" % missing)

    @staticmethod
    def _merge_chunk(chunks, frame):
        """Aceita repeticao identica; retransmissao conflitante e erro."""
        index = frame.get("chunk")
        if index in chunks:
            if chunks[index].get("b64") != frame.get("b64"):
                raise RuntimeError("screen.dump: chunk duplicado conflitante %s" % index)
            return
        chunks[index] = frame

    def exchange(self, request: dict):
        """Envia uma requisicao e devolve os frames NDJSON correspondentes.

        Para comandos simples devolve o envelope unico. Para screen.dump
        reconstroi e valida a sequencia start/chunks/end (contiguidade,
        Base64 canonico, size e CRC32 IEEE declarados no start).
        """
        request = dict(request)
        if "rid" not in request:
            request["rid"] = self._new_rid()
        payload = (json.dumps(request, separators=(",", ":")) + "\n").encode()
        is_dump = request.get("type") == "screen.dump"

        self._discard_input()
        self.transport.write(payload)
        flush = getattr(self.transport, "flush", None)
        if callable(flush):
            flush()

        frames = []
        chunks: dict = {}
        got_end = False
        for frame in self._iter_frames():
            if not self._matches(frame, request):
                continue
            frames.append(frame)
            if frame.get("ok") is False:
                # envelope de erro: devolve para main() reportar com exit 1
                if is_dump:
                    return frames
                break
            if not is_dump:
                break
            if frame.get("event") == "start":
                continue
            if isinstance(frame.get("chunk"), int) and "b64" in frame:
                self._merge_chunk(chunks, frame)
            elif frame.get("event") == "end":
                got_end = True
                break

        if not frames:
            raise RuntimeError("nenhuma resposta do dispositivo")

        if is_dump and frames[0].get("ok") is not False:
            start = frames[0] if frames[0].get("event") == "start" else None
            if start is None or not isinstance(start.get("size"), int) or not isinstance(start.get("chunks"), int):
                raise RuntimeError("screen.dump: start invalido (size/chunks obrigatorios)")
            if start["size"] < 0 or start["chunks"] < 0:
                raise RuntimeError("screen.dump: size/chunks invalidos")
            self._validate_chunks(chunks, start["chunks"])
            if not got_end:
                raise RuntimeError("screen.dump: frame end ausente")
            payload_len = sum(len(base64.b64decode(chunks[i]["b64"])) for i in range(start["chunks"]))
            if payload_len != start["size"]:
                raise RuntimeError("screen.dump: tamanho recebido=%d declarado=%d" % (payload_len, start["size"]))
            if "crc32" in start:
                raw = b"".join(base64.b64decode(chunks[i]["b64"]) for i in range(start["chunks"]))
                if not isinstance(start["crc32"], int) or (zlib.crc32(raw) & 0xFFFFFFFF) != start["crc32"]:
                    raise RuntimeError("screen.dump: CRC32 divergente")
        return frames

    @staticmethod
    def dump_payload(frames):
        """Reconstroi os bytes BMP dos frames validados de screen.dump."""
        ordered = [f for f in frames if isinstance(f.get("chunk"), int) and "b64" in f]
        ordered.sort(key=lambda f: f["chunk"])
        return b"".join(base64.b64decode(f["b64"]) for f in ordered)


Session = CyberdeckSession


def open_session(transport) -> CyberdeckSession:
    return CyberdeckSession(transport)


def connect(transport) -> CyberdeckSession:
    return open_session(transport)


def build_request(args) -> dict:
    if args.command == "ping":
        return {"type": "ping"}
    if args.command == "sys.info":
        return {"type": "sys.info"}
    if args.command == "wifi.status":
        return {"type": "wifi.status"}
    if args.command == "wifi.scan":
        return {"type": "wifi.scan"}
    if args.command == "ui.click":
        return {"type": "ui.click", "x": args.x, "y": args.y}
    if args.command == "ui.tap":
        return {"type": "ui.tap", "target": args.target}
    if args.command == "ui.type":
        return {"type": "ui.type", "text": args.text}
    if args.command == "ui.dump":
        return {"type": "ui.dump"}
    if args.command == "ui.clear":
        return {"type": "ui.clear"}
    if args.command == "screen.shot":
        return {"type": "screen.shot"}
    if args.command == "screen.dump":
        return {"type": "screen.dump"}
    if args.command == "fs.write":
        validate_fs_write_path(args.path)
        data = read_fs_write_input(args)
        if len(data) > FS_WRITE_MAX_BYTES:
            raise ValueError(
                "fs.write: %d bytes decodificados excedem o limite de %d"
                % (len(data), FS_WRITE_MAX_BYTES)
            )
        declared_size = getattr(args, "size", None)
        if declared_size is not None and declared_size != len(data):
            raise ValueError(
                "fs.write: --size %d difere do payload (%d bytes)"
                % (declared_size, len(data))
            )
        return {
            "type": "fs.write",
            "path": args.path,
            "data_b64": base64.b64encode(data).decode("ascii"),
            "size": len(data),
        }
    raise ValueError("comando desconhecido: %s" % args.command)


def validate_fs_write_path(path: str) -> None:
    """Confina o path a /sdcard e rejeita traversal/controles antes do envio.

    O firmware revalida tudo; aqui a recusa e eager para nao transmitir um
    pedido que ja e invalido (REQ-001..REQ-011).  O limite e medido em bytes
    UTF-8 porque esse e o formato que chega ao VFS.
    """
    if not isinstance(path, str) or not path:
        raise ValueError("fs.write: path vazio")
    try:
        encoded = path.encode("utf-8")
    except UnicodeError as exc:
        raise ValueError("fs.write: path nao e UTF-8 valido") from exc
    if len(encoded) > FS_WRITE_MAX_PATH_BYTES:
        raise ValueError("fs.write: path longo demais")
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in path):
        raise ValueError("fs.write: path com caractere de controle")
    # Backslash nao e separador no POSIX, mas pode virar caminho host/OS
    # diferente; rejeita-lo evita qualquer ambiguidade de traversal.
    if "\\" in path:
        raise ValueError("fs.write: path com separador invalido")
    if not path.startswith(FS_WRITE_ROOT):
        raise ValueError("fs.write: path fora da raiz confinada /sdcard")
    if path.endswith("/"):
        raise ValueError("fs.write: path aponta para um diretorio (termina em /)")
    for component in path[len(FS_WRITE_ROOT):].split("/"):
        if component in ("", ".", ".."):
            raise ValueError("fs.write: componente de path invalido (traversal)")
        try:
            if len(component.encode("utf-8")) > 255:
                raise ValueError("fs.write: componente de path longo demais")
        except UnicodeError as exc:
            raise ValueError("fs.write: path nao e UTF-8 valido") from exc


def decode_b64_canonical(text: str) -> bytes:
    """Decodifica Base64 estrito e canonico; qualquer desvio vira ValueError."""
    if not isinstance(text, str):
        raise ValueError("fs.write: data_b64 deve ser texto")
    if len(text) % 4 != 0:
        raise ValueError("fs.write: data_b64 com comprimento nao multiplo de 4")
    try:
        data = base64.b64decode(text, validate=True)
    except (ValueError, binascii.Error, TypeError) as exc:
        raise ValueError("fs.write: data_b64 invalido") from exc
    if base64.b64encode(data).decode("ascii") != text:
        raise ValueError("fs.write: data_b64 fora do formato canonico")
    return data


def _read_bounded_bytes(handle) -> bytes:
    """Le no maximo MAX+1 bytes, sem aceitar um payload maior por accident."""
    chunks = []
    remaining = FS_WRITE_MAX_BYTES + 1
    while remaining > 0:
        chunk = handle.read(remaining)
        if not chunk:
            break
        if not isinstance(chunk, (bytes, bytearray)):
            raise ValueError("fs.write: fonte de entrada deve produzir bytes")
        chunks.append(bytes(chunk))
        remaining -= len(chunk)
    data = b"".join(chunks)
    if len(data) > FS_WRITE_MAX_BYTES:
        raise ValueError(
            "fs.write: payload excede o limite de %d bytes decodificados"
            % FS_WRITE_MAX_BYTES
        )
    return data


def read_fs_write_input(args) -> bytes:
    """Le os bytes do fs.write: --data, --data-b64, --input FILE ou stdin.

    Sem nenhuma fonte explicita, le stdin em modo binario (o padrao quando a
    entrada vem de pipe/arquivo). Retorna bytes arbitrarios (NUL-safe).
    """
    data = getattr(args, "data", None)
    data_b64 = getattr(args, "data_b64", None)
    path_in = getattr(args, "input", None)
    use_stdin = bool(getattr(args, "stdin", False))
    sources = sum(
        value is not None for value in (data, data_b64, path_in)
    )
    if sources + int(use_stdin) > 1:
        raise ValueError("fs.write: use apenas uma fonte de dados")
    if use_stdin or sources == 0:
        return read_stdin_bytes()
    if data is not None:
        try:
            return data.encode("utf-8")
        except UnicodeError as exc:
            raise ValueError("fs.write: --data nao e UTF-8 valido") from exc
    if data_b64 is not None:
        return decode_b64_canonical(data_b64)
    with open(path_in, "rb") as handle:
        return _read_bounded_bytes(handle)


def read_stdin_bytes() -> bytes:
    """Le stdin como bytes, tambem sem aceitar mais que o limite fs.write."""
    buffer = getattr(sys.stdin, "buffer", None)
    if buffer is not None:
        return _read_bounded_bytes(buffer)
    text = sys.stdin.read(FS_WRITE_MAX_BYTES + 1)
    if isinstance(text, str):
        try:
            data = text.encode("utf-8")
        except UnicodeError as exc:
            raise ValueError("fs.write: stdin nao e UTF-8 valido") from exc
        if len(data) > FS_WRITE_MAX_BYTES:
            raise ValueError(
                "fs.write: payload excede o limite de %d bytes decodificados"
                % FS_WRITE_MAX_BYTES
            )
        return data
    if not isinstance(text, (bytes, bytearray)):
        raise ValueError("fs.write: stdin nao produziu bytes")
    data = bytes(text)
    if len(data) > FS_WRITE_MAX_BYTES:
        raise ValueError(
            "fs.write: payload excede o limite de %d bytes decodificados"
            % FS_WRITE_MAX_BYTES
        )
    return data


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="cyberdeck_cli",
        description="CLI NDJSON da ponte manual USB Serial-JTAG do cyberdeck5",
    )
    parser.add_argument("--port", default="/dev/ttyACM0", help="porta serial (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate (default: 115200)")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ping", help="heartbeat NDJSON (REQ-002)")
    sub.add_parser("sys.info", help="versoes/heap/uptime (REQ-008)")
    sub.add_parser("wifi.status", help="estado da conexao Wi-Fi")
    sub.add_parser("wifi.scan", help="varredura sincrona de redes (REQ-009)")
    p_click = sub.add_parser("ui.click", help="clique por coordenada absoluta")
    p_click.add_argument("x", type=int)
    p_click.add_argument("y", type=int)
    p_tap = sub.add_parser("ui.tap", help="clique no alvo visivel com o texto")
    p_tap.add_argument("target")
    p_type = sub.add_parser("ui.type", help="digita texto no prompt + Enter")
    p_type.add_argument("text")
    sub.add_parser("ui.dump", help="arvore visivel (classes/texto/coordenadas)")
    sub.add_parser("ui.clear", help="limpa a linha e executa `clear`")
    sub.add_parser("screen.shot", help="metadados do frame atual (BMP)")
    p_dump = sub.add_parser("screen.dump", help="stream BMP em chunks com CRC32 (REQ-006/007)")
    p_dump.add_argument("--out", help="grava o BMP reconstituido no arquivo")
    p_fs = sub.add_parser(
        "fs.write",
        help="grava ate 2048 bytes Base64 em /sdcard (atomico; REQ-001..REQ-011)",
    )
    p_fs.add_argument("path", help="path confinado em /sdcard (ex.: /sdcard/a.bin)")
    p_fs.add_argument("--data", help="conteudo como texto UTF-8")
    p_fs.add_argument("--data-b64", dest="data_b64", help="conteudo em Base64 canonico")
    p_fs.add_argument("--input", metavar="FILE", help="le os bytes do arquivo FILE")
    p_fs.add_argument(
        "--stdin",
        action="store_true",
        help="le os bytes de stdin (padrao quando nenhuma fonte e dada)",
    )
    p_fs.add_argument(
        "--size",
        type=int,
        help="tamanho declarado; precisa bater com o payload decodificado",
    )
    return parser


def main(argv=None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        import serial
    except ImportError:
        parser.error("pyserial e necessario para o transporte serial (pip install pyserial)")
    try:
        transport = serial.Serial(args.port, args.baud, timeout=3)
    except (OSError, serial.SerialException) as exc:  # type: ignore[attr-defined]
        parser.error("falha ao abrir %s: %s" % (args.port, exc))

    try:
        request = build_request(args)
    except (ValueError, OSError) as exc:
        print("erro: %s" % exc, file=sys.stderr)
        return 2
    try:
        frames = open_session(transport).exchange(request)
    except RuntimeError as exc:
        print("erro: %s" % exc, file=sys.stderr)
        return 2

    if args.command == "screen.dump" and getattr(args, "out", None):
        try:
            payload = CyberdeckSession.dump_payload(frames)
            with open(args.out, "wb") as output:
                output.write(payload)
        except (OSError, KeyError, ValueError) as exc:
            print("falha ao salvar screenshot: %s" % exc, file=sys.stderr)
            return 2

    for frame in frames:
        print(json.dumps(frame, ensure_ascii=False))
    if any(frame.get("ok") is False for frame in frames):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
