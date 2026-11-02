/*
 * Host-side TDD RED contract for USB Serial-JTAG NDJSON manual bridge.
 * Cobre REQ-002 (dispatch NDJSON bounded e erros) e REQ-003 (rid/envelopes)
 * e REQ-005 (comandos UI) conforme plano aprovado.
 *
 * Contrato esperado (producao ausente -> RED):
 *   Header: components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h
 *   Source: components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp
 *   Namespace: cyberdeck_serial
 *   Simbolos:
 *     constexpr size_t k_max_ndjson_line = 4096;
 *     constexpr size_t k_max_rid_len = 64;
 *     enum class dispatch_error { none, too_large, invalid_json, missing_rid,
 *                                  missing_type, unknown_type, invalid_utf8 };
 *     struct request { std::string rid; std::string type; std::string payload_raw; };
 *     struct response { std::string rid; bool ok; std::string result_json; dispatch_error err; std::string err_detail; };
 *     bool parse_ndjson_line(const char* data, size_t len, request& out, dispatch_error& err);
 *     std::string build_envelope(const response& r); // {"rid":..., "ok":..., "result"/"error":...}
 *     std::string build_error_envelope(const std::string& rid, dispatch_error err, const std::string& detail);
 *     dispatch_result dispatch_one(const char* line, size_t len); // bounded, nunca aloca > k_max_ndjson_line
 *
 * Sem hardware, sem pyserial, sem FreeRTOS/LVGL.
 * O teste falha em compilacao ate o coder criar os modulos (RED por design).
 */
#include "apps/serial/cyberdeck_serial_bridge.h"
#include "apps/music/cyberdeck_music_player.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int s_failures = 0;
int s_checks = 0;

#define CHECK(cond) do { ++s_checks; if (!(cond)) { ++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECK_EQ(a,b) do { ++s_checks; if ((a)!=(b)) { ++s_failures; std::printf("FAIL %s:%d expected '%s' vs '%s'\n", __FILE__, __LINE__, #b, #a); } } while(0)

inline std::string esc(const std::string& s){
    std::string r; for(unsigned char c: s){ if(c=='\n') r+="\\n"; else if(c=='\r') r+="\\r"; else if(c==0) r+="\\0"; else if(c<32||c>=127){ char b[8]; std::snprintf(b,sizeof(b),"\\x%02X",c); r+=b;} else r+=char(c);} return r;
}
#define CHECK_STR_EQ(actual, expected) do { ++s_checks; if ((actual)!=(expected)) { ++s_failures; std::printf("FAIL %s:%d expected '%s' actual '%s'\n", __FILE__, __LINE__, esc(expected).c_str(), esc(actual).c_str()); } } while(0)

struct diagnostics_fixture { int calls = 0; bool available = true; };
bool provide_diagnostics(void *context, cyberdeck_music::diagnostics_snapshot &out) {
    auto &fixture = *static_cast<diagnostics_fixture *>(context);
    ++fixture.calls;
    out.generation = 17;
    out.read_max_us = 11;
    out.decode_max_us = 22;
    out.codec_start_max_us = 33;
    out.write_max_us = 44;
    out.route_check_calls = 4;
    out.route_check_time_us = 55;
    out.route_check_max_us = 22;
    out.backend_write_calls = 3;
    out.backend_write_time_us = 66;
    out.backend_write_max_us = 33;
    out.pause_count = 2;
    out.pause_time_us = 900;
    return fixture.available;
}
bool provide_trace(void *context, std::uint32_t cursor, std::uint32_t limit,
                   cyberdeck_music::trace_page &out) {
    auto &fixture = *static_cast<diagnostics_fixture *>(context);
    ++fixture.calls;
    if (!fixture.available) return false;
    if (cursor == 99 && limit == 2) {
        out.count = UINT32_MAX;
        out.next = cursor;
        return true;
    }
    if (cursor == 0 && limit == cyberdeck_music::k_trace_page_capacity) {
        out.count = cyberdeck_music::k_trace_page_capacity;
        out.cursor = 0;
        out.next = cyberdeck_music::k_trace_page_capacity;
        for (std::uint32_t i = 0; i < out.count; ++i)
            out.events[i] = {UINT32_MAX, UINT64_MAX, UINT32_MAX,
                             cyberdeck_music::trace_kind::failure, INT32_MIN, UINT32_MAX, UINT32_MAX};
        return true;
    }
    if (cursor != 4 || limit != 2) return false;
    out.count = 1;
    out.next = 5;
    out.events[0] = {4, 123456789ULL, 9, cyberdeck_music::trace_kind::sd_read, 16, 27, 16};
    return true;
}

void test_bounded_max_line_rejected() {
    CHECK(cyberdeck_serial::k_max_ndjson_line == 4096);
    CHECK(cyberdeck_serial::k_max_rid_len == 64);
    std::string oversized(cyberdeck_serial::k_max_ndjson_line + 1, 'a');
    cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
    CHECK(!cyberdeck_serial::parse_ndjson_line(oversized.data(), oversized.size(), req, err));
    CHECK(err == cyberdeck_serial::dispatch_error::too_large);
    // empty line also must be rejected, not crash
    CHECK(!cyberdeck_serial::parse_ndjson_line("", 0, req, err));
    CHECK(err == cyberdeck_serial::dispatch_error::invalid_json || err == cyberdeck_serial::dispatch_error::missing_rid);
    // exactly at limit with valid JSON must not be too_large
    const std::string payload(512, 'x');
    const std::string at_limit = "{\"rid\":\"r1\",\"type\":\"ping\",\"payload\":\"" + payload + "\"}";
    CHECK(at_limit.size() <= cyberdeck_serial::k_max_ndjson_line);
    {
        cyberdeck_serial::dispatch_error e2; cyberdeck_serial::request r2;
        bool ok = cyberdeck_serial::parse_ndjson_line(at_limit.data(), at_limit.size(), r2, e2);
        CHECK(ok);
        CHECK(e2 == cyberdeck_serial::dispatch_error::none);
        CHECK(r2.rid == "r1");
        CHECK(r2.type == "ping");
    }
    // one byte over limit must be too_large even if otherwise valid JSON
    std::string over = at_limit + std::string(cyberdeck_serial::k_max_ndjson_line - at_limit.size() + 1, 'y');
    CHECK(over.size() == cyberdeck_serial::k_max_ndjson_line + 1);
    CHECK(!cyberdeck_serial::parse_ndjson_line(over.data(), over.size(), req, err));
    CHECK(err == cyberdeck_serial::dispatch_error::too_large);
}

void test_invalid_json_and_missing_fields() {
    cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
    // JSON truncado (compute actual length, do not trust hard-coded)
    {
        const char* s = "{\"rid\":\"a\",\"type\"";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::invalid_json);
    }
    // valid JSON but missing rid / type / empty rid
    {
        const char* s1 = "{\"type\":\"ping\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s1, std::strlen(s1), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_rid);
    }
    {
        const char* s2 = "{\"rid\":\"r1\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s2, std::strlen(s2), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_type);
    }
    {
        const char* s3 = "{\"rid\":\"\",\"type\":\"ping\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s3, std::strlen(s3), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_rid);
    }
    // whitespace and trailing newline must be accepted (NDJSON is trimmed)
    {
        const char* s = "  {\"rid\":\"r1\",\"type\":\"ping\"}  \n";
        CHECK(cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::none);
        CHECK(req.rid == "r1");
    }
    // rid too long (>64) -> rejected
    {
        std::string long_rid(65,'r');
        std::string line = "{\"rid\":\"" + long_rid + "\",\"type\":\"ping\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_rid || err == cyberdeck_serial::dispatch_error::too_large || err == cyberdeck_serial::dispatch_error::invalid_json);
    }
    // rid boundary 64 must be accepted
    {
        std::string ok_rid(64,'a');
        std::string line = "{\"rid\":\"" + ok_rid + "\",\"type\":\"ping\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(line.data(), line.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::none);
        CHECK(req.rid.size() == 64);
    }
    // unknown type -> unknown_type
    {
        const char* s = "{\"rid\":\"r1\",\"type\":\"unknown.cmd\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::unknown_type);
    }
    {
        const char *line = "{\"rid\":\"audio\",\"type\":\"music.diagnostics\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(line, std::strlen(line), req, err));
        CHECK(req.type == "music.diagnostics");
    }
    // invalid UTF-8 byte inside string must be invalid_utf8 (or at least invalid_json), never none
    {
        std::string bad_utf8 = std::string("{\"rid\":\"r1\",\"type\":\"ping\",\"p\":\"") + char(0xFF) + "\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(bad_utf8.data(), bad_utf8.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::invalid_utf8 || err == cyberdeck_serial::dispatch_error::invalid_json);
    }
    // NUL inside JSON must not be treated as C-string terminator
    {
        std::string with_nul = std::string("{\"rid\":\"r1\",\"type\":\"ping\",\"x\":\"a") + char(0) + "b\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(with_nul.data(), with_nul.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::invalid_json || err == cyberdeck_serial::dispatch_error::invalid_utf8);
    }
}

void test_rid_envelope_correlation() {
    {
        cyberdeck_serial::response resp; resp.rid="abc-123_~"; resp.ok=true; resp.result_json="{\"pong\":1}";
        std::string env = cyberdeck_serial::build_envelope(resp);
        CHECK(env.find("\"rid\":\"abc-123_~\"") != std::string::npos);
        CHECK(env.find("\"ok\":true") != std::string::npos);
        CHECK(env.find("\"result\"") != std::string::npos);
        CHECK(env.find('\n') == std::string::npos);
        CHECK(env.front() == '{' && env.back() == '}');
    }
    {
        std::string err_env = cyberdeck_serial::build_error_envelope("my-rid-1", cyberdeck_serial::dispatch_error::invalid_json, "unexpected token");
        CHECK(err_env.find("\"rid\":\"my-rid-1\"") != std::string::npos);
        CHECK(err_env.find("\"ok\":false") != std::string::npos);
        CHECK(err_env.find("\"error\"") != std::string::npos);
        CHECK(err_env.find('\n') == std::string::npos);
    }
    {
        const char* s1 = "{\"rid\":\"r42\",\"type\":\"ping\"}";
        auto r1 = cyberdeck_serial::dispatch_one(s1, std::strlen(s1));
        CHECK(r1.envelope_json.find("\"rid\":\"r42\"") != std::string::npos);
        CHECK(r1.envelope_json.find("\"ok\"") != std::string::npos);
        CHECK(r1.envelope_json.front() == '{');
    }
    {
        const char* s2 = "{\"rid\":\"R_9-._\",\"type\":\"ping\"}";
        auto r2 = cyberdeck_serial::dispatch_one(s2, std::strlen(s2));
        CHECK(r2.envelope_json.find("\"rid\":\"R_9-._\"") != std::string::npos);
    }
    // Error path must synthesize rid when input has no rid (cannot echo missing rid)
    {
        const char* bad = "not json";
        auto r = cyberdeck_serial::dispatch_one(bad, std::strlen(bad));
        CHECK(!r.ok);
        // envelope must still be valid NDJSON — either synthesized rid or explicit error without echoing garbage
        CHECK(r.envelope_json.front() == '{' && r.envelope_json.back() == '}');
        CHECK(r.envelope_json.find("\"ok\":false") != std::string::npos);
        CHECK(r.envelope_json.find('\n') == std::string::npos);
    }
}

void test_ui_commands_dispatch() {
    cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
    {
        const char* s = "{\"rid\":\"u1\",\"type\":\"ui.echo\",\"text\":\"hi\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::none);
        CHECK(req.type == "ui.echo");
        CHECK(req.rid == "u1");
    }
    {
        const char* s = "{\"rid\":\"u2\",\"type\":\"ui.clear\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::none);
        CHECK(req.type == "ui.clear");
    }
    {
        const char* commands[] = {
            "screen on",
            "screen off",
            "screen timeout 0",
             "screen timeout 1440",
             "battery protection on",
             "battery protection off",
             "battery protection status",
         };
        for (std::size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
            const std::string rid = "screen-" + std::to_string(i);
            const std::string json = "{\"rid\":\"" + rid +
                                     "\",\"type\":\"ui.type\",\"text\":\"" +
                                     commands[i] + "\"}";
            cyberdeck_serial::request screen_request;
            cyberdeck_serial::dispatch_error screen_error;
            CHECK(cyberdeck_serial::parse_ndjson_line(
                json.data(), json.size(), screen_request, screen_error));
            CHECK(screen_error == cyberdeck_serial::dispatch_error::none);
            CHECK(screen_request.type == "ui.type");

            const auto screen_result =
                cyberdeck_serial::dispatch_one(json.data(), json.size());
            CHECK(screen_result.ok);
            CHECK(screen_result.envelope_json.find(
                      "\"rid\":\"" + rid + "\"") != std::string::npos);
            CHECK(screen_result.envelope_json.find("\"ok\":true") !=
                  std::string::npos);
        }
    }
    {
        const char* s = "{\"rid\":\"u3\",\"type\":\"ui.echo\",\"text\":\"hello\"}";
        auto res = cyberdeck_serial::dispatch_one(s, std::strlen(s));
        CHECK(res.ok);
        CHECK(res.envelope_json.find("\"ok\":true") != std::string::npos);
        CHECK(res.envelope_json.find("\"rid\":\"u3\"") != std::string::npos);
        CHECK(res.envelope_json.find('\n') == std::string::npos);
    }
    // Invalid arg type must still produce valid envelope with rid, not crash
    {
        const char* s = "{\"rid\":\"u4\",\"type\":\"ui.echo\",\"text\":123}";
        auto res2 = cyberdeck_serial::dispatch_one(s, std::strlen(s));
        CHECK(res2.envelope_json.find("\"rid\":\"u4\"") != std::string::npos);
        CHECK(res2.envelope_json.find("\"ok\"") != std::string::npos);
        CHECK(res2.envelope_json.find('\n') == std::string::npos);
    }
}

void test_bounded_determinism_and_no_overflow() {
    {
        const char* s = "{\"rid\":\"d1\",\"type\":\"ping\"}";
        auto a = cyberdeck_serial::dispatch_one(s, std::strlen(s));
        auto b = cyberdeck_serial::dispatch_one(s, std::strlen(s));
        CHECK_STR_EQ(a.envelope_json, b.envelope_json);
        CHECK(a.envelope_json.find('\n') == std::string::npos);
        // nullptr/zero must not crash — treat as invalid
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error e;
        CHECK(!cyberdeck_serial::parse_ndjson_line(nullptr, 0, req, e));
        auto r_null = cyberdeck_serial::dispatch_one(nullptr, 0);
        CHECK(!r_null.ok);
        CHECK(r_null.envelope_json.find("\"ok\":false") != std::string::npos);
    }
    {
        std::string large = "{\"rid\":\"r1\",\"type\":\"ping\",\"data\":\"" + std::string(3000,'x') + "\"}";
        auto c = cyberdeck_serial::dispatch_one(large.data(), large.size());
        CHECK(c.envelope_json.size() <= cyberdeck_serial::k_max_ndjson_line + 256);
        CHECK(c.envelope_json.find('\n') == std::string::npos);
    }
    {
        std::string with_nul = std::string("{\"rid\":\"r1\",\"type\":\"ping\",\"x\":\"a") + char(0) + "b\"}";
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error e;
        CHECK(!cyberdeck_serial::parse_ndjson_line(with_nul.data(), with_nul.size(), req, e));
    }
}

void test_error_envelope_is_valid_ndjson() {
    // Toda envelope de erro deve ser JSON valido de uma linha terminavel por \n e conter rid
    std::string bad = "not json at all";
    auto r = cyberdeck_serial::dispatch_one(bad.data(), bad.size());
    CHECK(!r.ok);
    CHECK(r.envelope_json.find("\"rid\"") != std::string::npos || r.envelope_json.find("\"error\"") != std::string::npos);
    // Envelope deve ser parseavel como JSON (contem chaves e nao tem newline interno)
    CHECK(r.envelope_json.find('\n') == std::string::npos);
    CHECK(r.envelope_json.front() == '{' && r.envelope_json.back() == '}');
}

} // namespace

int main(){
    test_bounded_max_line_rejected();
    test_invalid_json_and_missing_fields();
    test_rid_envelope_correlation();
    test_ui_commands_dispatch();
    test_bounded_determinism_and_no_overflow();
    test_error_envelope_is_valid_ndjson();
    {
        cyberdeck_music::diagnostics_snapshot zero{};
        const std::string json = cyberdeck_serial::music_diagnostics_to_json(zero);
        CHECK(json.find("\"version\":1,\"read_only\":true") == 1);
        CHECK(json.find("\"read_calls\":0") != std::string::npos);
        CHECK(json.find("\"read_max_us\":0") != std::string::npos);
        CHECK(json.find("\"decode_max_us\":0") != std::string::npos);
        CHECK(json.find("\"codec_start_max_us\":0") != std::string::npos);
        CHECK(json.find("\"write_max_us\":0") != std::string::npos);
        CHECK(json.find("\"route_check_calls\":0") != std::string::npos);
        CHECK(json.find("\"backend_write_calls\":0") != std::string::npos);
        CHECK(json.find("\"pause_time_us\":0") != std::string::npos);
        CHECK(json.find("\"failure_counts\":{\"open\":0,\"id3\":0,\"decode\":0,\"codec_start\":0,\"codec_write\":0,\"read\":0,\"io\":0}") != std::string::npos);
        zero.failure_stage = 4;
        CHECK(cyberdeck_serial::music_diagnostics_to_json(zero).find("\"failure_stage\":\"codec_start\"") != std::string::npos);
        zero.failure_stage = 255;
        CHECK(cyberdeck_serial::music_diagnostics_to_json(zero).find("\"failure_stage\":null") != std::string::npos);
        zero.write_calls = UINT64_MAX;
        const std::string saturated = cyberdeck_serial::music_diagnostics_to_json(zero);
        CHECK(saturated.size() < 768);
        CHECK(saturated.find("\"write_calls\":18446744073709551615") != std::string::npos);
    }
    {
        const char *line = "{\"rid\":\"diag\",\"type\":\"music.diagnostics\"}";
        diagnostics_fixture fixture;
        cyberdeck_serial::set_music_diagnostics_provider_for_test(provide_diagnostics, &fixture);
        const auto result = cyberdeck_serial::dispatch_one(line, std::strlen(line));
        CHECK(result.ok);
        CHECK(result.envelope_json.find("\"read_only\":true") != std::string::npos);
        CHECK(result.envelope_json.find("\"generation\":17") != std::string::npos);
        CHECK(result.envelope_json.find("\"pause_time_us\":900") != std::string::npos);
        CHECK(result.envelope_json.find("\"read_max_us\":11") != std::string::npos);
        CHECK(result.envelope_json.find("\"decode_max_us\":22") != std::string::npos);
        CHECK(result.envelope_json.find("\"codec_start_max_us\":33") != std::string::npos);
        CHECK(result.envelope_json.find("\"write_max_us\":44") != std::string::npos);
        CHECK(result.envelope_json.find("\"route_check_calls\":4") != std::string::npos);
        CHECK(result.envelope_json.find("\"route_check_time_us\":55") != std::string::npos);
        CHECK(result.envelope_json.find("\"route_check_max_us\":22") != std::string::npos);
        CHECK(result.envelope_json.find("\"backend_write_calls\":3") != std::string::npos);
        CHECK(result.envelope_json.find("\"backend_write_time_us\":66") != std::string::npos);
        CHECK(result.envelope_json.find("\"backend_write_max_us\":33") != std::string::npos);
        CHECK(fixture.calls == 1);
        fixture.available = false;
        const auto unavailable = cyberdeck_serial::dispatch_one(line, std::strlen(line));
        CHECK(!unavailable.ok);
        CHECK(unavailable.err == cyberdeck_serial::dispatch_error::player_not_running);
        CHECK(fixture.calls == 2);
        const char *unknown = "{\"rid\":\"x\",\"type\":\"unknown\"}";
        cyberdeck_serial::dispatch_one(unknown, std::strlen(unknown));
        CHECK(fixture.calls == 2);
        cyberdeck_serial::set_music_diagnostics_provider_for_test(nullptr, nullptr);
    }
    {
        diagnostics_fixture fixture;
        cyberdeck_serial::set_music_trace_provider_for_test(provide_trace, &fixture);
        const char *line = "{\"rid\":\"trace\",\"type\":\"music.trace\",\"cursor\":4,\"limit\":2}";
        const auto result = cyberdeck_serial::dispatch_one(line, std::strlen(line));
        CHECK(result.ok);
        CHECK(result.envelope_json.find("\"read_only\":true") != std::string::npos);
        CHECK(result.envelope_json.find("\"kind\":\"sd_read\"") != std::string::npos);
        CHECK(result.envelope_json.find("\"time_us\":123456789") != std::string::npos);
        CHECK(result.envelope_json.find("\"next\":5") != std::string::npos);
        CHECK(result.envelope_json.size() < cyberdeck_serial::k_max_ndjson_line);
        CHECK(fixture.calls == 1);
        const char *max_page = "{\"rid\":\"trace-max\",\"type\":\"music.trace\",\"cursor\":0,\"limit\":16}";
        const auto bounded = cyberdeck_serial::dispatch_one(max_page, std::strlen(max_page));
        CHECK(bounded.ok);
        CHECK(bounded.envelope_json.size() < cyberdeck_serial::k_max_ndjson_line);
        CHECK(bounded.envelope_json.find("\"time_us\":18446744073709551615") != std::string::npos);
        const char *bad_count = "{\"rid\":\"trace-count\",\"type\":\"music.trace\",\"cursor\":99,\"limit\":2}";
        const auto clamped = cyberdeck_serial::dispatch_one(bad_count, std::strlen(bad_count));
        CHECK(clamped.ok && clamped.envelope_json.size() < cyberdeck_serial::k_max_ndjson_line);
        std::size_t seq_count = 0;
        for (std::size_t pos = 0; (pos = clamped.envelope_json.find("\"seq\":", pos)) != std::string::npos;
             pos += 6)
            ++seq_count;
        CHECK(seq_count == cyberdeck_music::k_trace_page_capacity);
        CHECK(fixture.calls == 3);
        fixture.available = false;
        const auto stopped = cyberdeck_serial::dispatch_one(line, std::strlen(line));
        CHECK(!stopped.ok && stopped.err == cyberdeck_serial::dispatch_error::player_not_running);
        CHECK(fixture.calls == 4);
        const char *bad = "{\"rid\":\"trace\",\"type\":\"music.trace\",\"limit\":17}";
        CHECK(!cyberdeck_serial::dispatch_one(bad, std::strlen(bad)).ok);
        CHECK(fixture.calls == 4);
        cyberdeck_serial::set_music_trace_provider_for_test(nullptr, nullptr);
    }
    if (s_failures==0) { std::printf("PASS: serial_ndjson_dispatch (%d checks)\n", s_checks); return 0; }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks); return 1;
}
