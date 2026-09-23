#include "features/serial/cyberdeck_serial_bridge.h"

#include "features/screenshot/screenshot_bmp.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(ESP_PLATFORM) && !defined(O_NOFOLLOW)
#error "fs.write no ESP exige O_NOFOLLOW do VFS"
#endif

#ifdef ESP_PLATFORM
#include "bsp/esp-bsp.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "features/wifi/wifi_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "platform/display/cyberdeck_ui.h"
#include <cstdarg>
#endif

/*
 * Ponte manual USB Serial-JTAG NDJSON (REQ-002/003/005/006/007/008/009).
 *
 * Parte PURA (host-testavel): parser JSON proprio, parse/envelopes/dispatch,
 * sessao de screen.dump, CRC32 IEEE e LineAssembler. Autocontida: espelha o
 * stride/size/header/conversao do BMP localmente (formulas identicas as de
 * screenshot_bmp.cpp) e NAO reference funcoes de screenshot_bmp_* fora de
 * #ifdef ESP_PLATFORM — tres dos quatro testes host linkam somente este
 * arquivo e quebrariam com simbolos nao resolvidos.
 *
 * Parte DEVICE (somente sob #ifdef ESP_PLATFORM): task FreeRTOS, driver
 * USB Serial-JTAG, writer de log via esp_log_set_vprintf com mutex
 * compartilhado, hooks de UI (click/tap/type/clear/dump), stream de
 * screen.dump e wifi.scan sincrono com o wifi_mgr.
 */

namespace cyberdeck_serial {
namespace {

/* ================================================================== */
/* JSON puro (sem cJSON): recursive descent sobre buffer limitado.     */
/* ================================================================== */

inline bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

inline bool is_ctrl(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* UTF-8 estrito: devolve bytes consumidos ou 0 se invalido. */
std::size_t utf8_len(unsigned char c)
{
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

bool valid_utf8_at(const char *data, std::size_t len, std::size_t pos, std::size_t &out_consumed)
{
    if (pos >= len) {
        return false;
    }
    const auto c = static_cast<unsigned char>(data[pos]);
    const std::size_t n = utf8_len(c);
    if (n == 0 || pos + n > len) {
        return false;
    }
    if (n == 1) {
        out_consumed = 1;
        return true;
    }
    for (std::size_t i = 1; i < n; ++i) {
        if ((static_cast<unsigned char>(data[pos + i]) & 0xC0) != 0x80) {
            return false;
        }
    }
    std::uint32_t cp = 0;
    switch (n) {
    case 2:
        cp = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos]) & 0x1F) << 6;
        break;
    case 3:
        cp = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos]) & 0x0F) << 12;
        break;
    case 4:
        cp = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos]) & 0x07) << 18;
        break;
    default:
        return false;
    }
    for (std::size_t i = 1; i < n; ++i) {
        cp |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + i]) & 0x3F) << (6 * (n - 1 - i));
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)) {
        return false; /* overlong */
    }
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return false; /* fora de faixa / surrogates */
    }
    out_consumed = n;
    return true;
}

bool valid_utf8(const char *data, std::size_t len)
{
    std::size_t pos = 0;
    while (pos < len) {
        std::size_t consumed = 0;
        if (!valid_utf8_at(data, len, pos, consumed)) {
            return false;
        }
        pos += consumed;
    }
    return true;
}

/* Campo capturado do objeto JSON de topo. */
struct json_field {
    enum class kind { absent, string, number, boolean, object_or_array };
    kind k = kind::absent;
    std::string s;   /* valor de string decodificado (sem aspas) */
    std::string raw; /* literal cru do valor (strings mantem as aspas) */
};

bool parse_string_raw(const char *data, std::size_t len, std::size_t &pos, std::string &out)
{
    if (pos >= len || data[pos] != '"') {
        return false;
    }
    ++pos;
    out.clear();
    while (pos < len) {
        const char c = data[pos];
        if (c == '"') {
            ++pos;
            return true;
        }
        if (is_ctrl(c)) {
            return false; /* controle cru (inclui NUL) em string */
        }
        if (c == '\\') {
            if (pos + 1 >= len) {
                return false;
            }
            const char e = data[pos + 1];
            pos += 2;
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (pos + 4 > len) {
                    return false;
                }
                std::uint32_t cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = data[pos + i];
                    std::uint32_t d = 0;
                    if (h >= '0' && h <= '9') {
                        d = static_cast<std::uint32_t>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        d = static_cast<std::uint32_t>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        d = static_cast<std::uint32_t>(h - 'A' + 10);
                    } else {
                        return false;
                    }
                    cp = (cp << 4) | d;
                }
                pos += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    /* par sintetico: exige \uDC00-\uDFFF em seguida */
                    if (pos + 6 > len || data[pos] != '\\' || data[pos + 1] != 'u') {
                        return false;
                    }
                    std::uint32_t lo = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = data[pos + 2 + i];
                        std::uint32_t d = 0;
                        if (h >= '0' && h <= '9') {
                            d = static_cast<std::uint32_t>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            d = static_cast<std::uint32_t>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            d = static_cast<std::uint32_t>(h - 'A' + 10);
                        } else {
                            return false;
                        }
                        lo = (lo << 4) | d;
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        return false;
                    }
                    pos += 6;
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default:
                return false;
            }
            continue;
        }
        std::size_t consumed = 0;
        if (!valid_utf8_at(data, len, pos, consumed)) {
            return false;
        }
        out.append(data + pos, consumed);
        pos += consumed;
    }
    return false; /* string sem fechamento */
}

bool parse_number(const char *data, std::size_t len, std::size_t &pos, std::string &out)
{
    const std::size_t start = pos;
    if (pos < len && data[pos] == '-') {
        ++pos;
    }
    bool digits = false;
    while (pos < len && is_digit(data[pos])) {
        ++pos;
        digits = true;
    }
    if (pos < len && data[pos] == '.') {
        ++pos;
        while (pos < len && is_digit(data[pos])) {
            ++pos;
            digits = true;
        }
    }
    if (!digits) {
        pos = start;
        return false;
    }
    if (pos < len && (data[pos] == 'e' || data[pos] == 'E')) {
        ++pos;
        if (pos < len && (data[pos] == '-' || data[pos] == '+')) {
            ++pos;
        }
        bool exp_digits = false;
        while (pos < len && is_digit(data[pos])) {
            ++pos;
            exp_digits = true;
        }
        if (!exp_digits) {
            pos = start;
            return false;
        }
    }
    out.assign(data + start, pos - start);
    return true;
}

/* Um valor JSON; objetos/arrays sao capturados como literal com
 * reconhecimento de strings/escapes (sem recursao — entrada <= 4096).
 * Classifica E extrai na mesma passada, sem re-parse:
 *   - string    -> kind::string, `s` decodido (escapes/NUL via
 *                  parse_string_raw) e `raw` = literal COM aspas;
 *   - objeto/arr -> kind::object_or_array, `raw` = literal integro;
 *   - true/false -> kind::boolean, `raw` = literal;
 *   - numero    -> kind::number, `raw` = literal do numero;
 *   - null      -> kind::number com literal "null" (contrato existente:
 *                  is_number_literal rejeita, logo null nunca e lido como
 *                  numero).
 * `out` e preenchido integralmente em cada sucesso. */
bool parse_value(const char *data, std::size_t len, std::size_t &pos, json_field &out)
{
    while (pos < len && is_ws(data[pos])) {
        ++pos;
    }
    if (pos >= len) {
        return false;
    }
    const std::size_t start = pos;
    const char c = data[pos];
    if (c == '"') {
        std::string decoded;
        if (!parse_string_raw(data, len, pos, decoded)) {
            return false;
        }
        out.k = json_field::kind::string;
        out.raw.assign(data + start, pos - start); /* literal com aspas */
        out.s = std::move(decoded);
        return true;
    }
    if (c == '{' || c == '[') {
        const char open = c;
        const char close = (c == '{') ? '}' : ']';
        ++pos;
        int depth_inner = 0;
        bool in_str = false;
        bool esc = false;
        while (pos < len) {
            const char ch = data[pos];
            if (in_str) {
                if (esc) {
                    esc = false;
                } else if (ch == '\\') {
                    esc = true;
                } else if (ch == '"') {
                    in_str = false;
                }
                ++pos;
                continue;
            }
            if (ch == '"') {
                in_str = true;
            } else if (ch == open) {
                ++depth_inner;
            } else if (ch == close) {
                if (depth_inner == 0) {
                    ++pos;
                    out.k = json_field::kind::object_or_array;
                    out.raw.assign(data + start, pos - start);
                    return true;
                }
                --depth_inner;
            }
            ++pos;
        }
        return false;
    }
    if (c == 't') {
        if (pos + 4 <= len && std::memcmp(data + pos, "true", 4) == 0) {
            pos += 4;
            out.k = json_field::kind::boolean;
            out.raw.assign("true");
            return true;
        }
        return false;
    }
    if (c == 'f') {
        if (pos + 5 <= len && std::memcmp(data + pos, "false", 5) == 0) {
            pos += 5;
            out.k = json_field::kind::boolean;
            out.raw.assign("false");
            return true;
        }
        return false;
    }
    if (c == 'n') {
        if (pos + 4 <= len && std::memcmp(data + pos, "null", 4) == 0) {
            pos += 4;
            out.k = json_field::kind::number; /* "null" nunca passa em is_number_literal */
            out.raw.assign("null");
            return true;
        }
        return false;
    }
    if (!parse_number(data, len, pos, out.raw)) {
        return false;
    }
    out.k = json_field::kind::number;
    return true;
}

/* Parseia o objeto de topo e extrai apenas os campos `wanted`
 * (preenchidos na mesma ordem). Exige consumo completo (trailing so ws). */
bool parse_document(const char *data, std::size_t len, const std::vector<std::string> &wanted,
                    std::vector<json_field> &out)
{
    if (wanted.size() != out.size()) {
        return false;
    }
    std::size_t pos = 0;
    while (pos < len && is_ws(data[pos])) {
        ++pos;
    }
    if (pos >= len || data[pos] != '{') {
        return false;
    }
    ++pos;
    while (pos < len && is_ws(data[pos])) {
        ++pos;
    }
    if (pos < len && data[pos] == '}') {
        ++pos;
    } else {
        for (;;) {
            while (pos < len && is_ws(data[pos])) {
                ++pos;
            }
            std::string key;
            if (!parse_string_raw(data, len, pos, key)) {
                return false;
            }
            while (pos < len && is_ws(data[pos])) {
                ++pos;
            }
            if (pos >= len || data[pos] != ':') {
                return false;
            }
            ++pos;
            /* parse_value ja classifica (kind) e extrai (s/raw) o valor;
             * nenhuma re-decodificacao aqui (evita divergir do literal). */
            json_field value;
            if (!parse_value(data, len, pos, value)) {
                return false;
            }
            for (std::size_t i = 0; i < wanted.size(); ++i) {
                if (wanted[i] == key) {
                    out[i] = value; /* copia: preenche todos os match (como antes) */
                }
            }
            while (pos < len && is_ws(data[pos])) {
                ++pos;
            }
            if (pos < len && data[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < len && data[pos] == '}') {
                ++pos;
                break;
            }
            return false;
        }
    }
    while (pos < len && is_ws(data[pos])) {
        ++pos;
    }
    return pos == len;
}

/* ================================================================== */
/* Envelopes e helpers JSON                                           */
/* ================================================================== */

void append_json_string(std::string &out, const std::string &s)
{
    static const char *hex = "0123456789abcdef";
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (is_ctrl(c)) {
            const auto u = static_cast<unsigned char>(c);
            out += "\\u00";
            out.push_back(hex[(u >> 4) & 0xF]);
            out.push_back(hex[u & 0xF]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

std::string error_name(dispatch_error err)
{
    switch (err) {
    case dispatch_error::none:
        return "none";
    case dispatch_error::too_large:
        return "too_large";
    case dispatch_error::invalid_json:
        return "invalid_json";
    case dispatch_error::missing_rid:
        return "missing_rid";
    case dispatch_error::missing_type:
        return "missing_type";
    case dispatch_error::unknown_type:
        return "unknown_type";
    case dispatch_error::invalid_utf8:
        return "invalid_utf8";
    case dispatch_error::internal:
        return "internal";
    case dispatch_error::invalid_path:
        return "invalid_path";
    case dispatch_error::invalid_payload:
        return "invalid_payload";
    case dispatch_error::io_error:
        return "io_error";
    }
    return "internal";
}

const std::vector<std::string> k_request_fields = {"rid",   "type", "text",     "x",      "y",
                                                    "target", "symbol", "path", "data_b64", "size"};

bool get_field(const std::vector<json_field> &fields, const std::string &name, json_field &out)
{
    for (std::size_t i = 0; i < k_request_fields.size() && i < fields.size(); ++i) {
        if (k_request_fields[i] == name) {
            out = fields[i];
            return out.k != json_field::kind::absent;
        }
    }
    return false;
}

bool is_number_literal(const json_field &f)
{
    return f.k == json_field::kind::number && !f.raw.empty() &&
           (is_digit(f.raw[0]) || (f.raw[0] == '-' && f.raw.size() > 1 && is_digit(f.raw[1])));
}

/* ================================================================== */
/* BMP local (espelha screenshot_bmp.cpp; nenhum reference de funcao externa)  */
/* ================================================================== */

std::size_t bmp_stride_local(std::size_t width) { return (width * 3u + 3u) & ~(std::size_t)3u; }

void rgb565_to_bgr888_local(std::uint16_t px, std::uint8_t out3[3])
{
    const auto r5 = static_cast<std::uint8_t>((px >> 11) & 0x1F);
    const auto g6 = static_cast<std::uint8_t>((px >> 5) & 0x3F);
    const auto b5 = static_cast<std::uint8_t>(px & 0x1F);
    out3[0] = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
    out3[1] = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
    out3[2] = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
}

/* Mesmo layout byte a byte de screenshot_bmp_fill_header (top-down storage,
 * biHeight positivo), preenchido localmente para nao depender do objeto. */
void fill_header_local(screenshot_bmp_header_t *hdr, int width, int height)
{
    std::memset(hdr, 0, sizeof(*hdr));
    const std::size_t total = screen_bmp_size(width, height);
    const std::size_t stride = bmp_stride_local(static_cast<std::size_t>(width));
    hdr->bfType = SCREENSHOT_BMP_MAGIC;
    hdr->bfSize = static_cast<std::uint32_t>(total);
    hdr->bfOffBits = SCREENSHOT_BMP_HEADER_SIZE;
    hdr->biSize = SCREENSHOT_BMP_INFOHEADER_SIZE;
    hdr->biWidth = static_cast<std::uint32_t>(width);
    hdr->biHeight = static_cast<std::uint32_t>(height);
    hdr->biPlanes = SCREENSHOT_BMP_PLANES;
    hdr->biBitCount = SCREENSHOT_BMP_BITS_PER_PIXEL;
    hdr->biCompression = SCREENSHOT_BMP_COMPRESSION;
    hdr->biSizeImage = static_cast<std::uint32_t>(stride * static_cast<std::size_t>(height));
}

/* Sessao de screen.dump: slot unico; falha de init nunca limpa a ativa. */
struct screen_session {
    bool active = false;
    std::string rid;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bmp;
    std::uint32_t chunks = 0;
};

screen_session g_screen;

/* ssid limitado a 32 bytes sem cortar UTF-8 no meio. */
std::string truncate_utf8_ssid(std::string s)
{
    if (s.size() <= 32) {
        return s;
    }
    std::size_t cut = 32;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    s.resize(cut);
    return s;
}

#ifdef ESP_PLATFORM
/* Declarado aqui para dispatch_one (definido na secao device abaixo);
 * blocos anonymous namespace no mesmo TU formam o mesmo namespace. */
struct device_result {
    bool handled = false;
    dispatch_result result;
};
device_result device_exec(const request &req, const std::vector<json_field> &fields);
#endif

} // namespace

/* ================================================================== */
/* Parse / envelopes (API publica)                                    */
/* ================================================================== */

bool parse_ndjson_line(const char *data, std::size_t len, request &out, dispatch_error &err)
{
    out = request{};
    if (data == nullptr || len == 0) {
        err = dispatch_error::invalid_json;
        return false;
    }
    if (len > k_max_ndjson_line) {
        err = dispatch_error::too_large;
        return false;
    }
    if (!valid_utf8(data, len)) {
        err = dispatch_error::invalid_utf8;
        return false;
    }
    std::size_t begin = 0;
    while (begin < len && is_ws(data[begin])) {
        ++begin;
    }
    std::size_t end = len;
    while (end > begin && is_ws(data[end - 1])) {
        --end;
    }
    if (begin >= end || data[begin] != '{') {
        err = dispatch_error::invalid_json;
        return false;
    }
    const char *p = data + begin;
    const std::size_t n = end - begin;

    std::vector<json_field> fields(k_request_fields.size());
    if (!parse_document(p, n, k_request_fields, fields)) {
        err = dispatch_error::invalid_json;
        return false;
    }

    json_field rid_f;
    if (!get_field(fields, "rid", rid_f) || rid_f.k != json_field::kind::string || rid_f.s.empty()) {
        err = dispatch_error::missing_rid;
        return false;
    }
    if (rid_f.s.size() > k_max_rid_len) {
        err = dispatch_error::too_large;
        return false;
    }
    out.rid = rid_f.s;
    out.payload_raw.assign(p, n);

    json_field type_f;
    if (!get_field(fields, "type", type_f) || type_f.k != json_field::kind::string || type_f.s.empty()) {
        err = dispatch_error::missing_type;
        return false;
    }
    out.type = type_f.s;

    static const char *const k_known[] = {"ping",       "ui.echo",     "ui.clear",  "ui.click",  "ui.tap",
                                          "ui.type",    "ui.dump",     "screen.shot", "screen.dump", "sys.info",
                                          "wifi.status", "wifi.scan",  "fs.write"};
    bool known = false;
    for (const char *k : k_known) {
        if (out.type == k) {
            known = true;
            break;
        }
    }
    if (!known) {
        err = dispatch_error::unknown_type;
        return false;
    }
    err = dispatch_error::none;
    return true;
}

std::string build_envelope(const response &r)
{
    std::string out;
    out.reserve(64);
    out += "{\"rid\":";
    append_json_string(out, r.rid);
    if (r.ok) {
        out += ",\"ok\":true,\"result\":";
        out += r.result_json.empty() ? std::string("null") : r.result_json;
        out.push_back('}');
    } else {
        out += ",\"ok\":false,\"error\":";
        append_json_string(out, r.err_detail);
        out += ",\"error_code\":";
        append_json_string(out, error_name(r.err));
        out.push_back('}');
    }
    return out;
}

std::string build_error_envelope(const std::string &rid, dispatch_error err, const std::string &detail)
{
    response r;
    r.rid = rid;
    r.ok = false;
    r.err = err;
    r.err_detail = detail;
    return build_envelope(r);
}

/* ================================================================== */
/* sys.info / wifi (API publica)                                       */
/* ================================================================== */

std::string sys_info_to_json(const SysInfo &info)
{
    std::string out;
    out.reserve(128);
    out += "{\"fw_version\":";
    append_json_string(out, info.fw_version);
    out += ",\"idf_version\":";
    append_json_string(out, info.idf_version);
    out += ",\"chip\":";
    append_json_string(out, info.chip);
    out += ",\"free_heap\":";
    out += std::to_string(info.free_heap);
    out += ",\"uptime\":";
    append_json_string(out, info.uptime);
    out.push_back('}');
    return out;
}

bool sys_info_from_json(const std::string &json, SysInfo &out)
{
    static const std::vector<std::string> wanted = {"fw_version", "idf_version", "chip", "free_heap", "uptime"};
    std::vector<json_field> fields(wanted.size());
    if (!parse_document(json.data(), json.size(), wanted, fields)) {
        return false;
    }
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        if (fields[i].k == json_field::kind::absent) {
            return false;
        }
    }
    if (fields[0].k != json_field::kind::string || fields[1].k != json_field::kind::string ||
        fields[2].k != json_field::kind::string || fields[4].k != json_field::kind::string) {
        return false;
    }
    if (!is_number_literal(fields[3])) {
        return false;
    }
    out.fw_version = fields[0].s;
    out.idf_version = fields[1].s;
    out.chip = fields[2].s;
    out.free_heap = std::atoi(fields[3].raw.c_str());
    out.uptime = fields[4].s;
    return true;
}

std::string wifi_scan_to_json(const std::vector<WifiNet> &nets)
{
    std::string out;
    out += "{\"networks\":[";
    bool first = true;
    for (const auto &n : nets) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out += "{\"ssid\":";
        append_json_string(out, truncate_utf8_ssid(n.ssid));
        out += ",\"rssi\":";
        out += std::to_string(n.rssi);
        out += ",\"open\":";
        out += n.open ? "true" : "false";
        out.push_back('}');
    }
    out += "]}";
    return out;
}

std::string handle_sys_info(const std::string &rid)
{
    SysInfo info;
#ifdef ESP_PLATFORM
    const esp_app_desc_t *app = esp_app_get_description();
    info.fw_version = (app != nullptr && app->version[0] != '\0') ? app->version : "0.0.0";
    info.idf_version = esp_get_idf_version();
    info.chip = "esp32p4";
    info.free_heap = static_cast<int>(esp_get_free_heap_size());
    const std::int64_t total_secs = esp_timer_get_time() / 1000000;
    char up[16] = {0};
    std::snprintf(up, sizeof(up), "%02d:%02d:%02d", static_cast<int>(total_secs / 3600),
                  static_cast<int>((total_secs / 60) % 60), static_cast<int>(total_secs % 60));
    info.uptime = up;
#else
    /* host: placeholder deterministico (contrato do teste) */
    info.fw_version = "0.0.0-host";
    info.idf_version = "host";
    info.chip = "host";
    info.free_heap = 0;
    info.uptime = "00:00:00";
#endif
    response r;
    r.rid = rid;
    r.ok = true;
    r.result_json = sys_info_to_json(info);
    return build_envelope(r);
}

std::string handle_wifi_scan(const std::string &rid, const std::vector<WifiNet> &nets)
{
    response r;
    r.rid = rid;
    r.ok = true;
    r.result_json = wifi_scan_to_json(nets);
    return build_envelope(r);
}

/* ================================================================== */
/* screen.dump: CRC32 / tamanho / sessao                               */
/* ================================================================== */

namespace {

struct crc32_table_gen {
    std::uint32_t t[256];
    constexpr crc32_table_gen() : t()
    {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
    }
};

constexpr crc32_table_gen k_crc_table{};

} // namespace

std::uint32_t crc32(const std::uint8_t *data, std::size_t len)
{
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = k_crc_table.t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::size_t screen_bmp_size(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const auto w = static_cast<std::size_t>(width);
    if (w > (SIZE_MAX - 3u) / 3u) {
        return 0;
    }
    const std::size_t stride = bmp_stride_local(w);
    if (stride == 0) {
        return 0;
    }
    const auto h = static_cast<std::size_t>(height);
    if (h > (SIZE_MAX - SCREENSHOT_BMP_HEADER_SIZE) / stride) {
        return 0;
    }
    const std::size_t total = SCREENSHOT_BMP_HEADER_SIZE + stride * h;
    if (total > UINT32_MAX) {
        return 0;
    }
    return total;
}

bool screen_chunk_bounds(int height, std::uint32_t chunk_index, std::uint32_t *out_start,
                         std::uint32_t *out_end)
{
    /* Auxiliar: 256 linhas (k_screen_chunk_bytes/4) por faixa. O dump real
     * pagina por BYTES do BMP (1024), nao por linha. */
    constexpr std::uint32_t k_rows_per_chunk = k_screen_chunk_bytes / 4;
    if (height <= 0 || out_start == nullptr || out_end == nullptr) {
        return false;
    }
    const auto h = static_cast<std::uint32_t>(height);
    const std::uint32_t start = chunk_index * k_rows_per_chunk;
    if (start >= h) {
        return false;
    }
    *out_start = start;
    *out_end = std::min(start + k_rows_per_chunk, h);
    return true;
}

bool screen_dump_init(const char *rid, int width, int height, const std::uint16_t *rgb565,
                      std::size_t pixel_count, std::size_t *out_total_bmp_bytes,
                      std::uint32_t *out_total_crc, std::uint32_t *out_total_chunks)
{
    if (out_total_bmp_bytes != nullptr) {
        *out_total_bmp_bytes = 0;
    }
    if (out_total_crc != nullptr) {
        *out_total_crc = 0;
    }
    if (out_total_chunks != nullptr) {
        *out_total_chunks = 0;
    }
    if (rid == nullptr || rgb565 == nullptr || out_total_bmp_bytes == nullptr || out_total_crc == nullptr ||
        out_total_chunks == nullptr) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (expected == 0 || pixel_count < expected) {
        return false;
    }
    const std::size_t total = screen_bmp_size(width, height);
    if (total == 0) {
        return false;
    }

    /* BMP top-down: linha y=0 do framebuffer primeiro, header com biHeight
     * positivo — paridade byte a byte com o oracle do teste. */
    std::vector<std::uint8_t> bmp(total, 0);
    screenshot_bmp_header_t hdr{};
    fill_header_local(&hdr, width, height);
    std::memcpy(bmp.data(), &hdr, sizeof(hdr));

    const std::size_t stride = bmp_stride_local(static_cast<std::size_t>(width));
    std::uint8_t *pixels = bmp.data() + SCREENSHOT_BMP_HEADER_SIZE;
    for (int y = 0; y < height; ++y) {
        const std::uint16_t *src = rgb565 + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::uint8_t *dst = pixels + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            std::uint8_t bgr[3];
            rgb565_to_bgr888_local(src[x], bgr);
            dst[static_cast<std::size_t>(x) * 3u + 0] = bgr[0];
            dst[static_cast<std::size_t>(x) * 3u + 1] = bgr[1];
            dst[static_cast<std::size_t>(x) * 3u + 2] = bgr[2];
        }
        /* padding [row_pixel_bytes, stride) ja e zero */
    }

    /* Publica somente apos sucesso: uma falha preserva a sessao ativa. */
    g_screen.active = true;
    g_screen.rid = rid;
    g_screen.width = width;
    g_screen.height = height;
    g_screen.bmp = std::move(bmp);
    g_screen.chunks = static_cast<std::uint32_t>((g_screen.bmp.size() + k_screen_chunk_bytes - 1) /
                                                 k_screen_chunk_bytes);

    *out_total_bmp_bytes = g_screen.bmp.size();
    *out_total_crc = crc32(g_screen.bmp.data(), g_screen.bmp.size());
    *out_total_chunks = g_screen.chunks;
    return true;
}

bool screen_dump_get_chunk(const char *rid, std::uint32_t chunk_index, std::vector<std::uint8_t> &out_bytes,
                           std::uint32_t &out_crc, bool &out_is_last)
{
    out_bytes.clear();
    out_crc = 0;
    out_is_last = false;
    if (rid == nullptr || !g_screen.active || g_screen.rid != rid) {
        return false;
    }
    if (chunk_index >= g_screen.chunks) {
        return false;
    }
    const std::size_t off = static_cast<std::size_t>(chunk_index) * k_screen_chunk_bytes;
    const std::size_t n = std::min<std::size_t>(k_screen_chunk_bytes, g_screen.bmp.size() - off);
    out_bytes.assign(g_screen.bmp.begin() + static_cast<std::ptrdiff_t>(off),
                     g_screen.bmp.begin() + static_cast<std::ptrdiff_t>(off + n));
    out_crc = crc32(out_bytes.data(), out_bytes.size());
    out_is_last = (chunk_index + 1 == g_screen.chunks);
    return true;
}

/* ================================================================== */
/* LineAssembler / tolerancia (API publica)                            */
/* ================================================================== */

void LineAssembler::feed(const char *data, std::size_t len)
{
    if (data == nullptr || len == 0) {
        return;
    }
    for (std::size_t i = 0; i < len; ++i) {
        const char c = data[i];
        if (m_discarding) {
            if (c == '\n') {
                m_discarding = false;
            }
            continue;
        }
        if (c == '\n') {
            std::string line = std::move(m_buf);
            m_buf.clear();
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            m_lines.push_back(std::move(line));
            continue;
        }
        m_buf.push_back(c);
        if (m_buf.size() > k_max_ndjson_line) {
            /* linha > 4096: descarta silenciosamente ate o proximo \n;
             * nunca entrega >4096 nem fica presa entre feeds. */
            m_buf.clear();
            m_discarding = true;
        }
    }
}

bool LineAssembler::next_line(std::string &out)
{
    if (m_lines.empty()) {
        return false;
    }
    out = std::move(m_lines.front());
    m_lines.pop_front();
    return true;
}

void LineAssembler::reset()
{
    m_buf.clear();
    m_lines.clear();
    m_discarding = false;
}

bool is_log_line(const std::string &line)
{
    std::size_t begin = 0;
    while (begin < line.size() && is_ws(line[begin])) {
        ++begin;
    }
    return begin >= line.size() || line[begin] != '{';
}

bool extract_envelope(const std::string &line, std::string &envelope)
{
    std::size_t begin = 0;
    while (begin < line.size() && is_ws(line[begin])) {
        ++begin;
    }
    if (begin >= line.size() || line[begin] != '{') {
        return false;
    }
    if (line.find("\"rid\"") == std::string::npos || line.find("\"ok\"") == std::string::npos) {
        return false;
    }
    envelope = line.substr(begin);
    return true;
}

/* ================================================================== */
/* Lado de dispositivo                                                 */
/* ================================================================== */

#ifdef ESP_PLATFORM

namespace {

/* ---- escrita de frames (mutex compartilhado com o writer de log) ---- */
SemaphoreHandle_t s_frame_mutex = nullptr;
bool s_started = false;

void write_raw(const char *data, std::size_t len)
{
    std::size_t off = 0;
    while (off < len && s_frame_mutex != nullptr) {
        const int n = usb_serial_jtag_write_bytes(data + off, len - off, pdMS_TO_TICKS(1000));
        if (n <= 0) {
            break;
        }
        off += static_cast<std::size_t>(n);
    }
}

/* Uma linha NDJSON por chamada; '\n' vira '\r\n' (o driver direto nao passa
 * pela conversao do VFS). */
void bridge_write_line(const std::string &line)
{
    if (s_frame_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    std::string out;
    out.reserve(line.size() + 8);
    char prev = 0;
    for (char c : line) {
        if (c == '\n' && prev != '\r') {
            out += "\r\n";
        } else {
            out.push_back(c);
        }
        prev = c;
    }
    write_raw(out.data(), out.size());
    xSemaphoreGive(s_frame_mutex);
}

int locked_log_writer(const char *format, va_list args)
{
    char buf[512];
    const int n = std::vsnprintf(buf, sizeof(buf), format, args);
    if (n <= 0) {
        return n;
    }
    const std::size_t len = (static_cast<std::size_t>(n) < sizeof(buf)) ? static_cast<std::size_t>(n) : sizeof(buf) - 1;
    bridge_write_line(std::string(buf, len));
    return n;
}

/* ---- helpers de resultado ---- */
dispatch_result ok_result(const request &req, std::string result_json)
{
    response r;
    r.rid = req.rid;
    r.ok = true;
    r.result_json = std::move(result_json);
    dispatch_result d;
    d.ok = true;
    d.envelope_json = build_envelope(r);
    d.err = dispatch_error::none;
    d.rid = req.rid;
    d.type = req.type;
    return d;
}

dispatch_result err_result(const request &req, dispatch_error err, const std::string &detail)
{
    dispatch_result d;
    d.ok = false;
    d.envelope_json = build_error_envelope(req.rid, err, detail);
    d.err = err;
    d.rid = req.rid;
    d.type = req.type;
    return d;
}

/* ---- injecao de teclado (fila bounded de 8; drena no tick do LVGL) ---- */
constexpr int k_clear_backspaces = 64;
constexpr std::uint32_t k_inject_delay_ms = 30;

void inject_text(const char *text, std::size_t len)
{
    if (text == nullptr || len == 0) {
        return;
    }
    cyberdeck_keyboard_input(text, len, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(k_inject_delay_ms));
}

void inject_enter()
{
    cyberdeck_keyboard_input(nullptr, 0, 0, LV_KEY_ENTER);
    vTaskDelay(pdMS_TO_TICKS(k_inject_delay_ms));
}

void inject_backspace()
{
    cyberdeck_keyboard_input(nullptr, 0, 0, LV_KEY_BACKSPACE);
    vTaskDelay(pdMS_TO_TICKS(k_inject_delay_ms));
}

bool is_utf8_continuation(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u & 0xC0) == 0x80;
}

/* Envia em segmentos de no maximo 1024 bytes, sem cortar sequencia UTF-8. */
void inject_text_segmented(const std::string &text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t n = std::min<std::size_t>(1024, text.size() - i);
        if (i + n < text.size()) {
            while (n > 1 && is_utf8_continuation(text[i + n])) {
                --n;
            }
        }
        inject_text(text.data() + i, n);
        i += n;
    }
}

/* "clear" digitado no prompt + Enter (CYBERDECK_CMD_CLEAR); prefixo de
 * backspaces limitado evita lixo residual na linha de edicao. */
void exec_ui_clear()
{
    for (int i = 0; i < k_clear_backspaces; ++i) {
        inject_backspace();
    }
    inject_text("clear", 5);
    inject_enter();
}

void exec_ui_type(const std::string &text)
{
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    lines.push_back(cur);
    if (lines.size() > 1 && lines.back().empty()) {
        lines.pop_back(); /* o \n final ja vira o Enter final */
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        inject_text_segmented(lines[i]);
        if (i + 1 < lines.size()) {
            inject_enter();
        }
    }
    inject_enter();
}

/* ---- alvos de UI ---- */
lv_obj_t *find_click_at(lv_obj_t *root, std::int32_t x, std::int32_t y)
{
    if (root == nullptr || lv_obj_is_hidden(root)) {
        return nullptr;
    }
    const auto count = static_cast<std::int32_t>(lv_obj_get_child_count(root));
    for (std::int32_t i = count - 1; i >= 0; --i) {
        lv_obj_t *hit = find_click_at(lv_obj_get_child(root, i), x, y);
        if (hit != nullptr) {
            return hit;
        }
    }
    lv_point_t pt = {x, y};
    if (lv_obj_hit_test(root, &pt)) {
        return root;
    }
    return nullptr;
}

void send_clicked_async(void *user)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(user);
    if (obj == nullptr || !lv_obj_is_in_widget_tree(obj)) {
        return;
    }
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *find_clickable_by_text(lv_obj_t *root, const std::string &want)
{
    if (root == nullptr || lv_obj_is_hidden(root)) {
        return nullptr;
    }
    const auto count = static_cast<std::int32_t>(lv_obj_get_child_count(root));
    for (std::int32_t i = count - 1; i >= 0; --i) {
        lv_obj_t *hit = find_clickable_by_text(lv_obj_get_child(root, i), want);
        if (hit != nullptr) {
            return hit;
        }
    }
    const char *text = nullptr;
    if (lv_obj_check_type(root, &lv_label_class)) {
        text = lv_label_get_text(root);
    } else if (lv_obj_check_type(root, &lv_textarea_class)) {
        text = lv_textarea_get_text(root);
    }
    if (text == nullptr || want != text) {
        return nullptr;
    }
    for (lv_obj_t *obj = root; obj != nullptr; obj = lv_obj_get_parent(obj)) {
        if (lv_obj_is_clickable(obj)) {
            return obj;
        }
    }
    return nullptr;
}

/* ---- ui.dump: arvore visivel como JSON (limitada) ---- */
constexpr int k_dump_max_nodes = 64;
constexpr std::size_t k_dump_max_bytes = 3500;
constexpr std::size_t k_dump_max_text = 120;

std::string clamp_text(const char *text)
{
    std::string s = text != nullptr ? text : "";
    if (s.size() <= k_dump_max_text) {
        return s;
    }
    std::size_t cut = k_dump_max_text;
    while (cut > 0 && is_utf8_continuation(s[cut])) {
        --cut;
    }
    s.resize(cut);
    return s;
}

void dump_obj(lv_obj_t *obj, std::string &out, int &count, bool &truncated)
{
    if (obj == nullptr || count >= k_dump_max_nodes || truncated) {
        return;
    }
    if (lv_obj_is_hidden(obj)) {
        return;
    }
    const char *cls = "other";
    std::string text;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        cls = "label";
        text = clamp_text(lv_label_get_text(obj));
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        cls = "textarea";
        text = clamp_text(lv_textarea_get_text(obj));
    } else if (lv_obj_check_type(obj, &lv_button_class)) {
        cls = "button";
    }
    lv_area_t area{};
    lv_obj_get_coords(obj, &area);
    if (count > 0) {
        out.push_back(',');
    }
    out += "{\"class\":";
    append_json_string(out, cls);
    out += ",\"text\":";
    append_json_string(out, text);
    out += ",\"x\":";
    out += std::to_string(area.x1);
    out += ",\"y\":";
    out += std::to_string(area.y1);
    out += ",\"w\":";
    out += std::to_string(area.x2 - area.x1 + 1);
    out += ",\"h\":";
    out += std::to_string(area.y2 - area.y1 + 1);
    out.push_back('}');
    ++count;
    if (out.size() >= k_dump_max_bytes) {
        truncated = true;
        return;
    }
    const auto child_count = static_cast<std::int32_t>(lv_obj_get_child_count(obj));
    for (std::int32_t i = 0; i < child_count; ++i) {
        dump_obj(lv_obj_get_child(obj, i), out, count, truncated);
        if (truncated) {
            return;
        }
    }
}

/* ---- captura de tela ---- */
struct capture {
    lv_draw_buf_t *snap = nullptr;
    std::vector<std::uint16_t> packed;
    int width = 0;
    int height = 0;
};

bool capture_screen(capture &out)
{
    if (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        return false;
    }
    out.snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();
    if (out.snap == nullptr) {
        return false;
    }
    out.width = out.snap->header.w;
    out.height = out.snap->header.h;
    if (out.snap->header.cf != LV_COLOR_FORMAT_RGB565 || out.width <= 0 || out.height <= 0 ||
        out.snap->header.stride < static_cast<std::uint32_t>(out.width) * sizeof(std::uint16_t)) {
        lv_draw_buf_destroy(out.snap);
        out.snap = nullptr;
        return false;
    }
    /* screen_dump_init espera RGB565 compacto (y*w+x); repack se o stride
     * do snapshot tiver padding. */
    const auto packed_stride = static_cast<std::size_t>(out.width) * sizeof(std::uint16_t);
    out.packed.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
    const auto *base = static_cast<const std::uint8_t *>(out.snap->data);
    for (int y = 0; y < out.height; ++y) {
        std::memcpy(out.packed.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width),
                    base + static_cast<std::size_t>(y) * out.snap->header.stride, packed_stride);
    }
    lv_draw_buf_destroy(out.snap);
    out.snap = nullptr;
    return true;
}

std::string base64_encode(const std::uint8_t *data, std::size_t len)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back(tbl[v & 63]);
    }
    if (len - i == 1) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (len - i == 2) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string frame_event(const request &req, const char *event)
{
    std::string s;
    s.reserve(96);
    s += "{\"rid\":";
    append_json_string(s, req.rid);
    s += ",\"ok\":true,\"event\":";
    append_json_string(s, event);
    s.push_back('}');
    return s;
}

void send_frame(const std::string &json)
{
    bridge_write_line(json + "\n");
}

/* Stream completo: start, chunks (Base64 canonico) e end pre-escritos;
 * envelope de retorno fica vazio (task nao reference de novo). */
device_result exec_screen_dump(const request &req)
{
    device_result d;
    d.handled = true;

    capture cap;
    if (!capture_screen(cap)) {
        d.result = err_result(req, dispatch_error::internal, "display busy ou captura falhou");
        return d;
    }
    std::size_t total = 0;
    std::uint32_t crc = 0;
    std::uint32_t chunks = 0;
    if (!screen_dump_init(req.rid.c_str(), cap.width, cap.height, cap.packed.data(), cap.packed.size(), &total,
                          &crc, &chunks)) {
        d.result = err_result(req, dispatch_error::internal, "bmp invalido");
        return d;
    }

    {
        std::string start = "{\"rid\":";
        append_json_string(start, req.rid);
        start += ",\"ok\":true,\"event\":\"start\",\"size\":";
        start += std::to_string(total);
        start += ",\"chunks\":";
        start += std::to_string(chunks);
        start += ",\"crc32\":";
        start += std::to_string(crc);
        start.push_back('}');
        send_frame(start);
    }
    bool failed = false;
    for (std::uint32_t i = 0; i < chunks; ++i) {
        std::vector<std::uint8_t> bytes;
        std::uint32_t chunk_crc = 0;
        bool is_last = false;
        if (!screen_dump_get_chunk(req.rid.c_str(), i, bytes, chunk_crc, is_last)) {
            failed = true;
            break;
        }
        std::string frame = "{\"rid\":";
        append_json_string(frame, req.rid);
        frame += ",\"ok\":true,\"event\":\"chunk\",\"chunk\":";
        frame += std::to_string(i);
        frame += ",\"crc32\":";
        frame += std::to_string(chunk_crc);
        frame += ",\"b64\":";
        append_json_string(frame, base64_encode(bytes.data(), bytes.size()));
        frame.push_back('}');
        send_frame(frame);
    }
    if (failed) {
        send_frame(build_error_envelope(req.rid, dispatch_error::internal, "chunk falhou"));
    }
    send_frame(frame_event(req, "end"));
    /* Todos os frames ja foram escritos; envelope vazio = nada a escrever. */
    return d;
}

/* ---- wifi.scan sincrono (async no wifi_mgr + semaforo estatico) ---- */
struct scan_ctx {
    SemaphoreHandle_t sem = nullptr;
    wifi_ap_record_t aps[WIFI_SCAN_MAX_APS];
    int count = 0;
    bool done = false;
};

scan_ctx s_scan;

void on_scan_done(const wifi_ap_record_t *aps, int count, void *ctx)
{
    auto *c = static_cast<scan_ctx *>(ctx);
    if (c == nullptr) {
        return;
    }
    /* O ponteiro e valido somente dentro do callback: copia aqui. */
    c->count = 0;
    if (aps != nullptr && count > 0) {
        c->count = count > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : count;
        for (int i = 0; i < c->count; ++i) {
            c->aps[i] = aps[i];
        }
    }
    c->done = true;
    if (c->sem != nullptr) {
        xSemaphoreGive(c->sem);
    }
}

device_result exec_wifi_scan(const request &req)
{
    device_result d;
    d.handled = true;
    if (s_scan.sem == nullptr) {
        d.result = err_result(req, dispatch_error::internal, "scan semaforo indisponivel");
        return d;
    }
    /* Drena Gives stale de um scan anterior. */
    while (xSemaphoreTake(s_scan.sem, 0) == pdTRUE) {
    }
    s_scan.done = false;
    s_scan.count = 0;
    const esp_err_t err = wifi_mgr_scan(on_scan_done, &s_scan);
    if (err != ESP_OK) {
        d.result = err_result(req, dispatch_error::internal, "wifi scan indisponivel");
        return d;
    }
    if (xSemaphoreTake(s_scan.sem, pdMS_TO_TICKS(WIFI_SCAN_SYNC_TIMEOUT_MS)) != pdTRUE) {
        const bool callback_owned = wifi_mgr_cancel_scan(on_scan_done, &s_scan);
        if (callback_owned) {
            /* O worker ainda possui o contexto e pode dar o semaforo depois:
             * melhor esforco para drenar sem destruir o objeto (estatico). */
            (void)xSemaphoreTake(s_scan.sem, pdMS_TO_TICKS(50));
        }
        d.result = err_result(req, dispatch_error::internal, "wifi scan timeout");
        return d;
    }
    std::vector<WifiNet> nets;
    nets.reserve(static_cast<std::size_t>(s_scan.count));
    for (int i = 0; i < s_scan.count; ++i) {
        WifiNet net;
        net.ssid = reinterpret_cast<const char *>(s_scan.aps[i].ssid);
        net.rssi = s_scan.aps[i].rssi;
        net.open = s_scan.aps[i].authmode == WIFI_AUTH_OPEN;
        nets.push_back(std::move(net));
    }
    d.result.ok = true;
    d.result.err = dispatch_error::none;
    d.result.rid = req.rid;
    d.result.type = req.type;
    response r;
    r.rid = req.rid;
    r.ok = true;
    r.result_json = wifi_scan_to_json(nets);
    d.result.envelope_json = build_envelope(r);
    return d;
}

device_result exec_wifi_status(const request &req)
{
    device_result d;
    d.handled = true;
    wifi_status_t st{};
    std::string result = "{\"connected\":false,\"has_ip\":false,\"ssid\":\"\",\"ip\":\"\"}";
    if (wifi_mgr_get_status(&st) == ESP_OK) {
        result = "{\"connected\":";
        result += st.connected ? "true" : "false";
        result += ",\"has_ip\":";
        result += st.has_ip ? "true" : "false";
        result += ",\"ssid\":";
        append_json_string(result, st.ssid);
        result += ",\"ip\":";
        append_json_string(result, st.ip);
        result.push_back('}');
    }
    d.result = ok_result(req, std::move(result));
    return d;
}

device_result exec_screen_shot(const request &req)
{
    device_result d;
    d.handled = true;
    capture cap;
    if (!capture_screen(cap)) {
        d.result = err_result(req, dispatch_error::internal, "display busy ou captura falhou");
        return d;
    }
    /* Metadados do frame (bytes/CRC ficam por conta do stream screen.dump). */
    std::string result = "{\"width\":";
    result += std::to_string(cap.width);
    result += ",\"height\":";
    result += std::to_string(cap.height);
    result += ",\"bmp_bytes\":";
    result += std::to_string(screen_bmp_size(cap.width, cap.height));
    result.push_back('}');
    d.result = ok_result(req, std::move(result));
    return d;
}

device_result device_exec(const request &req, const std::vector<json_field> &fields)
{
    device_result d;
    json_field f;

    if (req.type == "wifi.scan") {
        return exec_wifi_scan(req);
    }
    if (req.type == "wifi.status") {
        return exec_wifi_status(req);
    }
    if (req.type == "screen.dump") {
        return exec_screen_dump(req);
    }
    if (req.type == "screen.shot") {
        return exec_screen_shot(req);
    }
    if (req.type == "ui.clear") {
        d.handled = true;
        exec_ui_clear();
        d.result = ok_result(req, "{\"ok\":true}");
        return d;
    }
    if (req.type == "ui.type") {
        d.handled = true;
        if (!get_field(fields, "text", f) || f.k != json_field::kind::string) {
            d.result = err_result(req, dispatch_error::internal, "text deve ser string");
            return d;
        }
        exec_ui_type(f.s);
        d.result = ok_result(req, "{\"ok\":true}");
        return d;
    }
    if (req.type == "ui.click") {
        d.handled = true;
        json_field fx;
        json_field fy;
        if (!get_field(fields, "x", fx) || !get_field(fields, "y", fy) || !is_number_literal(fx) ||
            !is_number_literal(fy)) {
            d.result = err_result(req, dispatch_error::internal, "ui.click exige x e y numericos");
            return d;
        }
        const auto x = static_cast<std::int32_t>(std::atof(fx.raw.c_str()));
        const auto y = static_cast<std::int32_t>(std::atof(fy.raw.c_str()));
        lv_obj_t *target = nullptr;
        if (bsp_display_lock(pdMS_TO_TICKS(1000))) {
            target = find_click_at(lv_layer_top(), x, y);
            if (target == nullptr) {
                target = find_click_at(lv_screen_active(), x, y);
            }
            if (target != nullptr) {
                /* agenda com o lock ainda held: o cb roda na task do LVGL */
                lv_async_call(send_clicked_async, target);
            }
            bsp_display_unlock();
        } else {
            d.result = err_result(req, dispatch_error::internal, "display busy");
            return d;
        }
        if (target == nullptr) {
            d.result = err_result(req, dispatch_error::internal, "nenhum alvo clicavel no ponto");
            return d;
        }
        d.result = ok_result(req, "{\"ok\":true}");
        return d;
    }
    if (req.type == "ui.tap") {
        d.handled = true;
        json_field ft;
        const bool has_target = get_field(fields, "target", ft) || get_field(fields, "symbol", ft);
        if (!has_target || ft.k != json_field::kind::string || ft.s.empty()) {
            d.result = err_result(req, dispatch_error::internal, "ui.exige target/symbol");
            return d;
        }
        lv_obj_t *target = nullptr;
        if (bsp_display_lock(pdMS_TO_TICKS(1000))) {
            target = find_clickable_by_text(lv_layer_top(), ft.s);
            if (target == nullptr) {
                target = find_clickable_by_text(lv_screen_active(), ft.s);
            }
            if (target != nullptr) {
                lv_async_call(send_clicked_async, target);
            }
            bsp_display_unlock();
        } else {
            d.result = err_result(req, dispatch_error::internal, "display busy");
            return d;
        }
        if (target == nullptr) {
            d.result = err_result(req, dispatch_error::internal, "nenhum alvo visivel com o texto");
            return d;
        }
        d.result = ok_result(req, "{\"ok\":true}");
        return d;
    }
    if (req.type == "ui.dump") {
        d.handled = true;
        std::string nodes = "[";
        int count = 0;
        bool truncated = false;
        if (bsp_display_lock(pdMS_TO_TICKS(1000))) {
            dump_obj(lv_screen_active(), nodes, count, truncated);
            if (!truncated) {
                dump_obj(lv_layer_top(), nodes, count, truncated);
            }
            bsp_display_unlock();
        } else {
            d.result = err_result(req, dispatch_error::internal, "display busy");
            return d;
        }
        nodes.push_back(']');
        std::string result = "{\"nodes\":";
        result += nodes;
        result += ",\"truncated\":";
        result += truncated ? "true" : "false";
        result.push_back('}');
        d.result = ok_result(req, std::move(result));
        return d;
    }

    /* ping, ui.echo, sys.info: caminho puro. */
    return d;
}

/* Task da ponte: monta linhas, despacha e reference o envelope (logs do
 * console chegam intercalados e sao filtrados). */
void bridge_task_entry(void *)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        cfg.tx_buffer_size = 8192;
        cfg.rx_buffer_size = 8192;
        if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
            ESP_LOGE("serial_brg", "falha ao instalar driver USB Serial-JTAG");
            vTaskDelete(nullptr);
            return;
        }
    }
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_use_driver();

    LineAssembler assembler;
    std::uint8_t buf[512];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }
        assembler.feed(reinterpret_cast<const char *>(buf), static_cast<std::size_t>(n));
        std::string line;
        while (assembler.next_line(line)) {
            const std::size_t b = line.find_first_not_of(" \t\r");
            if (b == std::string::npos || line[b] != '{') {
                continue; /* log/console: nao e uma requisicao */
            }
            const dispatch_result res = dispatch_one(line.data(), line.size());
            if (!res.envelope_json.empty()) {
                bridge_write_line(res.envelope_json + "\n");
            }
        }
    }
}

} // namespace

#endif // ESP_PLATFORM

/* ================================================================== */
/* fs.write seguro (REQ-001..REQ-011)                                  */
/* ================================================================== */

namespace {

/* Path logico confinado: "<root><suffixo>" com "<root>" = "/sdcard/".
 * O host nao tem /sdcard gravavel: o SUFFIXO e reancorado na raiz fisica
 * (sandbox em /tmp, override CYBERDECK_SD_ROOT), preservando o path logico
 * do protocolo; no device a raiz fisica e o proprio /sdcard montado. */
constexpr char k_fs_write_root[] = "/sdcard/";
constexpr std::size_t k_fs_write_root_len = sizeof(k_fs_write_root) - 1; /* 8 */
constexpr std::size_t k_fs_write_max_path = 240;
constexpr std::size_t k_fs_write_max_component = 255;
constexpr std::size_t k_fs_write_temp_attempts = 8;
constexpr char k_fs_write_temp_prefix[] = ".fswrite.";
constexpr char k_fs_write_temp_suffix[] = ".tmp";

/* O contador nao precisa ser persistente: O_EXCL resolve colisoes com nomes
 * deixados por outra execucao/processo. O limite de tentativas e de nome
 * impede que uma lista de candidatos seja gerada sem fim. */
std::atomic<std::uint32_t> s_fs_write_temp_sequence{0};

std::string fs_write_make_temp_name(const std::string &base, std::uint32_t sequence)
{
    char digits[10] = {};
    std::size_t digit_count = 0;
    std::uint32_t value = sequence;
    do {
        digits[digit_count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && digit_count < sizeof(digits));
    if (value != 0u) {
        return {};
    }

    constexpr std::size_t prefix_len = sizeof(k_fs_write_temp_prefix) - 1;
    constexpr std::size_t suffix_len = sizeof(k_fs_write_temp_suffix) - 1;
    constexpr std::size_t fixed_len = prefix_len + suffix_len;
    if (base.size() > k_fs_write_max_component ||
        fixed_len + digit_count > k_fs_write_max_component ||
        base.size() > k_fs_write_max_component - fixed_len - digit_count) {
        return {};
    }

    std::string out;
    out.reserve(base.size() + fixed_len + digit_count);
    out += base;
    out += k_fs_write_temp_prefix;
    for (std::size_t i = digit_count; i > 0; --i) {
        out.push_back(digits[i - 1]);
    }
    out += k_fs_write_temp_suffix;
    return out;
}

std::string fs_write_next_temp_name(const std::string &base)
{
    return fs_write_make_temp_name(
        base, s_fs_write_temp_sequence.fetch_add(1u, std::memory_order_relaxed));
}

dispatch_result fs_write_error(const request &req, dispatch_error err, const std::string &detail)
{
    dispatch_result d;
    d.ok = false;
    d.err = err;
    d.rid = req.rid;
    d.type = req.type;
    /* Envelope de uma linha, sem '\\n' interno, com rid ecoado. */
    d.envelope_json = build_error_envelope(req.rid, err, detail);
    return d;
}

dispatch_result fs_write_success(const request &req, const std::vector<std::uint8_t> &bytes)
{
    std::string result = "{\"size\":";
    result += std::to_string(bytes.size());
    result += ",\"crc32\":";
    result += std::to_string(crc32(bytes.data(), bytes.size()));
    result.push_back('}');
    response r;
    r.rid = req.rid;
    r.ok = true;
    r.result_json = std::move(result);
    dispatch_result d;
    d.ok = true;
    d.err = dispatch_error::none;
    d.rid = req.rid;
    d.type = req.type;
    d.envelope_json = build_envelope(r);
    return d;
}

/* Validacao lexica do path: confinamento /sdcard, sem traversal "..",
 * sem componente vazio, sem barra final (alvo nao pode ser diretorio),
 * sem caracteres de controle/NUL e tamanho limitado. Nenhum toque em disco
 * acontece antes desta aprovacao. */
bool fs_write_validate_path(const std::string &path, std::string &detail)
{
    if (path.empty()) {
        detail = "path vazio";
        return false;
    }
    if (path.size() > k_fs_write_max_path) {
        detail = "path longo demais";
        return false;
    }
    for (char c : path) {
        if (is_ctrl(c)) {
            detail = "path com caractere de controle (NUL/control)";
            return false;
        }
        /* O VFS ESP usa '/', mas rejeitar '\\' evita que a mesma string
         * pathname seja interpretada como traversal por outra camada. */
        if (c == '\\') {
            detail = "path com separador invalido";
            return false;
        }
    }
    if (path.size() <= k_fs_write_root_len ||
        path.compare(0, k_fs_write_root_len, k_fs_write_root) != 0) {
        detail = "path fora da raiz confinada /sdcard";
        return false;
    }
    if (path.back() == '/') {
        detail = "path aponta para um diretorio (termina em /)";
        return false;
    }
    std::size_t start = k_fs_write_root_len;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::size_t length = end - start;
        if (length == 0) {
            detail = "path com componente vazio";
            return false;
        }
        if (length > k_fs_write_max_component) {
            detail = "componente de path longo demais";
            return false;
        }
        if (path.compare(start, length, "..") == 0 || path.compare(start, length, ".") == 0) {
            detail = "traversal '..' nao permitido no path";
            return false;
        }
        if (end >= path.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

/* size e lido somente como inteiro decimal sem sinal e ja limitado ao
 * teto por digito (sem overflow, inclusive em size_t de 32 bits). */
bool fs_write_parse_size(const json_field &field, std::size_t &out)
{
    if (field.k != json_field::kind::number || field.raw.empty()) {
        return false;
    }
    /* JSON nao admite zero inicial; alem disso, o calculo e limitado antes
     * da multiplicacao para nunca sofrer wrap em size_t. */
    if (field.raw.size() > 1 && field.raw[0] == '0') {
        return false;
    }
    std::size_t value = 0;
    for (char c : field.raw) {
        if (!is_digit(c)) {
            return false; /* rejeita "-5", "5.0", "1e3"... */
        }
        const std::size_t digit = static_cast<std::size_t>(c - '0');
        if (value > (k_fs_write_max_bytes - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    out = value;
    return true;
}

bool b64_value(char c, std::uint32_t &out)
{
    if (c >= 'A' && c <= 'Z') {
        out = static_cast<std::uint32_t>(c - 'A');
        return true;
    }
    if (c >= 'a' && c <= 'z') {
        out = static_cast<std::uint32_t>(c - 'a' + 26);
        return true;
    }
    if (c >= '0' && c <= '9') {
        out = static_cast<std::uint32_t>(c - '0' + 52);
        return true;
    }
    if (c == '+') {
        out = 62;
        return true;
    }
    if (c == '/') {
        out = 63;
        return true;
    }
    return false;
}

std::string b64_encode_canonical(const std::uint8_t *data, std::size_t len)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back(tbl[v & 63]);
    }
    if (len - i == 1) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (len - i == 2) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

/* Base64 estrito e canonico: comprimento multiplo de 4, somente alfabeto,
 * '=' apenas no fim (no maximo 2, alinhado ao ultimo quarteto) e
 * re-encode bit-identico a entrada (descarta bits residuais/padding extra).
 * A saida e NUL-safe (bytes arbitrarios em vector, nunca C-string). */
bool b64_decode_canonical(const std::string &in, std::vector<std::uint8_t> &out)
{
    out.clear();
    if (in.size() % 4 != 0) {
        return false;
    }
    const std::size_t first_pad = in.find('=');
    if (first_pad != std::string::npos) {
        const std::size_t pads = in.size() - first_pad;
        if (pads > 2) {
            return false;
        }
        for (std::size_t i = first_pad; i < in.size(); ++i) {
            if (in[i] != '=') {
                return false;
            }
        }
    }
    const std::size_t body = (first_pad == std::string::npos) ? in.size() : first_pad;
    for (std::size_t i = 0; i < body; ++i) {
        std::uint32_t discard = 0;
        if (!b64_value(in[i], discard)) {
            return false;
        }
    }
    out.reserve((in.size() / 4) * 3);
    for (std::size_t i = 0; i < in.size(); i += 4) {
        std::uint32_t v = 0;
        std::size_t vals = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = in[i + j];
            if (c == '=') {
                break;
            }
            std::uint32_t d = 0;
            if (!b64_value(c, d)) {
                out.clear();
                return false;
            }
            v = (v << 6) | d;
            ++vals;
        }
        if (vals < 2 || (vals < 4 && i + 4 != in.size())) {
            out.clear();
            return false;
        }
        v <<= (4 - vals) * 6;
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        if (vals >= 3) {
            out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        }
        if (vals == 4) {
            out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        }
    }
    if (b64_encode_canonical(out.data(), out.size()) != in) {
        out.clear();
        return false; /* nao canonico (bits residuais/padding nao canonico) */
    }
    return true;
}

/* Raiz fisica do alvo. Device: /sdcard (ponto de montagem do SD). Host: o
 * teste nao tem /sdcard entao o sufixo logico e reancorado em um sandbox
 * gravavel (CYBERDECK_SD_ROOT, default /tmp/cyberdeck5_sd). */
std::string fs_write_physical_root()
{
#ifdef ESP_PLATFORM
    return "/sdcard";
#else
    const char *env = std::getenv("CYBERDECK_SD_ROOT");
    std::string root = (env != nullptr && env[0] != '\0') ? std::string(env) : std::string("/tmp/cyberdeck5_sd");
    /* O override e apenas um sandbox host; nunca aceite um root relativo. */
    if (root.empty() || root.front() != '/') {
        root = "/tmp/cyberdeck5_sd";
    }
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    return root;
#endif
}

void fs_write_ensure_physical_root(const std::string &root)
{
#if !defined(ESP_PLATFORM)
    /* No host cria a raiz do sandbox (um nivel) para que o pai direto do
     * alvo exista; no device /sdcard e montado no boot e NUNCA e criado
     * aqui (falha de montagem vira erro tipado no passo de parent). */
    struct stat info {};
    if (::stat(root.c_str(), &info) == 0) {
        return;
    }
    (void)::mkdir(root.c_str(), 0755);
#else
    (void)root;
#endif
}

enum class fs_lookup { missing, found, not_directory, failed };

/* O host usa lstat() para exercitar a protecao contra symlinks. O ESP-IDF 5.5.5
 * expoe stat(), mas nao lstat nem openat por dirfd. A excecao de plataforma
 * e segura somente porque o prefixo fisico /sdcard do firmware e montado pelo
 * BSP como FATFS: FATFS nao possui entradas symlink, portanto nenhum
 * componente validado pode redirigir a escrita para fora da raiz. */
fs_lookup fs_lookup_path(const std::string &path, struct stat &info)
{
#ifdef ESP_PLATFORM
    if (::stat(path.c_str(), &info) == 0) {
        return fs_lookup::found;
    }
#else
    if (::lstat(path.c_str(), &info) == 0) {
        return fs_lookup::found;
    }
#endif
    if (errno == ENOENT) {
        return fs_lookup::missing;
    }
    if (errno == ENOTDIR) {
        return fs_lookup::not_directory;
    }
    return fs_lookup::failed;
}

/* Validacao em disco: cada prefixo do caminho (raiz + todos os parents,
 * inclusive o parent direto) precisa existir como diretorio e sem symlink;
 * o alvo final precisa, se existir, ser arquivo regular (rejeita symlink e
 * diretorio). Falha de consulta vira io_error; o resto, invalid_path.
 * `target_exists` permite ao commit ESP aplicar no-clobber sem fazer uma
 * segunda consulta por path. */
bool fs_write_validate_disk(const std::string &physical, bool &target_exists,
                            dispatch_error &err, std::string &detail)
{
    target_exists = false;
    std::vector<std::string> prefixes;
    {
        std::string current = physical;
        for (;;) {
            prefixes.push_back(current);
            const std::size_t slash = current.find_last_of('/');
            if (slash == std::string::npos || slash == 0) {
                break; /* nao desce alem da raiz do filesystem */
            }
            current.resize(slash);
        }
        std::reverse(prefixes.begin(), prefixes.end());
    }
    if (prefixes.size() < 2) {
        err = dispatch_error::invalid_path;
        detail = "path invalido (sem parent)";
        return false;
    }
    for (std::size_t i = 0; i + 1 < prefixes.size(); ++i) {
        struct stat info {};
        const fs_lookup state = fs_lookup_path(prefixes[i], info);
        if (state == fs_lookup::missing) {
            err = dispatch_error::invalid_path;
            detail = "parent ausente no path";
            return false;
        }
        if (state == fs_lookup::not_directory) {
            err = dispatch_error::invalid_path;
            detail = "componente do path nao e diretorio";
            return false;
        }
        if (state == fs_lookup::failed) {
            err = dispatch_error::io_error;
            detail = "falha ao consultar o parent";
            return false;
        }
        if (S_ISLNK(info.st_mode)) {
            err = dispatch_error::invalid_path;
            detail = "symlink nao permitido no path";
            return false;
        }
        if (!S_ISDIR(info.st_mode)) {
            err = dispatch_error::invalid_path;
            detail = "componente do path nao e diretorio";
            return false;
        }
    }
    struct stat target {};
    const fs_lookup state = fs_lookup_path(physical, target);
    if (state == fs_lookup::found) {
        target_exists = true;
        if (S_ISLNK(target.st_mode)) {
            err = dispatch_error::invalid_path;
            detail = "alvo e um symlink";
            return false;
        }
        if (S_ISDIR(target.st_mode)) {
            err = dispatch_error::invalid_path;
            detail = "alvo e um diretorio";
            return false;
        }
        if (!S_ISREG(target.st_mode)) {
            err = dispatch_error::invalid_path;
            detail = "alvo nao e um arquivo regular";
            return false;
        }
    } else if (state == fs_lookup::not_directory) {
        err = dispatch_error::invalid_path;
        detail = "componente do path nao e diretorio";
        return false;
    } else if (state == fs_lookup::failed) {
        err = dispatch_error::io_error;
        detail = "falha ao consultar o alvo";
        return false;
    }
    return true;
}

bool fs_write_write_bytes(int fd, const std::vector<std::uint8_t> &bytes)
{
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

#if !defined(ESP_PLATFORM) && defined(O_DIRECTORY) && defined(O_NOFOLLOW) && defined(AT_FDCWD) && \
    defined(AT_SYMLINK_NOFOLLOW)
/* No host, percorre e abre cada parent por descriptor.  Assim uma troca de
 * um componente por symlink entre lstat e open nao redireciona a escrita. */
bool fs_write_open_parent(const std::string &root, const std::string &relative, int &out_dir,
                          std::string &out_name, dispatch_error &err, std::string &detail)
{
    out_dir = -1;
    out_name.clear();
    int dir = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) {
        err = dispatch_error::io_error;
        detail = "falha ao abrir a raiz confinada";
        return false;
    }
    struct stat root_info {};
    if (::fstat(dir, &root_info) != 0 || !S_ISDIR(root_info.st_mode) || S_ISLNK(root_info.st_mode)) {
        (void)::close(dir);
        err = dispatch_error::invalid_path;
        detail = "raiz confinada invalida";
        return false;
    }

    std::size_t start = 0;
    for (;;) {
        std::size_t end = relative.find('/', start);
        if (end == std::string::npos) {
            end = relative.size();
        }
        if (end == start) {
            (void)::close(dir);
            err = dispatch_error::invalid_path;
            detail = "path com componente vazio";
            return false;
        }
        const std::string component = relative.substr(start, end - start);
        if (component == "." || component == ".." || component.empty()) {
            (void)::close(dir);
            err = dispatch_error::invalid_path;
            detail = "traversal '..' nao permitido no path";
            return false;
        }
        if (end == relative.size()) {
            out_dir = dir;
            out_name = component;
            return true;
        }
        const int next = ::openat(dir, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (next < 0) {
            const int saved = errno;
            (void)::close(dir);
            err = (saved == ENOENT || saved == ENOTDIR) ? dispatch_error::invalid_path
                                                       : dispatch_error::io_error;
            detail = (err == dispatch_error::invalid_path) ? "parent ausente no path"
                                                          : "falha ao abrir o parent";
            return false;
        }
        struct stat info {};
        if (::fstat(next, &info) != 0 || !S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
            (void)::close(next);
            (void)::close(dir);
            err = dispatch_error::invalid_path;
            detail = "componente do path nao e diretorio";
            return false;
        }
        (void)::close(dir);
        dir = next;
        start = end + 1;
    }
}
#endif

/* Commit por temp unico (.fswrite.<sequencia>.tmp no mesmo diretorio) + fsync
 * + rename. Cada nome candidato e criado com O_CREAT|O_EXCL; um nome ocupado
 * apenas gera a proxima tentativa, nunca e removido. Depois de um create
 * bem-sucedido, somente esse temp possuido por esta invocacao pode ser
 * removido em caso de falha.
 * No host com openat, o renameat POSIX fornece substituicao atomica. O
 * caminho ESP nao oferece openat/renameat: como o VFS FATFS nao documenta
 * replace atomico, rejeita um destino ja observado e nunca remove o destino
 * para tentar o rename novamente. Assim, uma falha preserva qualquer destino. */
dispatch_result fs_write_commit(const request &req, const std::string &physical,
                                const std::string &root, const std::string &relative,
                                const std::vector<std::uint8_t> &bytes, bool target_exists)
{
#if !defined(ESP_PLATFORM) && defined(O_DIRECTORY) && defined(O_NOFOLLOW) && defined(AT_FDCWD) && \
    defined(AT_SYMLINK_NOFOLLOW)
    (void)physical;
    (void)target_exists;
    int dir = -1;
    std::string name;
    dispatch_error open_err = dispatch_error::none;
    std::string open_detail;
    if (!fs_write_open_parent(root, relative, dir, name, open_err, open_detail)) {
        return fs_write_error(req, open_err, open_detail);
    }

    struct stat existing {};
    if (::fstatat(dir, name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(existing.st_mode) || !S_ISREG(existing.st_mode)) {
            (void)::close(dir);
            return fs_write_error(req, dispatch_error::invalid_path, "alvo nao e arquivo regular");
        }
    } else if (errno != ENOENT) {
        const int saved = errno;
        (void)::close(dir);
        char detail[96];
        std::snprintf(detail, sizeof(detail), "falha ao consultar o alvo (errno=%d)", saved);
        return fs_write_error(req,
                              (saved == ENOTDIR) ? dispatch_error::invalid_path : dispatch_error::io_error,
                              detail);
    }

    std::string temporary_name;
    int fd = -1;
    bool temporary_created = false;
    for (std::size_t attempt = 0; attempt < k_fs_write_temp_attempts; ++attempt) {
        temporary_name = fs_write_next_temp_name(name);
        if (temporary_name.empty()) {
            (void)::close(dir);
            return fs_write_error(req, dispatch_error::io_error,
                                  "nome do temporario excede o limite");
        }
        fd = ::openat(dir, temporary_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            temporary_created = true;
            break;
        }
        if (errno != EEXIST) {
            (void)::close(dir);
            return fs_write_error(req, dispatch_error::io_error,
                                  "falha ao abrir o arquivo temporario");
        }
    }
    if (fd < 0) {
        (void)::close(dir);
        return fs_write_error(req, dispatch_error::io_error,
                              "sem nome temporario unico disponivel");
    }
    bool ok = fs_write_write_bytes(fd, bytes);
    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        if (temporary_created) {
            (void)::unlinkat(dir, temporary_name.c_str(), 0);
        }
        (void)::close(dir);
        return fs_write_error(req, dispatch_error::io_error, "falha ao gravar o arquivo temporario");
    }
    if (::renameat(dir, temporary_name.c_str(), dir, name.c_str()) != 0) {
        const int saved = errno;
        if (temporary_created) {
            (void)::unlinkat(dir, temporary_name.c_str(), 0);
        }
        (void)::close(dir);
        char detail[96];
        std::snprintf(detail, sizeof(detail), "rename falhou (errno=%d)", saved);
        return fs_write_error(req, dispatch_error::io_error, detail);
    }
    (void)::close(dir);
    return fs_write_success(req, bytes);
#else
    (void)root;
    (void)relative;
    if (target_exists) {
#ifdef ESP_PLATFORM
        return fs_write_error(req, dispatch_error::io_error,
                              "FATFS sem replace atomico garantido: sobrescrita rejeitada");
#else
        return fs_write_error(req, dispatch_error::io_error,
                              "plataforma sem replace atomico garantido: sobrescrita rejeitada");
#endif
    }

    const std::size_t slash = physical.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= physical.size()) {
        return fs_write_error(req, dispatch_error::invalid_path, "caminho fisico invalido");
    }
    const std::string directory = physical.substr(0, slash + 1);
    const std::string base_name = physical.substr(slash + 1);

    int open_flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_NOFOLLOW)
    open_flags |= O_NOFOLLOW;
#endif
    std::string temporary;
    int fd = -1;
    bool temporary_created = false;
    for (std::size_t attempt = 0; attempt < k_fs_write_temp_attempts; ++attempt) {
        const std::string temporary_name = fs_write_next_temp_name(base_name);
        if (temporary_name.empty()) {
            return fs_write_error(req, dispatch_error::io_error,
                                  "nome do temporario excede o limite");
        }
        temporary = directory + temporary_name;
        fd = ::open(temporary.c_str(), open_flags, 0600);
        if (fd >= 0) {
            temporary_created = true;
            break;
        }
        if (errno != EEXIST) {
            return fs_write_error(req, dispatch_error::io_error,
                                  "falha ao abrir o arquivo temporario");
        }
    }
    if (fd < 0) {
        return fs_write_error(req, dispatch_error::io_error,
                              "sem nome temporario unico disponivel");
    }
    struct stat opened {};
    if (::fstat(fd, &opened) != 0 || S_ISLNK(opened.st_mode) || !S_ISREG(opened.st_mode)) {
        (void)::close(fd);
        if (temporary_created) {
            (void)::unlink(temporary.c_str());
        }
        return fs_write_error(req, dispatch_error::io_error, "temporario nao e arquivo regular seguro");
    }
    bool ok = fs_write_write_bytes(fd, bytes);
    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok) {
        if (temporary_created) {
            (void)::unlink(temporary.c_str());
        }
        return fs_write_error(req, dispatch_error::io_error, "falha ao gravar o arquivo temporario");
    }
    if (::rename(temporary.c_str(), physical.c_str()) != 0) {
        const int saved = errno;
        if (temporary_created) {
            (void)::unlink(temporary.c_str());
        }
        char detail[96];
        std::snprintf(detail, sizeof(detail), "rename falhou (errno=%d)", saved);
        return fs_write_error(req, dispatch_error::io_error, detail);
    }
    return fs_write_success(req, bytes);
#endif
}

/* Despacho de fs.write: valida rid ja ecoado, campos path/data_b64/size com
 * tipos corretos, path confinado, Base64 estrito canonico, size == tamanho
 * decodificado <= 2048 e entao faz commit seguro por temp unico + rename.
 * Aloca no maximo o payload decodificado (<= 2048) + path (<= 240) + nome
 * temporario (<= 255). */
dispatch_result exec_fs_write(const request &req, const std::vector<json_field> &fields)
{
    json_field path_field;
    json_field data_field;
    json_field size_field;
    if (!get_field(fields, "path", path_field) || !get_field(fields, "data_b64", data_field) ||
        !get_field(fields, "size", size_field)) {
        return fs_write_error(req, dispatch_error::invalid_payload, "fs.write exige path, data_b64 e size");
    }
    if (path_field.k != json_field::kind::string) {
        return fs_write_error(req, dispatch_error::invalid_path, "path deve ser string");
    }
    if (data_field.k != json_field::kind::string) {
        return fs_write_error(req, dispatch_error::invalid_payload, "data_b64 deve ser string");
    }
    std::string path_detail;
    if (!fs_write_validate_path(path_field.s, path_detail)) {
        return fs_write_error(req, dispatch_error::invalid_path, path_detail);
    }
    std::size_t size = 0;
    if (!fs_write_parse_size(size_field, size)) {
        return fs_write_error(req, dispatch_error::invalid_payload, "size deve ser inteiro entre 0 e 2048");
    }
    std::vector<std::uint8_t> bytes;
    if (!b64_decode_canonical(data_field.s, bytes)) {
        return fs_write_error(req, dispatch_error::invalid_payload, "data_b64 fora do base64 estrito canonico");
    }
    if (bytes.size() > k_fs_write_max_bytes) {
        return fs_write_error(req, dispatch_error::invalid_payload, "payload acima de 2048 bytes decodificados");
    }
    if (size != bytes.size()) {
        return fs_write_error(req, dispatch_error::invalid_payload, "size difere do tamanho decodificado");
    }
    const std::string root = fs_write_physical_root();
    fs_write_ensure_physical_root(root);
    const std::string relative = path_field.s.substr(k_fs_write_root_len);
    const std::string physical = root == "/" ? root + relative : root + "/" + relative;
    dispatch_error disk_err = dispatch_error::none;
    std::string disk_detail;
    bool target_exists = false;
    if (!fs_write_validate_disk(physical, target_exists, disk_err, disk_detail)) {
        return fs_write_error(req, disk_err, disk_detail);
    }
    return fs_write_commit(req, physical, root, relative, bytes, target_exists);
}

} // namespace

/* ================================================================== */
/* dispatch_one                                                        */
/* ================================================================== */

dispatch_result dispatch_one(const char *line, std::size_t len)
{
    dispatch_result res;
    request req;
    dispatch_error err = dispatch_error::none;
    if (!parse_ndjson_line(line, len, req, err)) {
        res.ok = false;
        res.err = err;
        res.rid = req.rid; /* preenchido quando disponivel (ex.: unknown_type) */
        res.type = req.type;
        res.envelope_json = build_error_envelope(req.rid, err, error_name(err));
        return res;
    }
    res.rid = req.rid;
    res.type = req.type;

    std::vector<json_field> fields(k_request_fields.size());
    if (!parse_document(req.payload_raw.data(), req.payload_raw.size(), k_request_fields, fields)) {
        res.ok = false;
        res.err = dispatch_error::invalid_json;
        res.envelope_json = build_error_envelope(req.rid, dispatch_error::invalid_json, "invalid_json");
        return res;
    }

#ifdef ESP_PLATFORM
    {
        const device_result d = device_exec(req, fields);
        if (d.handled) {
            return d.result;
        }
    }
#endif

    response r;
    r.rid = req.rid;
    r.ok = true;
    json_field f;

    if (req.type == "ping") {
        r.result_json = "{\"pong\":true}";
    } else if (req.type == "sys.info") {
        res.envelope_json = handle_sys_info(req.rid);
        res.ok = true;
        return res;
    } else if (req.type == "wifi.scan") {
        res.envelope_json = handle_wifi_scan(req.rid, {});
        res.ok = true;
        return res;
    } else if (req.type == "wifi.status") {
        r.result_json = "{\"connected\":false,\"has_ip\":false,\"ssid\":\"\",\"ip\":\"\"}";
    } else if (req.type == "ui.echo") {
        if (!get_field(fields, "text", f) || f.k != json_field::kind::string) {
            response e;
            e.rid = req.rid;
            e.ok = false;
            e.err = dispatch_error::internal;
            e.err_detail = "text deve ser string";
            res.ok = false;
            res.err = e.err;
            res.envelope_json = build_envelope(e);
            return res;
        }
        if (f.s.size() > 2048) {
            r.result_json = "{\"truncated\":true}";
        } else {
            r.result_json = "{\"text\":";
            append_json_string(r.result_json, f.s);
            r.result_json.push_back('}');
        }
    } else if (req.type == "fs.write") {
        /* REQ-001..REQ-011: caminho comum host/device (no device o
         * device_exec nao trata fs.write e cai aqui). */
        return exec_fs_write(req, fields);
    } else {
        /* Host stubs para comandos que no device rodam no device_exec. */
        r.result_json = "{\"ok\":true}";
    }

    res.envelope_json = build_envelope(r);
    res.ok = r.ok;
    res.err = r.err;
    return res;
}

#ifdef ESP_PLATFORM

bool bridge_start(void)
{
    if (s_started) {
        return true;
    }
    if (s_frame_mutex == nullptr) {
        s_frame_mutex = xSemaphoreCreateMutex();
        if (s_frame_mutex == nullptr) {
            return false;
        }
    }
    if (s_scan.sem == nullptr) {
        static StaticSemaphore_t s_scan_storage;
        s_scan.sem = xSemaphoreCreateCountingStatic(4, 0, &s_scan_storage);
        if (s_scan.sem == nullptr) {
            return false;
        }
    }
    (void)esp_log_set_vprintf(locked_log_writer);
    const BaseType_t created =
        xTaskCreate(bridge_task_entry, "serial_brg", 8192, nullptr, 3, nullptr);
    if (created != pdPASS) {
        return false;
    }
    s_started = true;
    return true;
}

#endif // ESP_PLATFORM

} // namespace cyberdeck_serial
