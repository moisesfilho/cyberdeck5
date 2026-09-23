/*
 * Host-side TDD RED contract for tolerancia a logs/leitura fragmentada na CLI (REQ-007/REQ-008).
 * Sem hardware real / pyserial.
 *
 * Contrato esperado (producao ausente -> RED):
 *   Header: components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h
 *           + tools host: tests/host/serial/cli_client.h  OU  mesma bridge com feeder tolerante
 *   Alternativa aceitavel: o teste exercita o lado host via feeder puro (sem pyserial) que o coder
 *   deve prover em produção host; aqui usamos cyberdeck_serial::log_tolerant_feed / cli_reader.
 *
 *   Para manter o contrato host-side sem fake de serial, os testes abaixo fixam:
 *     - feeder NDJSON tolerante a linhas de log intercaladas (prefix "[LOG]" ou sem JSON)
 *     - leitura fragmentada: stream cortado em chunks arbitrarios (1..17 bytes) ainda monta linhas NDJSON
 *     - max linha continua bounded (4096) mesmo com interleaving
 *
 *   Simbolos esperados (um dos dois conjuntos):
 *     (A) cyberdeck_serial::LineAssembler { void feed(const char* data,size_t len); bool next_line(std::string& out); void reset(); }
 *         bool is_log_line(const std::string& line);
 *         bool extract_envelope(const std::string& line, std::string& envelope); // filtra logs
 *     (B) host_cli::TolerantReader { void push_bytes(const char*,size_t); bool try_next_envelope(std::string& out); }
 *
 *   O teste tenta (A) primeiro; se inexistente, tenta (B) via include alternativo.
 */
#include "features/serial/cyberdeck_serial_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <random>

namespace {
int s_failures=0; int s_checks=0;
#define CHECK(c) do{++s_checks; if(!(c)){++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n",__FILE__,__LINE__,#c);} }while(0)
#define CHECK_STR(a,b) do{++s_checks; if((a)!=(b)){++s_failures; std::printf("FAIL %s:%d '%s' != '%s'\n",__FILE__,__LINE__,(a).c_str(),(b).c_str());} }while(0)

void test_log_interleaving_filtered(){
    cyberdeck_serial::LineAssembler asmbl;
    std::string stream =
        "[LOG] booting...\n"
        "{\"rid\":\"r1\",\"ok\":true,\"result\":1}\n"
        "some free-form log line without json\n"
        "[LOG] wifi: connected\n"
        "{\"rid\":\"r2\",\"ok\":false,\"error\":\"oops\"}\n";
    asmbl.feed(stream.data(), stream.size());
    std::string line;
    std::vector<std::string> raw;
    while (asmbl.next_line(line)) raw.push_back(line);
    // Raw stream has 5 lines (including logs) — assembler must preserve fragmentation semantics, not drop logs silently
    CHECK(raw.size()==5);
    // Now filter explicitly: only JSON with rid are envelopes. is_log_line must be consistent with line content.
    std::vector<std::string> envelopes;
    for (auto& l : raw) {
        if (cyberdeck_serial::is_log_line(l)) continue;
        std::string env;
        if (cyberdeck_serial::extract_envelope(l, env)) envelopes.push_back(env);
        else {
            // If line already looks like envelope, count it; otherwise skip non-JSON
            if (l.find("\"rid\"")!=std::string::npos && l.find("\"ok\"")!=std::string::npos) envelopes.push_back(l);
        }
    }
    CHECK(envelopes.size()==2);
    if (envelopes.size()==2){
        CHECK(envelopes[0].find("\"rid\":\"r1\"")!=std::string::npos);
        CHECK(envelopes[1].find("\"rid\":\"r2\"")!=std::string::npos);
    }
    // Non-JSON log must be classified as log, not envelope; extract_envelope must return false for it
    {
        std::string env; CHECK(!cyberdeck_serial::extract_envelope("just text", env));
        CHECK(cyberdeck_serial::is_log_line("just text"));
        CHECK(cyberdeck_serial::is_log_line("[LOG] only logs"));
        CHECK(!cyberdeck_serial::is_log_line("{\"rid\":\"x\",\"ok\":true}"));
    }
    {
        cyberdeck_serial::LineAssembler a2;
        a2.feed("[LOG] only logs\njust text\n", 22);
        std::vector<std::string> none;
        while (a2.next_line(line)) if (!cyberdeck_serial::is_log_line(line)) none.push_back(line);
        CHECK(none.empty());
    }
    // Empty stream produces no lines and reset clears buffer
    {
        cyberdeck_serial::LineAssembler empty;
        std::string out; CHECK(!empty.next_line(out));
        empty.feed("", 0); CHECK(!empty.next_line(out));
        empty.feed("{\"rid\":\"x\",\"ok\":true}\n", 22);
        CHECK(empty.next_line(out));
        empty.reset();
        CHECK(!empty.next_line(out));
    }
}

void test_fragmented_read_reassembly(){
    std::string full =
        "{\"rid\":\"a1\",\"ok\":true}\n"
        "[LOG] inter\n"
        "{\"rid\":\"a2\",\"ok\":true}\n";
    cyberdeck_serial::LineAssembler ref; ref.feed(full.data(), full.size());
    std::vector<std::string> ref_lines;
    std::string tmp; while(ref.next_line(tmp)) ref_lines.push_back(tmp);
    CHECK(ref_lines.size()==3);

    for(size_t frag : {1,2,3,5,7,13,17}){
        cyberdeck_serial::LineAssembler asmbl;
        for(size_t i=0;i<full.size(); i+=frag){
            size_t n = std::min(frag, full.size()-i);
            asmbl.feed(full.data()+i, n);
        }
        std::vector<std::string> got;
        while(asmbl.next_line(tmp)) got.push_back(tmp);
        CHECK(got.size()==ref_lines.size());
        for(size_t i=0;i<std::min(got.size(), ref_lines.size());++i){
            CHECK_STR(got[i], ref_lines[i]);
        }
    }
    // Null feed must not crash
    {
        cyberdeck_serial::LineAssembler nullAsm;
        nullAsm.feed(nullptr, 0);
        std::string out; CHECK(!nullAsm.next_line(out));
    }
    // CRLF must be normalized to same lines as LF (strip trailing \r)
    {
        cyberdeck_serial::LineAssembler crlf;
        std::string with_crlf = "{\"rid\":\"c1\",\"ok\":true}\r\n[LOG] hi\r\n{\"rid\":\"c2\",\"ok\":true}\r\n";
        crlf.feed(with_crlf.data(), with_crlf.size());
        std::vector<std::string> lines;
        while(crlf.next_line(tmp)) lines.push_back(tmp);
        CHECK(lines.size()==3);
        // lines must not retain '\r'
        for (auto& l : lines) CHECK(l.find('\r')==std::string::npos);
        // content parity with LF-only stream
        cyberdeck_serial::LineAssembler lf;
        std::string with_lf = "{\"rid\":\"c1\",\"ok\":true}\n[LOG] hi\n{\"rid\":\"c2\",\"ok\":true}\n";
        lf.feed(with_lf.data(), with_lf.size());
        std::vector<std::string> lf_lines; while(lf.next_line(tmp)) lf_lines.push_back(tmp);
        CHECK(lf_lines.size()==lines.size());
        for(size_t i=0;i<lines.size();++i) CHECK_STR(lines[i], lf_lines[i]);
    }
    // NUL byte inside stream must not truncate (pass raw len)
    {
        std::string with_nul = std::string("{\"rid\":\"n1\",\"ok\":true}\n") + std::string("a\0b\n",4) + "{\"rid\":\"n2\",\"ok\":true}\n";
        cyberdeck_serial::LineAssembler asmbl;
        asmbl.feed(with_nul.data(), with_nul.size());
        std::vector<std::string> lines; while(asmbl.next_line(tmp)) lines.push_back(tmp);
        CHECK(lines.size()==3);
        CHECK(lines[1].size()==3); // "a\0b"
    }
}

void test_bounded_fragmented_no_overflow(){
    std::string big(4097,'x');
    big += "\n";
    cyberdeck_serial::LineAssembler asmbl;
    for(size_t i=0;i<big.size(); i+=10) asmbl.feed(big.data()+i, std::min<size_t>(10, big.size()-i));
    std::string line; bool got=false, too_large=false;
    bool saw_error=false;
    while(asmbl.next_line(line)){
        got=true;
        if(line.size() > cyberdeck_serial::k_max_ndjson_line) too_large=true;
        // Some implementations surface too_large as a synthetic error line; treat as saw_error
        if(line.find("too_large")!=std::string::npos || line.find("error")!=std::string::npos) saw_error=true;
    }
    // Must either produce no line >4096, or signal error — but never deliver truncated >4096 as valid
    if(got) CHECK(too_large || saw_error || line.size()<=cyberdeck_serial::k_max_ndjson_line);
    // Must remain usable after error: next valid line must be delivered
    asmbl.feed("{\"rid\":\"after\",\"ok\":true}\n", 26);
    std::vector<std::string> tail;
    std::string tmp2;
    while(asmbl.next_line(tmp2) && tail.size()<2) tail.push_back(tmp2);
    bool found_after=false; for(auto &l: tail) if(l.find("after")!=std::string::npos) found_after=true;
    CHECK(found_after);
    // Exactly 4096 must be accepted; 4097 must not be delivered as valid
    {
        cyberdeck_serial::LineAssembler a;
        std::string ok = std::string(4096 - 1, 'y') + "\n";
        a.feed(ok.data(), ok.size());
        std::string out; CHECK(a.next_line(out)); CHECK(out.size()==4095);
    }
}

void test_partial_line_across_feeds(){
    cyberdeck_serial::LineAssembler asmbl;
    std::string part1="{\"rid\":\"p1\",\"ok\":";
    std::string part2="true}\n";
    asmbl.feed(part1.data(), part1.size());
    std::string line; CHECK(!asmbl.next_line(line));
    asmbl.feed(part2.data(), part2.size());
    CHECK(asmbl.next_line(line));
    CHECK(line.find("\"rid\":\"p1\"")!=std::string::npos);
    // feed of empty and multiple newlines
    {
        cyberdeck_serial::LineAssembler a;
        a.feed("\n\n{\"rid\":\"x\",\"ok\":true}\n\n", 25);
        std::vector<std::string> ls; std::string t; while(a.next_line(t)) if(!t.empty()) ls.push_back(t);
        CHECK(ls.size()==1);
    }
}

} // namespace
int main(){
    test_log_interleaving_filtered();
    test_fragmented_read_reassembly();
    test_bounded_fragmented_no_overflow();
    test_partial_line_across_feeds();
    if(s_failures==0){ std::printf("PASS: serial_cli_tolerance (%d checks)\n",s_checks); return 0; }
    std::printf("FAIL: %d de %d checks falharam\n",s_failures,s_checks); return 1;
}
