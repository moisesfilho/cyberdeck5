/*
 * Host-side contract da guarda de protecao de tela na ponte serial
 * (screen.status / screen.wake e recusa dos comandos de UI).
 *
 * Superficie coberta NESTE arquivo (build host, sem FreeRTOS/LVGL/ESP_PLATFORM):
 *   TEST-SCREEN-GUARD-001: `ui_command_needs_screen_on()` enumera exatamente os
 *                          comandos que leem/operam a arvore ativa ou o
 *                          framebuffer (ui.click, ui.tap, ui.type, ui.clear,
 *                          ui.dump, term.dump, screen.shot, screen.dump) e
 *                          libera os que nao tocam a UI (ping, sys.info,
 *                          wifi.*, events.read, fs.write, ui.echo,
 *                          sys.safe_mode.clear, screen.status, screen.wake).
 *   TEST-SCREEN-GUARD-002: `screen_status_result()` devolve exatamente
 *                          {"ok":true,"off":<bool>} para cada estado.
 *   TEST-SCREEN-GUARD-003: `screen_wake_should_turn_on()` so religa quando a
 *                          tela estava apagada e `screen_wake_result()`
 *                          devolve {"ok":true,"woke":<bool>} coerente.
 *   TEST-SCREEN-GUARD-004: o envelope de recusa carrega error_code
 *                          "screen_off" e a mensagem unica
 *                          k_screen_off_detail, em UMA linha NDJSON com o rid.
 *   TEST-SCREEN-GUARD-005: ambos os tipos sao conhecidos do parse NDJSON
 *                          (k_known) e o dispatch host nao os rejeita.
 *
 * Superficie NAO coberta aqui (vive sob `#ifdef ESP_PLATFORM`, nao linkavel no
 * host) e fixada estruturalmente em test_serial_screen_guard_contract.py:
 *   - o guard dentro de device_exec() preceder qualquer hit-test/injecao/
 *     captura e retornar o erro antes de alcancar o widget;
 *   - screen.status/screen.wake chamarem screen_off_is_active() e
 *     screen_off_turn_on() (adaptador de producao) sob bsp_display_lock, porque
 *     esse estado pertence a task do LVGL (timer de inatividade).
 * O comportamento no aparelho (tela apagada -> recusa -> wake) permanece na
 * validacao manual em tests/manual/serial-bridge-validation.pt-BR.md.
 *
 * Sem hardware, sem pyserial, sem rede.
 */
#include "apps/serial/cyberdeck_serial_bridge.h"

#include <cstdio>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond) do { ++s_checks; if (!(cond)) { ++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

inline std::string esc(const std::string &s)
{
    std::string r;
    for (unsigned char c : s) {
        if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == 0) r += "\\0";
        else if (c < 32 || c >= 127) { char b[8]; std::snprintf(b, sizeof(b), "\\x%02X", c); r += b; }
        else r += char(c);
    }
    return r;
}

#define CHECK_STR_EQ(actual, expected) do { ++s_checks; if ((actual) != (expected)) { ++s_failures; std::printf("FAIL %s:%d expected '%s' actual '%s'\n", __FILE__, __LINE__, esc(expected).c_str(), esc(actual).c_str()); } } while (0)

using namespace cyberdeck_serial;

void test_ui_commands_blocked_when_screen_off()
{
    const char *const gated[] = {"ui.click", "ui.tap",     "ui.type",     "ui.clear",
                                 "ui.dump",  "term.dump",  "screen.shot", "screen.dump"};
    for (const char *type : gated) {
        CHECK(ui_command_needs_screen_on(type));
    }
    const char *const allowed[] = {"ping",
                                   "ui.echo",
                                   "sys.info",
                                   "sys.safe_mode.clear",
                                   "wifi.status",
                                   "wifi.scan",
                                   "events.read",
                                   "fs.write",
                                   "screen.status",
                                   "screen.wake",
                                   "",
                                   "unknown.type"};
    for (const char *type : allowed) {
        CHECK(!ui_command_needs_screen_on(type));
    }
    /* std::string com o mesmo conteudo deve casar (dispatch usa std::string) */
    CHECK(ui_command_needs_screen_on(std::string("ui.click")));
    CHECK(!ui_command_needs_screen_on(std::string("screen.wake")));
}

void test_screen_status_result()
{
    CHECK_STR_EQ(screen_status_result(true), "{\"ok\":true,\"off\":true}");
    CHECK_STR_EQ(screen_status_result(false), "{\"ok\":true,\"off\":false}");
}

void test_screen_wake_decision_and_result()
{
    CHECK(screen_wake_should_turn_on(true));
    CHECK(!screen_wake_should_turn_on(false));
    CHECK_STR_EQ(screen_wake_result(true), "{\"ok\":true,\"woke\":true}");
    CHECK_STR_EQ(screen_wake_result(false), "{\"ok\":true,\"woke\":false}");
    /* woke reflete exatamente "estava apagada e foi religada" */
    CHECK(screen_wake_result(screen_wake_should_turn_on(true)) == std::string("{\"ok\":true,\"woke\":true}"));
    CHECK(screen_wake_result(screen_wake_should_turn_on(false)) == std::string("{\"ok\":true,\"woke\":false}"));
}

void test_refusal_envelope()
{
    const std::string envelope = build_error_envelope("g-1", dispatch_error::screen_off, k_screen_off_detail);
    CHECK(envelope.find("{\"rid\":\"g-1\",\"ok\":false") == 0);
    CHECK(envelope.find("\"error_code\":\"screen_off\"") != std::string::npos);
    CHECK(envelope.find("\"tela apagada: use screen.wake antes\"") != std::string::npos);
    CHECK(envelope.find('\n') == std::string::npos);
    CHECK(envelope.find('\r') == std::string::npos);
    CHECK(envelope.size() <= k_max_ndjson_line);
    /* a mensagem e unica na ponte: qualquer recusa usa o mesmo detalhe */
    CHECK_STR_EQ(std::string(k_screen_off_detail), "tela apagada: use screen.wake antes");
    /* codigo de erro distinto dos ja existentes (nao colide com `internal`) */
    CHECK(dispatch_error::screen_off != dispatch_error::internal);
    CHECK(dispatch_error::screen_off != dispatch_error::none);
    /* valores historicos preservados (enum aditivo no fim) */
    CHECK(static_cast<int>(dispatch_error::too_large) == 1);
    CHECK(static_cast<int>(dispatch_error::internal) == 7);
    CHECK(static_cast<int>(dispatch_error::io_error) == 10);
    CHECK(static_cast<int>(dispatch_error::screen_off) == 11);
}

void test_types_known_and_dispatch_host()
{
    /* Ambos precisam estar em k_known; senao o parse devolve unknown_type. */
    for (const char *type : {"screen.status", "screen.wake"}) {
        const std::string line = std::string("{\"rid\":\"k1\",\"type\":\"") + type + "\"}";
        request req;
        dispatch_error err = dispatch_error::invalid_json;
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.type == type);
        /* dispatch host nao rejeita o tipo (stub {"ok":true} no host) */
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(res.err == dispatch_error::none);
        CHECK(res.rid == "k1");
        CHECK(res.envelope_json.find("\"ok\":true") != std::string::npos);
    }
}

} // namespace

int main()
{
    test_ui_commands_blocked_when_screen_off();
    test_screen_status_result();
    test_screen_wake_decision_and_result();
    test_refusal_envelope();
    test_types_known_and_dispatch_host();
    std::printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
