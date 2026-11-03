#!/usr/bin/env python3
"""Contrato estrutural da guarda de protecao de tela na ponte serial.

`device_exec()` (recusa + screen.status + screen.wake) vive sob
`#ifdef ESP_PLATFORM` e depende de LVGL/FreeRTOS/BSP, logo nao e linkavel no
host. A parte pura (lista de comandos afetados e formatacao das respostas) e
exercitada por `test_serial_screen_guard.cpp`; este contrato fixa, sobre as
fontes reais, o que o build host nao alcanca:

  TEST-SCREEN-GUARD-010: o guard fica no inicio de device_exec() e PRECEDE
                         qualquer hit-test, injecao de teclado ou captura
                         (ui.click/ui.tap/ui.type/ui.clear/ui.dump/term.dump/
                         screen.shot/screen.dump), retornando o erro tipado
                         antes de alcancar o widget escondido.
  TEST-SCREEN-GUARD-011: screen.status responde a partir de
                         screen_off_is_active() e screen.wake so chama
                         screen_off_turn_on() quando a tela estava apagada,
                         reportando `woke` coerente.
  TEST-SCREEN-GUARD-012: comandos que nao tocam a UI ficam fora da lista
                         fechada; nada que MUDA a protecao (screen on/off/
                         timeout) ganha side channel serial.
  TEST-SCREEN-GUARD-013: header/CLI/code-map/Makefile documentam e registram
                         os dois comandos novos e a lista afetada.
  TEST-SCREEN-GUARD-014: screen.status e screen.wake leem
                         screen_off_is_active() e (no wake) executam
                         screen_off_turn_on() DENTRO de bsp_display_lock com
                         k_display_lock_timeout_ms; a resposta e composta
                         depois do unlock e a falha do lock responde
                         dispatch_error::internal "display busy". Nenhuma
                         chamada a screen_off_turn_on() existe fora do bloco.
                         Mutacao (remover o bsp_display_lock) deve falhar aqui.

Sem hardware, sem pyserial, sem rede.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
BRIDGE_CPP = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
BRIDGE_H = ROOT / "components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h"
CLI = ROOT / "tools/cyberdeck_cli.py"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODEMAP = ROOT / "code-map.md"

GATED = ("ui.click", "ui.tap", "ui.type", "ui.clear", "ui.dump", "term.dump",
         "screen.shot", "screen.dump")
ALLOWED = ("ping", "ui.echo", "sys.info", "sys.safe_mode.clear", "wifi.status",
           "wifi.scan", "events.read", "fs.write", "screen.status",
           "screen.wake")

# Chamadas que so podem ocorrer DEPOIS do guard (ordem no corpo real).
UI_SIDE_EFFECTS = ("find_click_at(", "find_clickable_by_text(", "lv_async_call(",
                   "exec_ui_type(", "exec_ui_clear()", "dump_obj(",
                   "exec_term_dump(", "exec_screen_shot(", "exec_screen_dump(")


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


def function_body(src, signature):
    """Corpo de uma DEFINICAO por balanceamento de chaves (ignora prototipos)."""
    search_at = 0
    while True:
        at = src.find(signature, search_at)
        require(at >= 0, "assinatura ausente: %s" % signature)
        open_at = src.find("{", at)
        require(open_at >= 0, "corpo ausente para: %s" % signature)
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
                    return src[open_at:i + 1]
        raise AssertionError("corpo nao balanceado para: %s" % signature)


def block_after(src, at):
    open_at = src.find("{", at)
    require(open_at >= 0, "bloco ausente")
    depth = 0
    for i in range(open_at, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_at:i + 1]
    raise AssertionError("bloco nao balanceado")


def main():
    for path in (BRIDGE_CPP, BRIDGE_H, CLI, MAKEFILE, CODEMAP):
        require(path.exists(), "artefato obrigatorio ausente: %s" % path)
    bridge = BRIDGE_CPP.read_text(encoding="utf-8", errors="ignore")
    header = BRIDGE_H.read_text(encoding="utf-8", errors="ignore")
    cli = CLI.read_text(encoding="utf-8", errors="ignore")
    mk = MAKEFILE.read_text(encoding="utf-8", errors="ignore")
    codemap = CODEMAP.read_text(encoding="utf-8", errors="ignore")

    device_exec = function_body(bridge, "device_result device_exec(const request &req")

    # =================================================================
    # TEST-SCREEN-GUARD-010
    # =================================================================
    require(re.search(r'ui_command_needs_screen_on\s*\(\s*req\.type\s*\)', device_exec),
            "device_exec deve consultar ui_command_needs_screen_on(req.type)")
    require(re.search(r'ui_command_needs_screen_on\s*\(\s*req\.type\s*\)\s*&&\s*'
                      r'screen_off_is_active\s*\(\s*\)', device_exec),
            "o guard deve exigir screen_off_is_active() junto da lista de comandos")
    require(re.search(r'err_result\s*\(\s*req\s*,\s*dispatch_error::screen_off\s*,\s*'
                      r'k_screen_off_detail\s*\)', device_exec),
            "o guard deve responder dispatch_error::screen_off com k_screen_off_detail")
    require(re.search(r'k_screen_off_detail\[\]\s*=\s*"tela apagada: use screen\.wake antes"', header),
            "k_screen_off_detail deve manter a mensagem 'tela apagada: use screen.wake antes'")

    guard_at = device_exec.find("ui_command_needs_screen_on(req.type)")
    require(guard_at >= 0, "guard ausente em device_exec")
    guard_body = block_after(device_exec, guard_at)
    require("return d;" in guard_body,
            "o guard deve retornar antes de qualquer efeito colateral")
    for token in ("screen_off_turn_on(", "exec_ui_type(", "exec_ui_clear()",
                  "dump_obj(", "exec_term_dump(", "exec_screen_dump(",
                  "exec_screen_shot(", "lv_async_call("):
        require(token not in guard_body,
                "o guard nao pode executar %s antes de recusar" % token)
    for token in UI_SIDE_EFFECTS:
        at = device_exec.find(token)
        require(at >= 0, "device_exec deve manter %s" % token)
        require(guard_at < at,
                "guard deve preceder %s no corpo de device_exec" % token)

    # =================================================================
    # TEST-SCREEN-GUARD-011
    # =================================================================
    status_at = device_exec.find('req.type == "screen.status"')
    require(status_at >= 0, "device_exec deve ter ramo req.type == \"screen.status\"")
    status_body = block_after(device_exec, status_at)
    require("ui_command_needs_screen_on" not in status_body,
            "screen.status nao pode ser barrado pelo proprio guard")
    status_read = re.search(r"const\s+bool\s+(\w+)\s*=\s*screen_off_is_active\s*\(\s*\)\s*;",
                            status_body)
    require(status_read is not None,
            "screen.status deve ler screen_off_is_active() uma vez, em variavel local")
    require(re.search(r"screen_status_result\s*\(\s*%s\s*\)" % re.escape(status_read.group(1)),
                      status_body),
            "screen.status deve responder screen_status_result() com o estado lido")

    wake_at = device_exec.find('req.type == "screen.wake"')
    require(wake_at >= 0, "device_exec deve ter ramo req.type == \"screen.wake\"")
    wake_body = block_after(device_exec, wake_at)
    wake_read = re.search(r"const\s+bool\s+(\w+)\s*=\s*screen_off_is_active\s*\(\s*\)\s*;",
                          wake_body)
    require(wake_read is not None,
            "screen.wake deve consultar screen_off_is_active() uma vez, em variavel local")
    gate_at = wake_body.find("screen_wake_should_turn_on(")
    turn_on_at = wake_body.find("screen_off_turn_on()")
    require(gate_at >= 0 and turn_on_at >= 0,
            "screen.wake deve religar via screen_off_turn_on() sob a decisao pura")
    require(gate_at < turn_on_at,
            "screen_off_turn_on() deve ficar dentro da decisao screen_wake_should_turn_on()")
    require(wake_body.count("screen_off_turn_on()") == 1,
            "screen.wake deve chamar screen_off_turn_on() uma unica vez")
    require(re.search(r"screen_wake_result\s*\(\s*%s\s*\)" % re.escape(wake_read.group(1)),
                      wake_body),
            "screen.wake deve reportar woke via screen_wake_result() com o estado lido")

    # =================================================================
    # TEST-SCREEN-GUARD-014
    # `screen.status`/`screen.wake` leem s_active e (no wake) trocam a tela
    # ativa; esse estado pertence a task do LVGL (o timer de inatividade le
    # s_active ali). Sem bsp_display_lock a leitura/ação corre fora do contexto
    # do LVGL (defeito B1). O lock e a acao precisam estar no MESMO bloco
    # protegido e a resposta composta depois do unlock.
    # =================================================================
    for name, body in (("screen.status", status_body), ("screen.wake", wake_body)):
        lock_at = body.find("bsp_display_lock(")
        require(lock_at >= 0,
                "%s deve tomar bsp_display_lock antes de tocar a protecao de tela" % name)
        require("k_display_lock_timeout_ms" in body[lock_at:body.find(")", lock_at) + 1],
                "%s deve usar o timeout k_display_lock_timeout_ms do dispatcher LVGL" % name)
        read_at = body.find("screen_off_is_active()")
        require(read_at > lock_at,
                "%s deve ler screen_off_is_active() SOB bsp_display_lock" % name)
        unlock_at = body.find("bsp_display_unlock()", read_at)
        require(unlock_at > read_at,
                "%s deve liberar o display (bsp_display_unlock) apos a leitura" % name)
        require(body.count("bsp_display_lock(") == 1 and body.count("bsp_display_unlock()") == 1,
                "%s deve ter exatamente um lock e um unlock" % name)
        busy_at = body.find('"display busy"')
        require(busy_at >= 0 and
                re.search(r'err_result\s*\(\s*req\s*,\s*dispatch_error::internal\s*,\s*'
                          r'"display busy"\s*\)', body) is not None,
                "%s deve responder dispatch_error::internal \"display busy\" se o lock falhar" % name)
        require(busy_at < read_at,
                "%s nao pode ler a protecao quando o lock falha" % name)
        result_at = body.find("ok_result(req,")
        require(result_at > unlock_at,
                "%s deve compor a resposta depois do unlock" % name)

    wake_lock_at = wake_body.find("bsp_display_lock(")
    wake_unlock_at = wake_body.find("bsp_display_unlock()", wake_lock_at)
    require(wake_lock_at < wake_body.find("screen_off_turn_on()") < wake_unlock_at,
            "screen_off_turn_on() deve executar DENTRO do bsp_display_lock")
    # Nenhuma outra chamada a screen_off_turn_on() na ponte: fora do bloco
    # protegido nao ha caminho legitimo para trocar a tela ativa.
    require(bridge.count("screen_off_turn_on()") == 1,
            "a ponte deve chamar screen_off_turn_on() apenas no wake sob bsp_display_lock")

    # =================================================================
    # TEST-SCREEN-GUARD-012
    # =================================================================
    guarded = function_body(bridge, "bool ui_command_needs_screen_on")
    for type_name in GATED:
        require('"%s"' % type_name in guarded,
                "lista fechada deve conter %r" % type_name)
    for type_name in ALLOWED:
        require('"%s"' % type_name not in guarded,
                "lista fechada nao pode conter %r" % type_name)
    for forbidden in ("screen_off_turn_off", "screen_off_set_timeout_minutes"):
        require(forbidden not in bridge,
                "a ponte nao pode ganhar side channel para %s (use ui.type)" % forbidden)
    require('"screen.status"' in re.search(r"k_known\[\][^;]*;", bridge, re.S).group(0) and
            '"screen.wake"' in re.search(r"k_known\[\][^;]*;", bridge, re.S).group(0),
            "screen.status/screen.wake devem estar em k_known")

    # =================================================================
    # TEST-SCREEN-GUARD-013
    # =================================================================
    doc = header[:header.find("namespace cyberdeck_serial")]
    require("screen.status" in doc and "screen.wake" in doc,
            "header deve documentar screen.status/screen.wake")
    for type_name in GATED:
        require(type_name in doc,
                "header deve listar %r entre os comandos recusados" % type_name)
    require("tela apagada" in doc, "header deve explicar a recusa da protecao de tela")

    for name in ("screen.status", "screen.wake"):
        require('"%s"' % name in cli, "CLI deve expor %s" % name)
        require(name in codemap, "code-map deve documentar %s" % name)

    for target in ("test_serial_screen_guard", "test_serial_screen_guard_contract"):
        require(target + ":" in mk, "Makefile deve registrar o alvo %s" % target)
    require(re.search(r"(?m)^\s*@\$\(MAKE\) --no-print-directory test_serial_screen_guard$", mk),
            "o agregado `test` deve executar a guarda de protecao de tela")
    require(re.search(r"(?m)^\s*@\$\(MAKE\) --no-print-directory test_serial_screen_guard_contract$", mk),
            "o agregado `test` deve executar o contrato estrutural da guarda")
    require("guard" in codemap and "dispatch_error::screen_off" in codemap,
            "code-map deve descrever a guarda e o erro tipado screen_off")

    print("test_serial_screen_guard_contract: OK")


if __name__ == "__main__":
    main()
