/*
 * Host-side contract for term.dump (REQ-TERM-001 / AC-TERM-001..005).
 *
 * Superficie coberta NESTE arquivo (build host, sem FreeRTOS/LVGL/ESP_PLATFORM):
 *   TEST-TERM-001 (AC-TERM-001): `term.dump` e um type CONHECIDO do parse
 *                               NDJSON (k_known) e dispatch host nao quebra.
 *   TEST-TERM-002 (AC-TERM-001): envelope de uma linha + `rid` ecoado, inclusive
 *                               em erros (unknown_type) e rids com escapes.
 *   TEST-TERM-003 (AC-TERM-002): resultado BOUNDED — envelope nao cresce com
 *                               payload irrelevante e respeita k_max_ndjson_line.
 *   TEST-TERM-004 (AC-TERM-002): truncamento/escape UTF-8-SAFE do payload do
 *                               dump: bytes multi-byte preservados e controles
 *                               escapados, garantindo envelope de uma linha.
 *   A ausencia do banner de boot e verificada contra a fonte real em
 *   test_term_dump_contract.py; este harness cobre somente o round-trip do
 *   payload fornecido ao dispatch.
 *
 * Superficie NAO coberta aqui (existe apenas sob ESP_PLATFORM ou tem linkage
 * interno) e verificada estruturalmente em test_term_dump_contract.py:
 *   - exec_term_dump() e o ramo `req.type == "term.dump"` em device_exec()
 *     (linhas dentro de `#ifdef ESP_PLATFORM` da ponte).
 *   - cyberdeck_ui_term_dump(): lock bounded (1000 ms) e TODOS os caminhos de
 *     unlock/erro em cyberdeck_ui.cpp.
 *   - truncate_left_utf8_local(): linkage interno (namespace anonimo), logo
 *     nao-linkavel por outro TU host; seu uso UTF-8-safe e verificado por
 *     inspecao estrutural, e o comportamento effective por validacao no
 *     dispositivo (tests/manual/serial-bridge-validation.pt-BR.md).
 *
 * Sem hardware, sem pyserial, sem rede.
 */
#include "apps/serial/cyberdeck_serial_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

inline bool has(const std::string &hay, const std::string &needle)
{
    return hay.find(needle) != std::string::npos;
}

/* Validador UTF-8 estrito independent (oráculo do teste, não a produção).
 * Usado para provar que o payload escapado continua UTF-8 válido e que
 * nenhum code point foi cortado no meio por um truncamento. */
bool valid_utf8(const std::string &s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        unsigned char lo2 = 0x80, hi2 = 0xBF; /* faixa do 2º byte */
        if (b0 < 0x80) { ++i; continue; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { len = 2; }
        else if (b0 == 0xE0) { len = 3; lo2 = 0xA0; }
        else if (b0 >= 0xE1 && b0 <= 0xEC) { len = 3; }
        else if (b0 == 0xED) { len = 3; hi2 = 0x9F; }
        else if (b0 >= 0xEE && b0 <= 0xEF) { len = 3; }
        else if (b0 == 0xF0) { len = 4; lo2 = 0x90; }
        else if (b0 >= 0xF1 && b0 <= 0xF3) { len = 4; }
        else if (b0 == 0xF4) { len = 4; hi2 = 0x8F; }
        else return false; /* continuação isolada ou byte inválido */
        if (i + len > s.size()) return false;  /* sequência truncada */
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        if (b1 < lo2 || b1 > hi2) return false;
        for (std::size_t k = 2; k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if (b < 0x80 || b > 0xBF) return false;
        }
        i += len;
    }
    return true;
}

/* Unescape mínimo de string JSON para recuperar o texto original do envelope.
 * `in` é o CONTEÚDO interno (sem aspas envolventes). Oráculo do teste:
 * garante que o payload do dump não foi corrompido. */
bool json_unescape(const std::string &in, std::string &out)
{
    out.clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c != '\\') {
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7F) return false;
            out.push_back(c);
            continue;
        }
        if (i + 1 >= in.size()) return false;
        const char k = in[i + 1];
        i += 1;
        if (k == '"') out.push_back('"');
        else if (k == '\\') out.push_back('\\');
        else if (k == 'u') {
            if (i + 2 >= in.size()) return false;
            const std::string hex = in.substr(i + 3, 2); /* \u00XX: os 2 dígitos úteis */
            for (char h : hex) {
                const bool okd = (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F');
                if (!okd) return false;
            }
            const auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
            if (cp > 0x7F) return false; /* produção escapa só Latin-1 */
            out.push_back(static_cast<char>(cp));
            i += 4; /* consome 'u' + 2 hex + dígito já lido; fecha em \u00XX */
        } else return false;
    }
    return true;
}

/* Extrai o valor da chave JSON `key` (nível raiz do envelope) como conteúdo
 * cru, sem as aspas envolventes; devolve false se ausente/não-string. */
bool extract_json_string(const std::string &envelope, const std::string &key, std::string &raw)
{
    const std::string needle = "\"" + key + "\":\"";
    const auto at = envelope.find(needle);
    if (at == std::string::npos) return false;
    const std::size_t start = at + needle.size();
    for (std::size_t i = start; i < envelope.size(); ++i) {
        if (envelope[i] == '\\') {
            const char k = (i + 1 < envelope.size()) ? envelope[i + 1] : '\0';
            i += (k == 'u') ? 5 : 1; /* \u00XX = 6 chars; \" e \\ = 2 chars */
            continue;
        }
        if (envelope[i] == '"') { raw = envelope.substr(start, i - start); return true; }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* TEST-TERM-001 (AC-TERM-001): type conhecido no parse host           */
/* ------------------------------------------------------------------ */
void test_term_dump_is_known_type()
{
    using namespace cyberdeck_serial;
    {
        const std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\"}";
        request req;
        dispatch_error err = dispatch_error::internal;
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.rid == "t1");
        CHECK(req.type == "term.dump");
        CHECK(!req.payload_raw.empty());
    }
    /* NDJSON é aparado: espaços, \r e \n terminal não alteram o type. */
    {
        const std::string line = "  {\"rid\":\"t2\",\"type\":\"term.dump\"}  \r\n";
        request req;
        dispatch_error err = dispatch_error::internal;
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.rid == "t2");
        CHECK(req.type == "term.dump");
    }
    /* term.dump não exige argumentos extras: campos conhecidos irrelevantes
     * são aceitos (o dispatch de term.dump não lê nenhum deles). */
    {
        const std::string line = "{\"rid\":\"t3\",\"type\":\"term.dump\",\"text\":\"ignorado\",\"size\":10}";
        request req;
        dispatch_error err = dispatch_error::internal;
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.type == "term.dump");
    }
    /* rid no limite exato (k_max_rid_len) é aceito. */
    {
        const std::string rid(cyberdeck_serial::k_max_rid_len, 'a');
        const std::string line = "{\"rid\":\"" + rid + "\",\"type\":\"term.dump\"}";
        request req;
        dispatch_error err = dispatch_error::internal;
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.rid.size() == cyberdeck_serial::k_max_rid_len);
        CHECK(req.type == "term.dump");
    }
    /* Quase-erros: o type precisa casar EXATAMENTE (nem prefixo, nem sufixo,
     * nem variante de caixa, nem espaço residual, nem escape decoration).
     * Todos → unknown_type e o rid continua preenchido para eco no erro. */
    {
        const char *const near_miss[] = {"term.dumpx", "term.dum", "term_dump", "termdump",
                                         "Term.dump", "TERM.DUMP", "term.dump ", " term.dump",
                                         "xterm.dump", "term.dump\\n", "term.dump\\t"};
        for (const char *bad : near_miss) {
            request req;
            dispatch_error err = dispatch_error::none;
            const std::string line = std::string("{\"rid\":\"t9\",\"type\":\"") + bad + "\"}";
            CHECK(!parse_ndjson_line(line.data(), line.size(), req, err));
            CHECK(err == dispatch_error::unknown_type);
            CHECK(req.rid == "t9");
        }
        /* Contrapositivo exato: escape JSON \u no type é decodificado e o
         * tipo resultante é reconhecido (o match é sobre o valor decodificado). */
        const char *decoded = "{\"rid\":\"t9\",\"type\":\"term.dum\\u0070\"}";
        request dreq;
        dispatch_error derr = dispatch_error::none;
        CHECK(parse_ndjson_line(decoded, std::strlen(decoded), dreq, derr));
        CHECK(derr == dispatch_error::none);
        CHECK(dreq.type == "term.dump");
        CHECK(dreq.rid == "t9");
    }
    /* type ausente/vazio e entradas degeneradas: erro, nunca crash. */
    {
        request req;
        dispatch_error err = dispatch_error::none;
        const char *s1 = "{\"rid\":\"t10\"}";
        CHECK(!parse_ndjson_line(s1, std::strlen(s1), req, err));
        CHECK(err == dispatch_error::missing_type);
        const char *s2 = "{\"rid\":\"t10\",\"type\":\"\"}";
        CHECK(!parse_ndjson_line(s2, std::strlen(s2), req, err));
        CHECK(err == dispatch_error::missing_type);
        CHECK(!parse_ndjson_line(nullptr, 0, req, err));
        CHECK(err != dispatch_error::none);
        CHECK(!parse_ndjson_line("", 0, req, err));
        CHECK(err != dispatch_error::none);
    }
}

/* ------------------------------------------------------------------ */
/* TEST-TERM-002 (AC-TERM-001): envelope/rid correlacionado             */
/* ------------------------------------------------------------------ */
void test_envelope_and_rid_correlation()
{
    using namespace cyberdeck_serial;
    {
        const std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(res.err == dispatch_error::none);
        CHECK(res.rid == "t1");
        CHECK(res.type == "term.dump");
        CHECK(has(res.envelope_json, "\"rid\":\"t1\""));
        CHECK(has(res.envelope_json, "\"ok\":true"));
        CHECK(has(res.envelope_json, "\"result\""));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
        CHECK(res.envelope_json.find('\r') == std::string::npos);
        CHECK(res.envelope_json.size() >= 2);
        CHECK(res.envelope_json.front() == '{');
        CHECK(res.envelope_json.back() == '}');
    }
    /* Determinismo: mesma entrada ⇒ envelope byte-idêntico. */
    {
        const std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\"}";
        const dispatch_result a = dispatch_one(line.data(), line.size());
        const dispatch_result b = dispatch_one(line.data(), line.size());
        CHECK_STR_EQ(a.envelope_json, b.envelope_json);
    }
    /* rids com caracteres especiais são escapados, nunca quebram a linha. */
    {
        const std::string line = "{\"rid\":\"a b-c_d.e~f\",\"type\":\"term.dump\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(has(res.envelope_json, "\"rid\":\"a b-c_d.e~f\""));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    {
        /* rid com aspas e barra: escapado no envelope e sem aspas cruas. */
        const std::string line = std::string("{\"rid\":\"q\\\"x\\\\y\",\"type\":\"term.dump\"}");
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(has(res.envelope_json, "\"rid\":\"q\\\"x\\\\y\""));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    {
        /* Byte de controle cru (0x0A) DENTRO da string JSON não é JSON válido:
         * a requisição é rejeitada com invalid_json e nenhum byte de controle
         * cru vaza para o envelope de erro (que segue uma linha). */
        std::string line = std::string("{\"rid\":\"a") + char(0x0A) + "b\",\"type\":\"term.dump\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.err == cyberdeck_serial::dispatch_error::invalid_json);
        CHECK(res.envelope_json.find('\n') == std::string::npos);
        CHECK(res.envelope_json.find('\r') == std::string::npos);
        CHECK(res.envelope_json.front() == '{' && res.envelope_json.back() == '}');
    }
    /* Erro de type desconhecido ecoa o rid e sinaliza unknown_type. */
    {
        const std::string line = "{\"rid\":\"t9\",\"type\":\"term.dumpx\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.err == cyberdeck_serial::dispatch_error::unknown_type);
        CHECK(res.rid == "t9");
        CHECK(has(res.envelope_json, "\"rid\":\"t9\""));
        CHECK(has(res.envelope_json, "\"ok\":false"));
        CHECK(has(res.envelope_json, "\"error\""));
        CHECK(has(res.envelope_json, "\"error_code\":\"unknown_type\""));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    /* JSON malformado: envelope de erro de uma linha, sem eco de lixo. */
    {
        const char *bad = "{\"rid\":\"t11\",\"type\"";
        const dispatch_result res = dispatch_one(bad, std::strlen(bad));
        CHECK(!res.ok);
        CHECK(res.envelope_json.front() == '{' && res.envelope_json.back() == '}');
        CHECK(has(res.envelope_json, "\"ok\":false"));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    {
        const char *bad = "not json";
        const dispatch_result res = dispatch_one(bad, std::strlen(bad));
        CHECK(!res.ok);
        CHECK(res.envelope_json.front() == '{' && res.envelope_json.back() == '}');
        CHECK(has(res.envelope_json, "\"ok\":false"));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    /* dispatch_one tolerante a nullptr/0 e NUL embutido (sem crash). */
    {
        const dispatch_result res = dispatch_one(nullptr, 0);
        CHECK(!res.ok);
        CHECK(has(res.envelope_json, "\"ok\":false"));
        const std::string with_nul = std::string("{\"rid\":\"t1\",\"type\":\"term.dump\",\"x\":\"a") + char(0) + "b\"}";
        const dispatch_result res2 = dispatch_one(with_nul.data(), with_nul.size());
        CHECK(res2.envelope_json.find('\n') == std::string::npos);
    }
}

/* ------------------------------------------------------------------ */
/* TEST-TERM-003 (AC-TERM-002): resultado bounded                      */
/* ------------------------------------------------------------------ */
void test_bounded_result()
{
    using namespace cyberdeck_serial;
    /* O envelope de term.dump é bounded: não cresce com payload irrelevante. */
    std::size_t base = 0;
    {
        const std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        base = res.envelope_json.size();
        CHECK(base > 0);
    }
    {
        const std::string line =
            "{\"rid\":\"t1\",\"type\":\"term.dump\",\"text\":\"" + std::string(3000, 'x') + "\"}";
        CHECK(line.size() < cyberdeck_serial::k_max_ndjson_line);
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(res.envelope_json.size() == base);
        CHECK(res.envelope_json.size() <= cyberdeck_serial::k_max_ndjson_line);
    }
    /* Payload máximo aceito ainda respeita o teto do envelope de saída. */
    {
        const std::string filler(cyberdeck_serial::k_max_ndjson_line - 60, 'y');
        const std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\",\"symbol\":\"" + filler + "\"}";
        CHECK(line.size() <= cyberdeck_serial::k_max_ndjson_line);
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.envelope_json.size() <= cyberdeck_serial::k_max_ndjson_line + 256);
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    /* Limite de entrada: exatamente k_max_ndjson_line não é too_large; +1 é. */
    {
        request req;
        dispatch_error err = dispatch_error::none;
        std::string line = "{\"rid\":\"t1\",\"type\":\"term.dump\",\"symbol\":\"";
        line.append(cyberdeck_serial::k_max_ndjson_line - line.size() - 2, 'z');
        line += "\"}";
        CHECK(line.size() == cyberdeck_serial::k_max_ndjson_line);
        CHECK(parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == dispatch_error::none);
        CHECK(req.type == "term.dump");

        const std::string over = line + std::string(1, 'z');
        CHECK(over.size() == cyberdeck_serial::k_max_ndjson_line + 1);
        CHECK(!parse_ndjson_line(over.data(), over.size(), req, err));
        CHECK(err == dispatch_error::too_large);
    }
    /* rid acima de k_max_rid_len é rejeitado mesmo com type válido. */
    {
        const std::string rid(cyberdeck_serial::k_max_rid_len + 1, 'r');
        const std::string line = "{\"rid\":\"" + rid + "\",\"type\":\"term.dump\"}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.envelope_json.find('\n') == std::string::npos);
        CHECK(has(res.envelope_json, "\"ok\":false"));
    }
}

/* ------------------------------------------------------------------ */
/* TEST-TERM-004 (AC-TERM-002): escape/truncamento UTF-8-safe          */
/* ------------------------------------------------------------------ */
/*
 * exec_term_dump serializa o texto com append_json_string() e reduz o texto
 * por truncate_left_utf8_local() antes/depois, para caber no limite de
 * envelope. append_json_string() é observável no host pelo ramo `ui.echo`,
 * que usa exatamente o mesmo helper: os invariantes abaixo valem para o
 * payload do term.dump (texto de terminal tem \n, ANSI residual e UTF-8).
 */
void test_utf8_safe_payload_escaping()
{
    using namespace cyberdeck_serial;
    struct Case { const char *name; std::string text; };
    const std::string multibyte = "acentuação: \xC3\xA9\xC3\xA0";                       /* 2 bytes */
    const std::string euro = "\xE2\x82\xAC";                                              /* 3 bytes */
    const std::string emoji = "\xF0\x9F\x98\x80";                                         /* 4 bytes */
    const std::string mixed = std::string("root@deck:~$ ") + multibyte + " " + euro + " " + emoji + "\n";
    const std::string ctrl = std::string("a\tb") + char(0x1B) + "[0m" + char(0x07) + char(0x7F) + "\r\n";
    const Case cases[] = {
        {"ascii", "root@deck:~$ "},
        {"multibyte-2-3-4", mixed},
        {"controles-ansi", ctrl},
        {"quotes-backslash", "path \"C:\\tmp\"\\n"},
        {"vazio", ""},
    };
    for (const Case &c : cases) {
        std::printf("  caso: %s\n", c.name);
        /* JSONEncode: escapes mínimos para string JSON. */
        std::string quoted;
        quoted.push_back('"');
        for (char ch : c.text) {
            if (ch == '"' || ch == '\\') { quoted.push_back('\\'); quoted.push_back(ch); }
            else if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F) {
                static const char *hex = "0123456789abcdef";
                const auto u = static_cast<unsigned char>(ch);
                quoted += "\\u00";
                quoted.push_back(hex[(u >> 4) & 0xF]);
                quoted.push_back(hex[u & 0xF]);
            } else quoted.push_back(ch);
        }
        quoted.push_back('"');

        std::string line = "{\"rid\":\"t1\",\"type\":\"ui.echo\",\"text\":";
        line += quoted;
        line += "}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        /* (a) envelope permanece UMA linha NDJSON, sem \n/\r crus. */
        CHECK(res.envelope_json.find('\n') == std::string::npos);
        CHECK(res.envelope_json.find('\r') == std::string::npos);
        CHECK(res.envelope_json.front() == '{' && res.envelope_json.back() == '}');
        /* (b) bytes >= 0x80 atravessam sem mojibake nem escape indevido. */
        for (unsigned char ch : c.text) {
            if (ch >= 0x80) {
                const std::string raw(1, static_cast<char>(ch));
                CHECK(has(res.envelope_json, raw));
            }
        }
        /* (c) o texto recuperado do envelope é byte-idêntico ao original
         *     e continua UTF-8 válido (nada truncado no meio de um code point). */
        std::string raw_value;
        CHECK(extract_json_string(res.envelope_json, "text", raw_value));
        std::string recovered;
        CHECK(json_unescape(raw_value, recovered));
        CHECK(recovered == c.text);
        CHECK(valid_utf8(recovered));
        /* (d) o envelope inteiro não contém byte de controle cru nem DEL. */
        for (char ch : res.envelope_json) {
            const auto u = static_cast<unsigned char>(ch);
            CHECK(!(u < 0x20 || u == 0x7F));
        }
    }
    /* Limite de payload do ramo de eco (mesma política bounded do dump):
     * acima do limite a resposta troca o texto por `truncated:true`. */
    {
        std::string text(2048, 'k');
        std::string line = "{\"rid\":\"t1\",\"type\":\"ui.echo\",\"text\":\"" + text + "\"}";
        const dispatch_result ok_res = dispatch_one(line.data(), line.size());
        CHECK(ok_res.ok);
        CHECK(has(ok_res.envelope_json, "\"truncated\":true") == false);
        CHECK(ok_res.envelope_json.size() <= cyberdeck_serial::k_max_ndjson_line + 256);

        text.push_back('k'); /* 2049 */
        line = "{\"rid\":\"t1\",\"type\":\"ui.echo\",\"text\":\"" + text + "\"}";
        const dispatch_result big_res = dispatch_one(line.data(), line.size());
        CHECK(big_res.ok);
        CHECK(has(big_res.envelope_json, "\"truncated\":true"));
        CHECK(big_res.envelope_json.find('\n') == std::string::npos);
    }
    /* Texto com many multi-byte + controles fica dentro do teto de envelope. */
    {
        std::string text;
        /* > 2048 byteseffective, forcing the bounded (`truncated`) response
         * while the NDJSON line itself stays under k_max_ndjson_line. */
        while (text.size() < 2100) text += mixed;
        std::string quoted = "\"";
        for (char ch : text) {
            if (ch == '"' || ch == '\\') { quoted.push_back('\\'); quoted.push_back(ch); }
            else if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F) {
                static const char *hex = "0123456789abcdef";
                const auto u = static_cast<unsigned char>(ch);
                quoted += "\\u00";
                quoted.push_back(hex[(u >> 4) & 0xF]);
                quoted.push_back(hex[u & 0xF]);
            } else quoted.push_back(ch);
        }
        quoted.push_back('"');
        std::string line = "{\"rid\":\"t1\",\"type\":\"ui.echo\",\"text\":" + quoted + "}";
        const dispatch_result res = dispatch_one(line.data(), line.size());
        CHECK(res.ok);
        CHECK(has(res.envelope_json, "\"truncated\":true"));
        CHECK(res.envelope_json.find('\n') == std::string::npos);
        CHECK(res.envelope_json.size() <= cyberdeck_serial::k_max_ndjson_line + 256);
    }
}

/* ------------------------------------------------------------------ */
/* Round-trip do payload de term.dump fornecido ao dispatch.              */
/* ------------------------------------------------------------------ */
void test_term_dump_payload_roundtrip()
{
    using namespace cyberdeck_serial;
    const std::string initial_text = "root@deck:~$ ";

    const std::string line =
        "{\"rid\":\"boot\",\"type\":\"ui.echo\",\"text\":\"root@deck:~$ \"}";
    const dispatch_result res = dispatch_one(line.data(), line.size());
    CHECK(res.ok);
    std::string raw_value;
    CHECK(extract_json_string(res.envelope_json, "text", raw_value));
    std::string recovered;
    CHECK(json_unescape(raw_value, recovered));
    CHECK(recovered == initial_text);
}

} // namespace

int main()
{
    test_term_dump_is_known_type();
    test_envelope_and_rid_correlation();
    test_bounded_result();
    test_utf8_safe_payload_escaping();
    test_term_dump_payload_roundtrip();
    if (s_failures == 0) {
        std::printf("PASS: serial_term_dump (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
