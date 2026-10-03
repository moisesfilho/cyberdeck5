#!/usr/bin/env python3
"""Structural contract for term.dump production artefacts (REQ-TERM-001).

Inspeciona as fontes reais (sem ligar hardware, sem ESP_PLATFORM) para fixar
os requisitos que a superficie host NAO alcanca: `exec_term_dump` e
`cyberdeck_ui_term_dump` vivem sob `#ifdef ESP_PLATFORM`, e
`truncate_left_utf8_local` tem linkage interno.

Cobertura:
  TEST-TERM-005 (AC-TERM-003): `term.dump` e type conhecido e tem ramo
                              Proprio em device_exec; resultado bounded com
                               text/truncated/bytes/limit e limite de envelope.
  TEST-TERM-006 (AC-TERM-004): lock bounded (tick timeout) em
                               cyberdeck_ui_term_dump e TODOS os caminhos de
                               unlock/erro (arg invalido, lock falho, terminal
                               ausente, sucesso).
  TEST-TERM-007 (AC-TERM-005): API C esperada (assinatura + header), CLI
                               `term.dump` exposto e registro no Makefile,
                               mais regressao estrutural do despacho.

Sem hardware, sem pyserial, sem rede.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
BRIDGE_CPP = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
BRIDGE_H = ROOT / "components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h"
UI_CPP = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
UI_H = ROOT / "components/cyberdeck/include/platform/display/cyberdeck_ui.h"
CLI = ROOT / "tools/cyberdeck_cli.py"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


def function_body(src: str, signature: str) -> str:
    """Extrai o corpo de uma DEFINICAO por balanceamento de chaves.

    Ignora forward declarations: se houver ';' entre a assinatura e a
    proxima '{', a ocorrencia e prototipo e a busca continua.
    """
    search_at = 0
    while True:
        at = src.find(signature, search_at)
        require(at >= 0, f"assinatura ausente: {signature}")
        open_at = src.find("{", at)
        require(open_at >= 0, f"corpo ausente para: {signature}")
        if ";" in src[at:open_at]:
            search_at = open_at
            continue
        depth = 0
        for i in range(open_at, len(src)):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    return src[open_at : i + 1]
        raise AssertionError(f"corpo nao balanceado para: {signature}")


def block_after(src: str, at: int) -> str:
    """Extrai o bloco {...} que comeca em/after `at`, por balanceamento."""
    open_at = src.find("{", at)
    require(open_at >= 0, "bloco ausente")
    depth = 0
    for i in range(open_at, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_at : i + 1]
    raise AssertionError("bloco nao balanceado")


def main():
    for path in (BRIDGE_CPP, BRIDGE_H, UI_CPP, UI_H, CLI, MAKEFILE):
        require(path.exists(), f"artefato obrigatorio ausente: {path}")
    bridge = BRIDGE_CPP.read_text(encoding="utf-8", errors="ignore")
    header = BRIDGE_H.read_text(encoding="utf-8", errors="ignore")
    ui = UI_CPP.read_text(encoding="utf-8", errors="ignore")
    ui_header = UI_H.read_text(encoding="utf-8", errors="ignore")
    cli = CLI.read_text(encoding="utf-8", errors="ignore")
    mk = MAKEFILE.read_text(encoding="utf-8", errors="ignore")

    # =================================================================
    # TEST-TERM-005 (AC-TERM-003): ramo device_exec e resultado bounded
    # =================================================================
    # type conhecido no parse (regressao: remover da lista quebraria o host)
    require(re.search(r'k_known\[\][^;]*"term\.dump"', bridge, re.DOTALL),
            "term.dump deve estar na lista de types conhecidos (k_known)")
    require('"term.dump"' in bridge, "term.dump deve estar referenciado na ponte")

    # ramo dedicado em device_exec, devolvendo handled=true
    device_exec = function_body(bridge, "device_result device_exec(const request &req")
    require(re.search(r'req\.type\s*==\s*"term\.dump"', device_exec),
            "device_exec deve ter ramo proprio para req.type == \"term.dump\"")
    require(re.search(r'return\s+exec_term_dump\s*\(\s*req\s*\)', device_exec),
            "device_exec deve delegar para exec_term_dump(req)")

    body = function_body(bridge, "device_result exec_term_dump(const request &req)")
    require("d.handled = true" in body, "exec_term_dump deve marcar handled = true")

    # limites explicitos e bounded
    require(re.search(r'k_term_dump_max_bytes\s*=\s*3000', bridge),
            "deve existir k_term_dump_max_bytes = 3000")
    require(re.search(r'k_term_dump_envelope_limit\s*=\s*3500', bridge),
            "deve existir k_term_dump_envelope_limit = 3500")
    require(re.search(r'std::vector<char>\s+raw\s*\(\s*k_term_dump_max_bytes\s*\)', body),
            "exec_term_dump deve alocar raw com k_term_dump_max_bytes (bounded)")
    require(re.search(r'cyberdeck_ui_term_dump\s*\(\s*raw\.data\(\)\s*,\s*raw\.size\(\)',
                      body),
            "exec_term_dump deve passar raw.data()/raw.size() a cyberdeck_ui_term_dump")
    require(re.search(r'envelope_json\.size\(\)\s*<=\s*k_term_dump_envelope_limit', body),
            "exec_term_dump deve respeitar k_term_dump_envelope_limit")

    # campos publicados no result (aparecem escapados no literal C)
    for field in ("text", "truncated", "bytes", "limit"):
        require(re.search(r'\\"' + field + r'\\"', body),
                f'exec_term_dump deve publicar o campo "{field}" no result')
    require(re.search(r'truncated\s*!=\s*0\s*\?\s*"true"\s*:\s*"false"', body),
            "truncated deve ser serializado como booleano JSON (true/false)")
    require(re.search(r'std::to_string\(\s*text\.size\(\)\s*\)', body),
            "bytes deve refletir text.size()")
    require(re.search(r'std::to_string\(\s*k_term_dump_max_bytes\s*\)', body),
            "limit deve reportar k_term_dump_max_bytes")

    # truncamento UTF-8-safe iterativo com convergencia garantida
    require(re.search(r'text\s*=\s*truncate_left_utf8_local\s*\(', body),
            "exec_term_dump deve truncar via truncate_left_utf8_local")
    require(re.search(r'text\.size\(\)\s*-\s*std::min<std::size_t>\s*\(\s*128\s*,\s*text\.size\(\)\s*\)',
                      body),
            "o passo de truncamento deve reduzir o texto de forma monotona (128 bytes)")
    require(re.search(r'text\.empty\(\)', body),
            "o laco de truncamento precisa do caso text.empty() para terminar")
    truncate = function_body(bridge, "std::string truncate_left_utf8_local(const std::string &s")
    require(re.search(r'\(\s*static_cast<unsigned char>\s*\(\s*s\[\s*safe\s*\]\s*\)\s*&\s*0xC0\s*\)\s*==\s*0x80',
                      truncate),
            "truncate_left_utf8_local deve cortar em fronteira de code point UTF-8 (0x80)")
    require(re.search(r'return\s+s\.substr\(\s*safe\s*\)', truncate),
            "truncate_left_utf8_local deve devolver o sufixo a partir da fronteira")
    require(re.search(r'if\s*\(\s*s\.size\(\)\s*<=\s*limit\s*\)', truncate),
            "truncate_left_utf8_local nao deve truncar quando size <= limit")
    # regressao: o helper nao pode ser definido fora do bloco ESP_PLATFORM,
    # senao o build host quebra com -Werror=unused-function
    guard = bridge.find("#ifdef ESP_PLATFORM")
    helper = bridge.find("std::string truncate_left_utf8_local(const std::string &s")
    require(guard >= 0 and helper > guard,
            "truncate_left_utf8_local deve ficar depois do guarda ESP_PLATFORM")
    require(re.search(r'#ifdef ESP_PLATFORM[\s\S]*?truncate_left_utf8_local[\s\S]*?#endif',
                      bridge),
            "truncate_left_utf8_local deve estar dentro de um bloco #ifdef ESP_PLATFORM")

    # propagacao de erro do device para o envelope
    require(re.search(r'if\s*\(\s*err\s*!=\s*ESP_OK\s*\)', body),
            "exec_term_dump deve tratar erro de cyberdeck_ui_term_dump")
    require("err_result(" in body, "exec_term_dump deve emitir err_result em falha")
    require(re.search(r'ESP_ERR_TIMEOUT\s*\?\s*"display busy"\s*:\s*"terminal indisponivel"', body),
            "exec_term_dump deve distinguir ESP_ERR_TIMEOUT (display busy) de indisponibilidade")
    require(re.search(r'dispatch_error::internal', body),
            "falha de hardware/UI deve ser dispatch_error::internal")
    # nenhum caminho de erro devolve envelope de sucesso
    ok_sites = len(re.findall(r'ok_result\s*\(', body))
    require(ok_sites >= 1, "exec_term_dump deve usar ok_result no caminho feliz")

    # =================================================================
    # TEST-TERM-006 (AC-TERM-004): lock bounded e todos os unlocks
    # =================================================================
    require("esp_err_t cyberdeck_ui_term_dump" in ui_header,
            "cyberdeck_ui.h deve expor cyberdeck_ui_term_dump")
    require(re.search(r'esp_err_t\s+cyberdeck_ui_term_dump\s*\(\s*char\s*\*\s*buffer\s*,\s*size_t\s+capacity\s*,'
                      r'\s*size_t\s*\*\s*out_bytes\s*,\s*int\s*\*\s*out_truncated\s*\)',
                      ui_header, re.DOTALL),
            "assinatura publica esperada: (char*, size_t, size_t*, int*)")
    require(re.search(r'extern\s+"C"', ui_header), "cyberdeck_ui_term_dump deve ser extern \"C\"")

    ui_body = function_body(ui, "esp_err_t cyberdeck_ui_term_dump(char *buffer, size_t capacity")
    locks = len(re.findall(r'bsp_display_lock\s*\(', ui_body))
    require(locks == 1, f"cyberdeck_ui_term_dump deve adquirir o lock exatamente 1 vez (achou {locks})")
    require(re.search(r'bsp_display_lock\s*\(\s*pdMS_TO_TICKS\(\s*1000\s*\)\s*\)', ui_body),
            "o lock deve ser bounded por pdMS_TO_TICKS(1000)")
    require(re.search(r'if\s*\(\s*!\s*bsp_display_lock', ui_body),
            "falha do lock deve ser tratada (nunca assumir sucesso)")

    lock_at = ui_body.find("bsp_display_lock")
    guard_at = ui_body.rfind("if", 0, lock_at)
    require(guard_at >= 0, "o lock deve estar sob uma guarda if (!bsp_display_lock(...))")
    failure_branch = block_after(ui_body, guard_at)
    tail = ui_body[ui_body.find(failure_branch) + len(failure_branch):]

    # A falha do lock NUNCA da unlock (o lock nao foi adquirido) e retorna timeout.
    require("bsp_display_unlock" not in failure_branch,
            "falha do lock nao pode chamar unlock (lock nao adquirido)")
    require(re.search(r'return\s+ESP_ERR_TIMEOUT', failure_branch),
            "falha do lock deve retornar ESP_ERR_TIMEOUT sem unlock")

    # A partir da aquisicao, TODO return libera o lock: nenhum caminho
    # pode sair com o display travado.
    returns_after_lock = len(re.findall(r'return\s+ESP_', tail))
    unlocks_after_lock = len(re.findall(r'bsp_display_unlock\s*\(', tail))
    require(returns_after_lock >= 2,
            f"deve haver caminho de erro e caminho de sucesso apos o lock ({returns_after_lock})")
    require(unlocks_after_lock == returns_after_lock,
            f"todo return apos o lock deve liberar: {unlocks_after_lock} unlock(s) para "
            f"{returns_after_lock} return(s)")

    # Argumentos invalidos: rejeitados ANTES de tomar o lock.
    arg_check = ui_body.find("ESP_ERR_INVALID_ARG")
    lock_at = ui_body.find("bsp_display_lock")
    require(arg_check >= 0, "cyberdeck_ui_term_dump deve rejeitar argumentos invalidos")
    require(lock_at >= 0 and arg_check < lock_at,
            "argumentos invalidos devem ser rejeitados antes de tomar o lock")
    require(re.search(r'out_bytes\s*==\s*nullptr\s*\|\|\s*out_truncated\s*==\s*nullptr', ui_body),
            "out_bytes/out_truncated nulos devem ser rejeitados")
    require(re.search(r'capacity\s*>\s*0\s*&&\s*buffer\s*==\s*nullptr', ui_body),
            "buffer nulo com capacity > 0 deve ser rejeitado")

    # Estado terminal ausente: unlock + erro.
    require(re.search(r's_terminal\s*==\s*nullptr[\s\S]*?bsp_display_unlock\s*\(\s*\)[\s\S]*?'
                      r'return\s+ESP_ERR_INVALID_STATE', ui_body),
            "terminal ausente deve fazer unlock e retornar ESP_ERR_INVALID_STATE")

    # Zera saidas antes de qualquer trabalho (evita lixo no chamador).
    zero_at = ui_body.find("*out_bytes = 0")
    require(zero_at >= 0 and zero_at < ui_body.find("bsp_display_lock"),
            "out_bytes/out_truncated devem ser zerados antes do lock")

    # Caminho feliz: copia bounded, marca truncacao e libera o lock.
    require(re.search(r'rendered\.size\(\)\s*>\s*capacity[\s\S]*?truncate_left_utf8\s*\(\s*rendered\s*,\s*capacity\s*\)',
                      ui_body),
            "acima da capacidade o snapshot deve ser truncado por truncate_left_utf8")
    require(re.search(r'std::memcpy\s*\(\s*buffer\s*,\s*snapshot\.data\(\)\s*,\s*snapshot\.size\(\)\s*\)',
                      ui_body),
            "o snapshot deve ser copiado com memcpy para buffer do chamador")
    require(re.search(r'\*\s*out_bytes\s*=\s*snapshot\.size\(\)', ui_body),
            "out_bytes deve receber snapshot.size()")
    require(re.search(r'\*\s*out_truncated\s*=\s*rendered\.size\(\)\s*>\s*snapshot\.size\(\)\s*\?\s*1\s*:\s*0',
                      ui_body),
            "out_truncated deve sinalizar o truncamento do snapshot")
    require(re.search(r'bsp_display_unlock\s*\(\s*\)\s*;\s*return\s+ESP_OK', ui_body),
            "o caminho feliz deve liberar o lock antes de ESP_OK")
    require(re.search(r'if\s*\(\s*!\s*snapshot\.empty\(\)\s*\)', ui_body),
            "memcpy deve ser guardado por !snapshot.empty() (buffer pode ser capacity 0)")

    # =================================================================
    # TEST-TERM-007 (AC-TERM-005): API/CLI esperadas e regressao
    # =================================================================
    require(re.search(r'sub\.add_parser\(\s*"term\.dump"', cli),
            "a CLI deve expor o subcomando term.dump")
    require(re.search(r'if\s+args\.command\s*==\s*"term\.dump":\s*\n\s*return\s+\{"type":\s*"term\.dump"\}',
                      cli),
            'build_request deve devolver {"type":"term.dump"} sem argumentos')
    require("term.dump" in mk, "o Makefile deve registrar os testes de term.dump")
    require("test_serial_term_dump" in mk, "Makefile: alvo test_serial_term_dump")
    require("test_term_dump_contract" in mk, "Makefile: alvo test_term_dump_contract")
    require("test_term_dump_cli" in mk, "Makefile: alvo test_term_dump_cli")

    # Regressao estrutural: os types vizinhos no mesmo contrato NDJSON
    # permanecem na lista conhecida (term.dump nao pode ser removido sozinho).
    for neighbour in ("ui.dump", "screen.shot", "screen.dump", "ui.type"):
        require(f'"{neighbour}"' in bridge, f"type conhecido vizinho {neighbour} ausente")

    # device_exec trata os dumps de tela ANTES do terminal; a ordem nao pode
    # inverter e fazer term.dump cair no stub generico do host.
    require(device_exec.find('req.type == "term.dump"')
            < device_exec.find("else {"),
            "term.dump deve ser tratado antes do ramo generico (stub host)")

    print("PASS: term_dump_contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as e:
        print(f"FAIL: {e}")
        raise SystemExit(1)