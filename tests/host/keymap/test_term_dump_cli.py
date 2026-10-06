#!/usr/bin/env python3
"""Contract for CLI term.dump (REQ-TERM-001 / AC-TERM-005, TEST-TERM-007).

Cobre o lado CLI de `term.dump` sem hardware e sem pyserial:

  - make_parser() expoe o subcomando `term.dump`, sem argumentos extras
  - build_request(args) devolve EXATAMENTE {"type":"term.dump"} (sem payload)
  - a requisicao enviada e uma linha NDJSON com `rid` e type correto
  - `main(["term.dump"])` imprime o envelope normal e retorna 0
  - envelope de erro (ok:false) e impresso e retorna 1
  - ausencia de resposta levanta RuntimeError e retorna 2 em stderr
  - payload com \\n, aspas e UTF-8 sobrevive ao round-trip e continua uma linha
  - a correlacao por `rid` ignora frames de log e de outras requisicoes
  - o round-trip do texto fornecido pelo transporte e preservado

O transporte serial e substituido por um dublê em memoria; nenhum byte real
sai do host.

O comportamento do payload (limite 3000 bytes, `truncated`) e responsabilidade
do firmware e verificado estruturalmente em test_term_dump_contract.py.
"""
import io
import json
import sys
import types
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

import tools.cyberdeck_cli as cli  # noqa: E402


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


class FakeTransport:
    """Transporte em memoria: captura o que a CLI escreve e devolve linhas."""

    def __init__(self, replies=None):
        self.written = b""
        self.replies = list(replies or [])

    # --- API minima esperada pela CyberdeckSession ---
    def write(self, data):
        self.written += bytes(data)

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    @property
    def in_waiting(self):
        return 0

    def read(self, size=1):
        return b""

    def readline(self):
        if not self.replies:
            return b""
        item = self.replies.pop(0)
        if callable(item):
            item = item(self.last_request())
        return item

    def last_request(self):
        payload = self.written.decode().strip()
        return json.loads(payload.splitlines()[-1])

    def sent_lines(self):
        return [ln for ln in self.written.decode().splitlines() if ln.strip()]


def run_main(argv, transport):
    """Executa cli.main() com um modulo `serial` duble e devolve (rc, out, err)."""
    fake_module = types.ModuleType("serial")

    def serial_open(port, baud, timeout=None):
        transport.port = port
        transport.baud = baud
        return transport

    class SerialException(Exception):
        pass

    fake_module.Serial = serial_open
    fake_module.SerialException = SerialException
    original = sys.modules.get("serial")
    sys.modules["serial"] = fake_module
    out, err = io.StringIO(), io.StringIO()
    try:
        with redirect_stdout(out), redirect_stderr(err):
            rc = cli.main(argv)
    finally:
        if original is None:
            sys.modules.pop("serial", None)
        else:
            sys.modules["serial"] = original
    return rc, out.getvalue(), err.getvalue()


def envelope(rid, result):
    return (json.dumps({"rid": rid, "ok": True, "result": result}) + "\n").encode()


# ---------------------------------------------------------------------
# parser e build_request
# ---------------------------------------------------------------------
def test_parser_exposes_term_dump():
    parser = cli.make_parser()
    args = parser.parse_args(["term.dump"])
    require(args.command == "term.dump", "o subcomando term.dump deve ser reconhecido")
    # term.dump nao aceita argumentos posicionais/Opcionais adicionais.
    try:
        with redirect_stderr(io.StringIO()):
            parser.parse_args(["term.dump", "extra"])
    except SystemExit:
        pass
    else:
        raise AssertionError("term.dump nao deve aceitar argumento posicional extra")


def test_build_request_shape():
    parser = cli.make_parser()
    req = cli.build_request(parser.parse_args(["term.dump"]))
    require(req == {"type": "term.dump"},
            f"build_request deve devolver exatamente {{'type': 'term.dump'}}, obtive {req!r}")
    require("text" not in req, "term.dump nao leva campo text")
    require("rid" not in req, "o rid e gerado pela sessao, nao pelo build_request")
    # build_request nao deve ser confundido com ui.dump/screen.dump.
    for other in ("ui.dump", "screen.shot"):
        other_req = cli.build_request(parser.parse_args([other]))
        require(other_req != req, f"{other} nao pode gerar o mesmo request de term.dump")


# ---------------------------------------------------------------------
# requisicao enviada ao dispositivo
# ---------------------------------------------------------------------
def test_request_line_is_single_ndjson():
    reply = {"rid": None, "ok": True, "result": {"text": "", "truncated": False,
                                                   "bytes": 0, "limit": 3000}}
    transport = FakeTransport()
    transport.replies = [lambda req: envelope(req["rid"], reply["result"])]
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 0, f"deve retornar 0 no caminho feliz, obtive {rc} (err={err!r})")

    lines = transport.sent_lines()
    require(len(lines) == 1, f"deve enviar exatamente 1 linha, enviou {len(lines)}")
    sent = json.loads(lines[0])
    require(sent["type"] == "term.dump", f"type enviado errado: {sent!r}")
    require("rid" in sent and sent["rid"], "a requisicao deve carregar rid")
    require(transport.written.endswith(b"\n"), "a requisicao deve ser terminada por \\n")
    require(b"\n" not in transport.written[:-1], "a requisicao nao pode ter \\n interno")


def test_request_ignores_foreign_frames():
    """Logs e envelopes de outra requisicao nao podem virar a resposta."""
    mine = {"text": "meu", "truncated": False, "bytes": 3, "limit": 3000}
    foreign = (json.dumps({"rid": "outro-rid", "ok": True,
                           "result": {"text": "alheio"}}) + "\n").encode()
    transport = FakeTransport()
    transport.replies = [
        b"I (1234) cyberdeck: log line\n",
        b"texto solto do console sem JSON\n",
        foreign,
        lambda req: envelope(req["rid"], mine),
    ]
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 0, f"deve retornar 0, obtive {rc} (err={err!r})")
    printed = [json.loads(ln) for ln in out.strip().splitlines() if ln.strip()]
    require(len(printed) == 1, f"apenas o envelope proprio deve ser impresso: {printed!r}")
    require(printed[0]["result"] == mine,
            f"o envelope alheio nao pode sobrescrever o proprio: {printed[0]!r}")


# ---------------------------------------------------------------------
# saida normal / erro / ausencia de resposta
# ---------------------------------------------------------------------
def test_normal_output():
    result = {"text": "root@cyberdeck:~$ ", "truncated": False,
              "bytes": 18, "limit": 3000}
    transport = FakeTransport()
    transport.replies = [lambda req: envelope(req["rid"], result)]
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 0, f"caminho feliz deve retornar 0, obtive {rc}")
    require(err == "", f"nao deve escrever em stderr: {err!r}")
    lines = out.strip().splitlines()
    require(len(lines) == 1, f"a saida normal deve ser 1 linha JSON, obtive {len(lines)}")
    frame = json.loads(lines[0])
    require(frame["ok"] is True, f"ok:true esperado: {frame!r}")
    require(frame["result"] == result, f"result preservado: {frame['result']!r}")
    require(frame["result"]["truncated"] is False, "truncated:false em saida normal")
    require(frame["result"]["limit"] == 3000, "limit deve reportar 3000")
    # ensure_ascii=False preserva o texto como UTF-8 real na saida.
    require("café" not in out, "sanity do oráculo de UTF-8")
    result_utf8 = {"text": "acentuação € \U0001f600", "truncated": False,
                   "bytes": 21, "limit": 3000}
    transport2 = FakeTransport()
    transport2.replies = [lambda req: envelope(req["rid"], result_utf8)]
    rc2, out2, _ = run_main(["term.dump"], transport2)
    require(rc2 == 0, "caminho feliz com UTF-8 deve retornar 0")
    require("acentuação € \U0001f600" in out2,
            "o texto UTF-8 deve ser impresso sem escapes ASCII")
    frame2 = json.loads(out2.strip())
    require(frame2["result"]["text"] == result_utf8["text"],
            "round-trip do texto UTF-8 deve ser exato")
    # O \\n interno do texto nao pode quebrar a saida em varias linhas.
    require(len(out2.strip().splitlines()) == 1,
            "texto com \\n deve continuar em uma unica linha JSON no stdout")


def test_truncated_output():
    result = {"text": "linha\n" * 500, "truncated": True, "bytes": 3000, "limit": 3000}
    transport = FakeTransport()
    transport.replies = [lambda req: envelope(req["rid"], result)]
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 0, f"truncated:true ainda e sucesso, obtive {rc}")
    frame = json.loads(out.strip())
    require(frame["result"]["truncated"] is True, "truncated:true deve ser propagado")
    require(frame["result"]["bytes"] == 3000, "bytes deve vir do envelope")
    require(len(out.strip().splitlines()) == 1, "saida truncada continua uma linha")


def test_error_output_returns_1():
    transport = FakeTransport()
    transport.replies = [lambda req: (json.dumps({
        "rid": req["rid"], "ok": False,
        "error": "display busy",
        "error_code": "internal",
    }) + "\n").encode()]
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 1, f"envelope de erro deve retornar 1, obtive {rc}")
    frame = json.loads(out.strip())
    require(frame["ok"] is False, f"ok:false esperado: {frame!r}")
    require(frame["error"] == "display busy", f"detalhe do erro preservado: {frame!r}")
    require(frame["error_code"] == "internal", f"error_code preservado: {frame!r}")


def test_no_response_returns_2():
    transport = FakeTransport(replies=[])
    rc, out, err = run_main(["term.dump"], transport)
    require(rc == 2, f"ausencia de resposta deve retornar 2, obtive {rc}")
    require("erro:" in err, f"deve reportar o erro em stderr: {err!r}")
    require(out == "", f"nao deve imprimir nada no stdout: {out!r}")


def test_unknown_command_rejected():
    # O parser rejeita o subcomando inexistente (invalid choice).
    parser = cli.make_parser()
    try:
        with redirect_stderr(io.StringIO()):
            parser.parse_args(["term.dumpx"])
    except SystemExit:
        pass
    else:
        raise AssertionError("o parser deve rejeitar term.dumpx (quase-erro de digitacao)")

    # E build_request tambem tem guarda propria para comando desconhecido.
    args = types.SimpleNamespace(command="term.dumpx")
    try:
        cli.build_request(args)
    except ValueError:
        pass
    else:
        raise AssertionError("build_request deve rejeitar comando desconhecido")


def main_all():
    test_parser_exposes_term_dump()
    test_build_request_shape()
    test_request_line_is_single_ndjson()
    test_request_ignores_foreign_frames()
    test_normal_output()
    test_truncated_output()
    test_error_output_returns_1()
    test_no_response_returns_2()
    test_unknown_command_rejected()
    print("PASS: term_dump_cli")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main_all())
    except (AssertionError, OSError, UnicodeError, ValueError) as e:
        print(f"FAIL: {e}")
        raise SystemExit(1)
