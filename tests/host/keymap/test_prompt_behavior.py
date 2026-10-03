#!/usr/bin/env python3
"""Host-side behavioural checks for the shell console prompt.

The prompt, the visible line and the UTF-8 helpers belong to the foreground
shell application (cyberdeck_shell_console), which is pure and host-linkable.
These tests compile and run the real production translation unit instead of
reimplementing its behaviour or copying bodies out of the LVGL UI.
"""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
CONSOLE_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp"
RUNTIME_SRC = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
FILTER_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_terminal_filter.cpp"
PRODUCTION_SOURCES = [str(CONSOLE_SRC), str(RUNTIME_SRC)]
LOCAL_SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
VFS_NAMESPACE = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp"
LIMIT = 12288

RENDERED_SIGNATURE = "std::string get_rendered_output(const cyberdeck_shell_console::line_view &view)"


def compile_and_run(directory: str, name: str, source: str,
                    sources: list[str]) -> None:
    """Compile one harness against the real production translation units."""
    root = Path(directory)
    cpp = root / f"{name}.cpp"
    binary = root / name
    cpp.write_text(source, encoding="utf-8")
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/cyberdeck/include"),
        str(cpp), *sources, "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)


def helper_harness() -> str:
    """Compile the real console helpers straight out of the production TU.

    The helpers moved from the LVGL UI to cyberdeck_shell_console, so the
    production translation unit is compiled here instead of copying bodies.
    """
    return """#include "apps/shell/cyberdeck_shell_console.h"
#include <cassert>
#include <cstddef>
#include <string>
using namespace cyberdeck_shell_console;
static constexpr size_t TERMINAL_LIMIT = k_terminal_limit;
static_assert(k_terminal_limit == 12288, "the console must keep the 12288 byte bound");
""" + r'''
int main() {
    const std::string two = "aa\xC3\xA9zz";
    const std::string three = "aa\xE2\x82\xACzz";
    const std::string four = "aa\xF0\x9F\x94\xA5zz";
    // A byte budget landing in each UTF-8 sequence must move right to a
    // codepoint boundary, never emit an invalid leading continuation byte.
    assert(truncate_left_utf8(two, 3) == "zz");
    assert(truncate_left_utf8(three, 4) == "zz");
    assert(truncate_left_utf8(four, 5) == "zz");
    assert(truncate_left_utf8(two, 0).empty());
    assert(truncate_left_utf8(three, 0).empty());
    assert(truncate_left_utf8(four, 0).empty());

    const std::string long_cwd(TERMINAL_LIMIT + 32, 'c');
    const std::string prompt = fit_prompt_marker(long_cwd + "$ ");
    assert(prompt.size() <= TERMINAL_LIMIT);
    assert(prompt.size() >= 2);
    assert(prompt.compare(prompt.size() - 2, 2, "$ ") == 0);

    // The cursor is measured against the fitted suffix, not the hidden
    // prefix.  Exercise both the truncation boundary and codepoint boundaries
    // inside the visible suffix (not merely the byte-counting helper).
    const std::string line(TERMINAL_LIMIT - 8, 'x');
    const std::string tail = "\xE2\x82\xAC\xF0\x9F\x94\xA5tail";
    const std::string full = line + tail;
    const std::string fitted = fit_visible_line(full, 0);
    const size_t line_start = full.size() - fitted.size();
    assert(line_start == 3);
    size_t cursor_bytes = 1;
    if (cursor_bytes < line_start) cursor_bytes = line_start;
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == 0);
    const size_t euro = fitted.find("\xE2\x82\xAC");
    const size_t fire = fitted.find("\xF0\x9F\x94\xA5");
    assert(euro != std::string::npos && fire == euro + 3);
    cursor_bytes = line_start + euro + 3; // immediately after the euro sign
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == euro + 1);
    cursor_bytes = line_start + fire + 4; // immediately after the fire emoji
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == euro + 2);
    cursor_bytes = full.size();
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == utf8_char_count(fitted));
    return 0;
}
'''


def prompt_harness() -> str:
    """Compile the production compose() and assert the whole prompt matrix.

    REQ-1/AC-1: the remote prompt is preserved literally in the scrollback and
    the console never synthesizes a prompt for it.  There is no fixed marker, no
    inference, and while SSH is connected not even the local cwd prompt: the tail
    shows only the edited line, so `marker` can only ever be the empty string, the
    password prompt, or the local cwd prompt of a disconnected console.
    """
    return r'''#include "apps/shell/cyberdeck_shell_console.h"
#include <cassert>
#include <string>
using namespace cyberdeck_shell_console;

static line_input typed(const std::string &line, size_t cursor)
{
    line_input input;
    input.line = line;
    input.visible_line = line;
    input.cursor_bytes = cursor;
    return input;
}

static std::string marker_for(bool connected, bool password, bool owned_elsewhere,
                              const std::string &cwd)
{
    surface_state state;
    state.ssh_connected = connected;
    state.password_pending = password;
    state.input_owned_elsewhere = owned_elsewhere;
    state.cwd = cwd;
    return compose(state, typed("cmd", 3)).marker;
}

int main() {
    /* REQ-1/AC-1: a matriz de marcadores nao mudou de cardinalidade, mudou de
     * contrato.  O console local continua no menu, a senha continua com o seu
     * proprio marcador mascarado, e outro dono da entrada continua sem prompt. */
    assert(marker_for(false, false, false, "/") == "/$ ");
    assert(marker_for(false, false, false, "/data") == "/data$ ");
    assert(marker_for(false, false, true, "/") == "");
    assert(marker_for(false, true, true, "/") == "Password: ");
    assert(marker_for(false, true, false, "/secret") == "Password: ");

    /* REQ-1/AC-1: com SSH online NAO existe marcador local nem fixo.  O prompt
     * remoto chega pelo fluxo remoto e e preservado literal pelo filtro; fabricar
     * o prompt do cwd do deck mostraria dois prompts na tela e um deles mentiria
     * sobre o host remoto, entao a cauda conectada nao tem prompt nenhum. */
    assert(marker_for(true, false, false, "/secret").empty());
    assert(marker_for(true, false, true, "/secret").empty());
    /* Ausencia de prompt e um estado legitimo, tambem com o SSH conectado. */
    assert(marker_for(true, false, true, "/secret").empty());
    /* O cwd do deck nao vaza para a superficie conectada em nenhum dos cantos do
     * dominio: raiz, caminho profundo e cwd vazio seguem sem prompt. */
    assert(marker_for(true, false, false, "/").empty());
    assert(marker_for(true, false, false, "/very/secret/path").empty());
    assert(marker_for(true, false, false, "").empty());
    /* A senha continua tendo precedencia absoluta sobre a supressao do
     * connected: o mascaramento nunca e desligado por estar em sessao SSH. */
    assert(marker_for(true, true, false, "/secret") == "Password: ");
    assert(marker_for(true, true, true, "/secret") == "Password: ");

    /* REQ-1/AC-1: nenhuma combinacao de superficie produz um marcador fixo nem
     * um prompt remoto sintetizado.  O marcador so pode ser vazio, "Password: "
     * ou o prompt do cwd terminado em "$ ". */
    const bool surfaces[][3] = {
        {false, false, false}, {false, false, true},
        {false, true, false},  {false, true, true},
        {true, false, false},  {true, false, true},
        {true, true, false},   {true, true, true},
    };
    const char *cwds[] = {"/", "/data", "/very/secret/path", ""};
    for (const auto &surface : surfaces) {
        for (const char *cwd : cwds) {
            const std::string marker =
                marker_for(surface[0], surface[1], surface[2], cwd);
            assert(marker.find("ssh> ") == std::string::npos);
            assert(marker.find("ssh") == std::string::npos);
            assert(marker.find("root@") == std::string::npos);
            const bool password = marker == "Password: ";
            const bool local_prompt =
                marker.empty() ||
                (marker.size() >= 2 && marker.compare(marker.size() - 2, 2, "$ ") == 0);
            assert(password || local_prompt);
            /* A senha tem precedencia sobre o prompt local. */
            if (surface[1]) assert(password);
            /* Nenhum outro dono da entrada pode aplicar o prompt local. */
            if (surface[2] && !surface[1]) assert(marker.empty());
            /* REQ-1/AC-1: uma sessao SSH aberta nunca fabrica prompt local: nem
             * o prompt do cwd do deck, em nenhum dos quatro cantos do dominio. */
            if (surface[0] && !surface[1]) assert(marker.empty());
            /* O prompt local so existe no console local, com dono local. */
            if (!surface[0] && !surface[1] && !surface[2]) {
                assert(marker == fit_prompt_marker(std::string(cwd) + "$ "));
            }
        }
    }

    /* REQ-1/AC-1 + AC-003: com o SSH online a linha remota e exibida sem
     * mascarar, sem reescrever e sem prompt local algum. */
    surface_state connected;
    connected.ssh_connected = true;
    connected.cwd = "/secret";
    const line_view remote = compose(connected, typed("pwd", 3));
    assert(remote.marker.empty());
    assert(remote.text() == "pwd");
    /* O cwd do deck nao aparece na cauda conectada: nenhum prompt local e
     * fabricado, de modo que o prompt remoto do scrollback fica como unico. */
    assert(remote.text().find("/secret") == std::string::npos);
    assert(remote.text().find("$ ") == std::string::npos);
    assert(remote.fitted_line == "pwd");
    assert(remote.visible_line == "pwd");
    assert(remote.text().find("ssh> ") == std::string::npos);
    assert(remote.reserved() == remote.text().size());

    /* Um outro dono da entrada nao muda nada em cima do conectado: a cauda e a
     * mesma, sem residuo de prompt em nenhuma das duas superficies. */
    connected.input_owned_elsewhere = true;
    const line_view promptless = compose(connected, typed("pwd", 3));
    assert(promptless.marker.empty());
    assert(promptless.text() == "pwd");
    assert(promptless.fitted_line == "pwd");
    assert(promptless.text() == remote.text());
    connected.input_owned_elsewhere = false;

    /* AC-002: sem prompt local a aritmetica do cursor relativo mede so a linha
     * remota visivel, e cai no inicio da linha quando o cursor esta em zero. */
    const size_t marker_chars = utf8_char_count(remote.marker);
    assert(marker_chars == 0); /* a cauda conectada nao reserva prompt nenhum */
    assert(remote.cursor_chars() == marker_chars + 3);
    assert(compose(connected, typed("pwd", 0)).cursor_chars() == marker_chars);

    /* AC-002: o console local continua contando os codepoints do prompt local
     * mais os da janela visivel; a supressao do modo conectado nao pode ter
     * derrubado essa aritmetica. */
    surface_state local;
    local.cwd = "/secret";
    const line_view local_line = compose(local, typed("pwd", 3));
    const size_t local_marker_chars = utf8_char_count(local_line.marker);
    assert(local_line.marker == "/secret$ ");
    assert(local_marker_chars == 9);
    assert(local_line.text() == "/secret$ pwd");
    assert(local_line.cursor_chars() == local_marker_chars + 3);
    assert(compose(local, typed("pwd", 0)).cursor_chars() == local_marker_chars);

    /* AC-002: UTF-8 conta codepoints, nunca bytes, e o prefixo oculto nunca
     * entra na contagem.  Vale nas duas superficies: prompt local mais linha, e
     * linha conectada sem prompt. */
    const std::string utf8_line = "p\xC3\xA9\xE2\x82\xAC";
    const size_t stops[][2] = {{0, 0}, {1, 1}, {3, 2}, {6, 3}};
    for (const auto &stop : stops) {
        assert(compose(connected, typed(utf8_line, stop[0])).cursor_chars() ==
               marker_chars + stop[1]);
        assert(compose(local, typed(utf8_line, stop[0])).cursor_chars() ==
               local_marker_chars + stop[1]);
    }

    /* AC-002/limite: o orcamento visivel desconta os bytes do prompt, o
     * scrollback e aparado pelo que a cauda reserva de fato e um cursor no
     * prefixo oculto clampa no inicio da janela visivel. */
    line_input huge;
    huge.line = std::string(k_terminal_limit + 40, 'x');
    huge.visible_line = huge.line;
    huge.cursor_bytes = 1;
    const line_view bounded = compose(connected, huge);
    assert(bounded.marker.empty());
    assert(bounded.line_start > 0);
    assert(bounded.cursor_bytes == bounded.line_start);
    assert(bounded.cursor_chars() == marker_chars);
    /* Conectado, o orcamento inteiro vai para a linha remota: sem prompt local
     * para descontar, a linha ajustada pode ocupar o limite cheio. */
    assert(bounded.fitted_line.size() <= k_terminal_limit - bounded.marker.size());
    assert(bounded.fitted_line.size() == k_terminal_limit);
    assert(bounded.text().size() <= k_terminal_limit);
    assert(bounded.text().size() == k_terminal_limit);
    assert(bounded.reserved() == bounded.text().size());
    assert(bounded.reserved() == bounded.marker.size() + bounded.fitted_line.size());

    /* O console local continua descontando os bytes do prompt do orcamento
     * visivel e do scrollback, e o cursor no prefixo oculto clampa igual. */
    const line_view bounded_local = compose(local, huge);
    assert(bounded_local.marker == "/secret$ ");
    assert(bounded_local.line_start > 0);
    assert(bounded_local.cursor_bytes == bounded_local.line_start);
    assert(bounded_local.cursor_chars() == local_marker_chars);
    assert(bounded_local.fitted_line.size() <= k_terminal_limit - bounded_local.marker.size());
    assert(bounded_local.fitted_line.size() == k_terminal_limit - local_line.marker.size());
    assert(bounded_local.text().size() <= k_terminal_limit);
    assert(bounded_local.reserved() == bounded_local.text().size());
    assert(bounded_local.reserved() == bounded_local.marker.size() + bounded_local.fitted_line.size());

    /* Um cursor alem do fim da linha clampa no tamanho da linha, nas duas
     * superficies. */
    assert(compose(connected, typed("pwd", 99)).cursor_bytes == 3);
    assert(compose(local, typed("pwd", 99)).cursor_bytes == 3);

    /* REQ-3: senha continua mascarada, com marcador proprio e cwd vedado. */
    surface_state password;
    password.password_pending = true;
    password.cwd = "/secret";
    const line_view secret = compose(password, typed("hunter2", 7));
    assert(secret.marker == "Password: ");
    assert(secret.fitted_line == "*******");
    assert(secret.fitted_line.find('h') == std::string::npos);
    assert(secret.text().find("secret") == std::string::npos);
    assert(secret.text().find("ssh> ") == std::string::npos);
    return 0;
}
'''


def test_utf8_limits_and_cursor() -> None:
    with tempfile.TemporaryDirectory(prefix="cyberdeck-prompt-") as directory:
        compile_and_run(directory, "prompt_helpers", helper_harness(),
                        PRODUCTION_SOURCES)


def test_prompt_matrix_executes_production_composition() -> None:
    with tempfile.TemporaryDirectory(prefix="cyberdeck-prompt-") as directory:
        compile_and_run(directory, "prompt", prompt_harness(), PRODUCTION_SOURCES)


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def _slice(body: str, begin: str, end: str) -> str:
    first = body.index(begin)
    return body[first:body.index(end, first) + len(end)]


def extracted_rendered_tail() -> str:
    """Verbatim production statements for the scrollback budget and the LF rule.

    `get_rendered_output()` lives in the LVGL view, which is not host-linkable.
    Lifting the two production statements *as source text* keeps the assertions
    below behavioural against production: any change to the budget, to the
    `needs_visual_separator` guard or to the push fails this test, because the
    extracted text is what gets compiled and run.

    The Wi-Fi/BLE overlay region between the two statements is dropped.  It is
    dropped only after proving it can merely append to `output`, so the extracted
    pair is the whole of the scrollback-to-view transformation under test.
    """
    body = _function_body(UI.read_text(encoding="utf-8"), RENDERED_SIGNATURE)
    budget = _slice(body, "const size_t used = view.reserved();",
                    "std::string output = s_output;")
    rule = _slice(body, "const bool needs_visual_separator", "push_back('\\n');")

    anchor = "std::string output = s_output;"
    overlay = body[body.index(anchor) + len(anchor):
                   body.index("const bool needs_visual_separator")]
    for forbidden in ("push_back", "pop_back", "clear()", "resize", "output = "):
        if forbidden in overlay:
            raise AssertionError(f"overlay region is not append-only: {forbidden}")
    if body.count("push_back") != 1:
        raise AssertionError("the visual LF must be the only byte the rule appends")
    tail = body[body.index("push_back('\\n');") + len("push_back('\\n');"):]
    if tail.strip() != "return output;":
        raise AssertionError("nothing may run between the LF rule and the return")
    return budget + "\n" + rule


def rendered_output_harness(tail: str) -> str:
    """Compile the extracted production rule and drive the LF matrix.

    REQ-2/AC-2: exactly one visual LF, appended only when the normalized output
    does not already end in one, bounded by the bytes the composed tail really
    reserves, and idempotent across repeated renders.
    """
    return r'''#include "apps/shell/cyberdeck_shell_console.h"
#include "apps/shell/cyberdeck_terminal_filter.h"
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>
using cyberdeck_shell_console::line_view;
using cyberdeck_shell_console::truncate_left_utf8;
using cyberdeck_shell_console::utf8_char_count;
static constexpr size_t TERMINAL_LIMIT = cyberdeck_shell_console::k_terminal_limit;
static constexpr size_t k_marker_bytes = 7; /* "/data$ " */

/* Production statements, extracted verbatim from get_rendered_output(). */
static std::string rendered(const line_view &view, const std::string &s_output)
{
''' + tail + r'''
    return output;
}

/* The view whose composed tail reserves exactly TERMINAL_LIMIT - available
 * bytes, leaving `available` bytes of scrollback budget.  Suppressing the local
 * prompt is the only way to reserve nothing, and it is a real surface state. */
static line_view view_for_available(size_t available)
{
    cyberdeck_shell_console::surface_state state;
    state.cwd = "/data";
    const size_t marker_bytes =
        available <= TERMINAL_LIMIT - k_marker_bytes ? k_marker_bytes : 0;
    state.input_owned_elsewhere = marker_bytes == 0;
    const size_t line_bytes = TERMINAL_LIMIT - available - marker_bytes;
    cyberdeck_shell_console::line_input input;
    input.line = std::string(line_bytes, 'x');
    input.visible_line = input.line;
    input.cursor_bytes = input.line.size();
    const line_view view = cyberdeck_shell_console::compose(state, input);
    assert(view.marker.size() == marker_bytes);
    assert(view.reserved() == TERMINAL_LIMIT - available);
    return view;
}

/* REQ-1/AC-1: the same budget while an SSH session is connected.  The connected
 * tail is prompt-free, so reserved() discounts nothing and the whole budget goes
 * to the edited line.  This is the surface that changed, so the LF rule must be
 * re-proved against it rather than assumed. */
static line_view connected_view_for_available(size_t available)
{
    cyberdeck_shell_console::surface_state state;
    state.ssh_connected = true;
    state.cwd = "/data";
    const size_t line_bytes = TERMINAL_LIMIT - available;
    cyberdeck_shell_console::line_input input;
    input.line = std::string(line_bytes, 'x');
    input.visible_line = input.line;
    input.cursor_bytes = input.line.size();
    const line_view view = cyberdeck_shell_console::compose(state, input);
    assert(view.marker.empty());
    assert(view.reserved() == TERMINAL_LIMIT - available);
    assert(view.reserved() == view.text().size());
    return view;
}

/* Oracle for REQ-2, stated from the acceptance criterion and deliberately not
 * derived from the production statements under test. */
static std::string expected(size_t available, const std::string &scrollback)
{
    if (scrollback.empty()) return {};
    if (scrollback.back() == '\n') {
        return scrollback.size() <= available
                   ? scrollback
                   : truncate_left_utf8(scrollback, available);
    }
    if (available == 0) return {};
    std::string base = scrollback.size() > available - 1
                           ? truncate_left_utf8(scrollback, available - 1)
                           : scrollback;
    if (base.size() < available) base.push_back('\n');
    return base;
}

static void check_view(const line_view &view, size_t available, const std::string &scrollback)
{
    assert(view.reserved() == TERMINAL_LIMIT - available);

    const std::string once = rendered(view, scrollback);
    assert(once == expected(available, scrollback));
    /* AC-002: idempotencia.  A segunda passagem nao acrescenta nada, porque a
     * primeira ja terminou em LF sempre que acrescentou um. */
    assert(rendered(view, once) == once);
    /* O scrollback nunca invade o orcamento reservado pela cauda composta. */
    assert(once.size() <= available);
    /* AC-004: scrollback + cauda composta cabem no limite do terminal, e o
     * cursor relativo continua apontando para dentro da area renderizada. */
    assert(once.size() + view.text().size() <= TERMINAL_LIMIT);
    assert(utf8_char_count(once) + view.cursor_chars() <=
           utf8_char_count(once + view.text()));
}

static void check_case(size_t available, const std::string &scrollback)
{
    check_view(view_for_available(available), available, scrollback);
}

/* REQ-1/AC-1: the identical matrix re-proved against the connected surface. */
static void check_connected_case(size_t available, const std::string &scrollback)
{
    check_view(connected_view_for_available(available), available, scrollback);
}

static std::string filter_all(const std::string &raw)
{
    cyberdeck_terminal_filter filter;
    std::string out;
    std::vector<char> buffer(raw.size() + 2);
    const size_t written = filter.feed(raw.data(), raw.size(), buffer.data(),
                                       buffer.size());
    out.append(buffer.data(), written);
    const size_t flushed = filter.flush(buffer.data(), buffer.size());
    out.append(buffer.data(), flushed);
    return out;
}

static std::string filter_partitioned(const std::string &raw, size_t step)
{
    cyberdeck_terminal_filter filter;
    std::string out;
    std::vector<char> buffer(raw.size() + 2);
    for (size_t offset = 0; offset < raw.size(); offset += step) {
        const size_t chunk = step < raw.size() - offset ? step : raw.size() - offset;
        const size_t written =
            filter.feed(raw.data() + offset, chunk, buffer.data(), buffer.size());
        out.append(buffer.data(), written);
    }
    const size_t flushed = filter.flush(buffer.data(), buffer.size());
    out.append(buffer.data(), flushed);
    return out;
}

int main() {
    /* Ausencia de prompt e de saida: nada e inventado quando nao ha scrollback. */
    check_case(TERMINAL_LIMIT, "");
    check_case(0, "");

    /* REQ-2/AC-2: saida que ja termina em LF nao recebe um segundo LF. */
    check_case(TERMINAL_LIMIT, "out\n");
    check_case(TERMINAL_LIMIT, "out\n\n");
    check_case(TERMINAL_LIMIT, "user@host:~$ ls\n");
    check_case(16, "0123456789abcde\n");   /* exatamente o orcamento */
    check_case(16, "0123456789abcdef\n");  /* um byte acima: apara, sem LF extra */
    check_case(1, "\n");

    /* REQ-2/AC-2: saida sem LF recebe exatamente um LF visual. */
    check_case(TERMINAL_LIMIT, "out");
    check_case(TERMINAL_LIMIT, "user@host:~$ ");
    check_case(TERMINAL_LIMIT, "sem quebra de linha no fim");
    check_case(16, "0123456789abcde");      /* um byte abaixo do orcamento */
    check_case(16, "0123456789abcdef");     /* exatamente o orcamento */
    check_case(16, "0123456789abcdefghij"); /* acima do orcamento */
    check_case(1, "x");                     /* orcamento de um byte */
    check_case(1, "xy");                    /* apara e ainda assim fecha em LF */
    check_case(2, "xy");

    /* Limite: com a cauda ocupando tudo, nao sobra orcamento e nada e escrito. */
    check_case(0, "out");
    check_case(0, "out\n");
    check_case(0, "user@host:~$ ");

    /* Varredura de limites: nenhum tamanho de scrollback pode estourar o
     * orcamento, duplicar o LF ou deixar de termina-lo. */
    for (size_t available = 0; available <= 64; ++available) {
        for (size_t length = 0; length <= 80; ++length) {
            const std::string scrollback(length, 'y');
            check_case(available, scrollback);
            check_case(available, scrollback + "\n");
        }
    }

    /* REQ-1/AC-1 + REQ-2: o prompt remoto chega literal pelo fluxo remoto e a
     * saida normalizada e o que decide se existe um LF visual. */
    const line_view view = view_for_available(64);
    assert(rendered(view, "") == "");
    assert(rendered(view, "root@host:~# ") == "root@host:~# \n");
    assert(rendered(view, "root@host:~# \n") == "root@host:~# \n");

    /* REQ-1/AC-1 + REQ-2: a mesma regra sobre a superficie conectada, cuja cauda
     * e a propria linha remota sem prompt local.  O scrollback observado e o que
     * o host remoto enviou, intacto. */
    const line_view connected_view = connected_view_for_available(64);
    assert(connected_view.marker.empty());
    assert(rendered(connected_view, "") == "");
    assert(rendered(connected_view, "root@host:~# ") == "root@host:~# \n");
    assert(rendered(connected_view, "root@host:~# \n") == "root@host:~# \n");
    assert(rendered(connected_view, filter_all("root@host:~$ ls\r\n")) ==
           "root@host:~$ ls\n");
    assert(rendered(connected_view, filter_all("root@host:~$ ")) ==
           "root@host:~$ \n");

    /* REQ-2/AC-2 na superficie conectada: o LF visual continua unico,
     * condicional ao fim da saida e dentro do orcamento reservado. */
    check_connected_case(TERMINAL_LIMIT, "");
    check_connected_case(TERMINAL_LIMIT, "out");
    check_connected_case(TERMINAL_LIMIT, "out\n");
    check_connected_case(TERMINAL_LIMIT, "user@host:~$ ");
    check_connected_case(16, "0123456789abcde");
    check_connected_case(16, "0123456789abcdef");
    check_connected_case(1, "x");
    check_connected_case(1, "xy");
    check_connected_case(0, "out");
    check_connected_case(0, "out\n");
    check_connected_case(0, "user@host:~$ ");
    for (size_t available = 0; available <= 64; ++available) {
        for (size_t length = 0; length <= 80; ++length) {
            const std::string scrollback(length, 'y');
            check_connected_case(available, scrollback);
            check_connected_case(available, scrollback + "\n");
        }
    }

    /* CR e CRLF remotos sao normalizados em um unico LF pelo filtro, entao a
     * regra visual nao acrescenta nada: e a saida normalizada que decide. */
    assert(filter_all("root@host:~$ ls\r\n") == "root@host:~$ ls\n");
    assert(rendered(view, filter_all("root@host:~$ ls\r\n")) == "root@host:~$ ls\n");
    assert(filter_all("root@host:~$ ls\r") == "root@host:~$ ls\n");
    assert(rendered(view, filter_all("root@host:~$ ls\r")) == "root@host:~$ ls\n");
    assert(rendered(view, filter_all("root@host:~$ ")) == "root@host:~$ \n");

    /* ANSI removido e fragmentacao irrelevante: o texto remoto preservado
     * continua sendo a unica entrada da regra visual. */
    assert(filter_all("\x1B[32mroot@host\x1B[0m:~$ \x1B[1mls -la\x1B[0m\r\n") ==
           "root@host:~$ ls -la\n");
    for (size_t step = 1; step <= 8; ++step) {
        assert(filter_partitioned("\x1B[32mroot@host\x1B[0m:~$ ls\r\n", step) ==
               "root@host:~$ ls\n");
    }

    /* REQ-3: o LF visual e apenas view.  Ele nao volta para o scrollback nem
     * para o payload: reaplicar a regra ao proprio resultado nao cresce. */
    std::string scrollback = "out";
    for (int round = 0; round < 8; ++round) {
        const std::string next = rendered(view, scrollback);
        assert(next.size() <= 64);
        if (next == scrollback) break;
        scrollback = next;
    }
    assert(rendered(view, scrollback) == scrollback);
    assert(scrollback.back() == '\n');
    return 0;
}
'''


def test_visual_separator_executes_production_statements() -> None:
    sources = PRODUCTION_SOURCES + [str(FILTER_SRC)]
    with tempfile.TemporaryDirectory(prefix="cyberdeck-rendered-") as directory:
        compile_and_run(directory, "rendered",
                        rendered_output_harness(extracted_rendered_tail()), sources)


def test_invalid_cd_invokes_real_local_shell() -> None:
    harness = r'''
#include "apps/shell/cyberdeck_local_shell.h"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(int argc, char **argv) {
    (void)argc;
    std::filesystem::path root = argv[1];
    std::filesystem::create_directories(root / "deep");
    cyberdeck_local_shell shell(root.string());
    assert(shell.execute("cd deep").status == cyberdeck_local_shell_status::handled);
    for (const char *command : {"cd missing", "cd /tmp/outside", "cd deep/absent"}) {
        assert(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
        assert(shell.cwd() == "/deep");
        assert(shell.execute("pwd").output == "/deep\n");
    }
}
'''
    with tempfile.TemporaryDirectory(prefix="cyberdeck-cd-") as directory:
        root = Path(directory) / "root"
        cpp = Path(directory) / "cd.cpp"
        binary = Path(directory) / "cd"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "components/cyberdeck/include"), str(cpp),
            str(LOCAL_SHELL), str(VFS_NAMESPACE), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), str(root)], check=True)


if __name__ == "__main__":
    test_utf8_limits_and_cursor()
    test_prompt_matrix_executes_production_composition()
    test_visual_separator_executes_production_statements()
    test_invalid_cd_invokes_real_local_shell()
    print("PASS: prompt UTF-8 limits, cursor, remote prompt preservation, "
          "visual LF rule and cd preservation")
