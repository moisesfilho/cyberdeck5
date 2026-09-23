/*
 * Host-side TDD RED contract for fs.write seguro (REQ-001..REQ-011).
 * Cobre:
 *   - protocolo rid/type/path/data_b64/size
 *   - request validation (missing/invalid fields)
 *   - limite 2048 decoded
 *   - path safety (traversal, absolute, empty, ..)
 *   - strict canonical base64
 *   - atomic/CRC response contract
 *   - NDJSON errors tipados com rid ecoado
 *   - preserve cat / serial contracts existentes
 *
 * Contrato esperado (producao ausente -> RED):
 *   Header: components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h
 *   Source: components/cyberdeck/src/features/serial/cyberdeck_serial_bridge.cpp
 *           + shell/fs_write puro (se separado)
 *   Namespace: cyberdeck_serial
 *   Extensoes esperadas:
 *     constexpr size_t k_fs_write_max_bytes = 2048;
 *     dispatch deve aceitar type "fs.write" e validar path/data_b64/size
 *     strict canonical base64: decode+re-encode == original
 *     path confinado a /sdcard, rejeita "..", traversal, empty, NUL
 *     size == decoded_len, rejeita >2048
 *     resposta ok:true contem result com size/crc32 (ou similar) e sem \n interno
 *     erros sao NDJSON com rid ecoado e error_code tipado
 *
 * Sem hardware, sem pyserial. O teste compila contra o header existente e
 * fica RED enquanto fs.write nao for implementado (unknown_type / missing).
 */
#include "features/serial/cyberdeck_serial_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
int s_failures = 0;
int s_checks = 0;

#define CHECK(cond) do { ++s_checks; if (!(cond)) { ++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECK_STR_EQ(a,b) do { ++s_checks; if ((a)!=(b)) { ++s_failures; std::printf("FAIL %s:%d expected '%s' actual '%s'\n", __FILE__, __LINE__, (b).c_str(), (a).c_str()); } } while(0)

namespace fs = std::filesystem;

class ScopedFsWriteSandbox {
public:
    ScopedFsWriteSandbox()
    {
        const char *previous = std::getenv("CYBERDECK_SD_ROOT");
        had_previous_ = previous != nullptr;
        if (had_previous_) {
            previous_ = previous;
        }
        char pattern[] = "/tmp/cyberdeck-fs-write-XXXXXX";
        char *created = ::mkdtemp(pattern);
        if (created == nullptr) {
            CHECK(false);
            return;
        }
        root_ = fs::path(created);
        env_ready_ = ::setenv("CYBERDECK_SD_ROOT", created, 1) == 0;
        CHECK(env_ready_);
    }

    ~ScopedFsWriteSandbox()
    {
        if (had_previous_) {
            (void)::setenv("CYBERDECK_SD_ROOT", previous_.c_str(), 1);
        } else {
            (void)::unsetenv("CYBERDECK_SD_ROOT");
        }
        if (!root_.empty()) {
            std::error_code error;
            fs::remove_all(root_, error);
        }
    }

    bool ready() const { return env_ready_ && !root_.empty(); }
    const fs::path &root() const { return root_; }

private:
    fs::path root_;
    std::string previous_;
    bool had_previous_ = false;
    bool env_ready_ = false;
};

static bool write_text(const fs::path &path, const std::string &data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return output.good();
}

static std::string read_text(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static std::string b64_canonical(const std::string &raw){
    static const char tbl[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((raw.size()+2)/3)*4);
    size_t i=0;
    for(; i+3<=raw.size(); i+=3){
        uint32_t v=(uint32_t)(uint8_t)raw[i]<<16 | (uint32_t)(uint8_t)raw[i+1]<<8 | (uint8_t)raw[i+2];
        out.push_back(tbl[(v>>18)&63]); out.push_back(tbl[(v>>12)&63]); out.push_back(tbl[(v>>6)&63]); out.push_back(tbl[v&63]);
    }
    if(raw.size()-i==1){ uint32_t v=(uint32_t)(uint8_t)raw[i]<<16; out.push_back(tbl[(v>>18)&63]); out.push_back(tbl[(v>>12)&63]); out+="=="; }
    else if(raw.size()-i==2){ uint32_t v=(uint32_t)(uint8_t)raw[i]<<16 | (uint32_t)(uint8_t)raw[i+1]<<8; out.push_back(tbl[(v>>18)&63]); out.push_back(tbl[(v>>12)&63]); out.push_back(tbl[(v>>6)&63]); out.push_back('='); }
    return out;
}

static std::string make_fs_write_req(const std::string &rid, const std::string &path, const std::string &data_b64, int size){
    std::string s="{\"rid\":\""+rid+"\",\"type\":\"fs.write\",\"path\":\""+path+"\",\"data_b64\":\""+data_b64+"\",\"size\":"+std::to_string(size)+"}";
    return s;
}

void test_preexisting_temp_candidate_preserved()
{
    ScopedFsWriteSandbox sandbox;
    CHECK(sandbox.ready());
    if (!sandbox.ready()) {
        return;
    }

    /* O processo host comeca com sequence=0. Este e o primeiro caso de
     * fs.write executado, portanto .0.tmp deve sofrer colisao O_EXCL. */
    const std::string base_name = "atomic.bin";
    const std::string candidate_name = base_name + ".fswrite.0.tmp";
    const fs::path candidate = sandbox.root() / candidate_name;
    const std::string candidate_contents = "KEEP";
    CHECK(write_text(candidate, candidate_contents));
    if (candidate_contents != read_text(candidate)) {
        return;
    }

    const std::string payload = "NEW-ATOMIC";
    const std::string line = make_fs_write_req("r-collision", "/sdcard/" + base_name,
                                                b64_canonical(payload), static_cast<int>(payload.size()));
    const auto result = cyberdeck_serial::dispatch_one(line.data(), line.size());

    CHECK(result.ok);
    CHECK(result.err == cyberdeck_serial::dispatch_error::none);
    CHECK(result.envelope_json.find("\"ok\":true") != std::string::npos);
    CHECK(result.envelope_json.find('\n') == std::string::npos);
    CHECK(read_text(sandbox.root() / base_name) == payload);

    /* O candidato preexistente e no-clobber: nunca pode ser apagado. O
     * temporario criado pela chamada deve desaparecer via rename ou cleanup. */
    CHECK(read_text(candidate) == candidate_contents);
    std::size_t temporary_files = 0;
    std::error_code directory_error;
    for (std::filesystem::directory_iterator it(sandbox.root(), directory_error), end;
         !directory_error && it != end; ++it) {
        const std::string name = it->path().filename().string();
        if (name.find(".fswrite.") != std::string::npos) {
            ++temporary_files;
            CHECK(name == candidate_name);
        }
    }
    CHECK(!directory_error);
    CHECK(temporary_files == 1);
}

void test_known_type_and_constants(){
    // k_fs_write_max_bytes deve existir e ser 2048; se nao existir, este teste falha em compilacao -> RED
    // Para manter compilacao antes da implementacao, verificamos via dispatch behavior
    cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
    std::string hello_b64 = b64_canonical("hello");
    std::string line = make_fs_write_req("r1","/sdcard/a.bin", hello_b64, 5);
    bool ok = cyberdeck_serial::parse_ndjson_line(line.data(), line.size(), req, err);
    // Antes da implementacao, parse deve rejeitar unknown_type; apos, deve aceitar
    CHECK(ok);
    CHECK(err == cyberdeck_serial::dispatch_error::none);
    CHECK(req.type == "fs.write");
    CHECK(req.rid == "r1");
    // Verifica constante se exposta (tolerante ate producao existir).
    // Antes da producao k_fs_write_max_bytes nao existe; teste ainda deve compilar
    // e validar o limite via comportamento de dispatch >2048 (test_size_and_base64_limits).
    // Se o header expor a constante, validamos valor; senao, pulamos sem quebrar compilacao.
    // (compile-time detection via __has_include nao cobre constexpr; usamos comportamento)
    (void)hello_b64;
}

void test_missing_fields_rejected(){
    cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
    std::string hello_b64 = b64_canonical("hi");
    // missing path
    {
        std::string s="{\"rid\":\"r1\",\"type\":\"fs.write\",\"data_b64\":\""+hello_b64+"\",\"size\":2}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s.data(), s.size(), req, err) || !cyberdeck_serial::dispatch_one(s.data(), s.size()).ok);
    }
    // missing data_b64
    {
        std::string s="{\"rid\":\"r1\",\"type\":\"fs.write\",\"path\":\"/sdcard/a.bin\",\"size\":2}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s.data(), s.size(), req, err) || !cyberdeck_serial::dispatch_one(s.data(), s.size()).ok);
    }
    // missing size
    {
        std::string s="{\"rid\":\"r1\",\"type\":\"fs.write\",\"path\":\"/sdcard/a.bin\",\"data_b64\":\""+hello_b64+"\"}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s.data(), s.size(), req, err) || !cyberdeck_serial::dispatch_one(s.data(), s.size()).ok);
    }
    // missing rid
    {
        std::string s="{\"type\":\"fs.write\",\"path\":\"/sdcard/a.bin\",\"data_b64\":\""+hello_b64+"\",\"size\":2}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s.data(), s.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_rid);
    }
    // missing type
    {
        std::string s="{\"rid\":\"r1\",\"path\":\"/sdcard/a.bin\",\"data_b64\":\""+hello_b64+"\",\"size\":2}";
        CHECK(!cyberdeck_serial::parse_ndjson_line(s.data(), s.size(), req, err));
        CHECK(err == cyberdeck_serial::dispatch_error::missing_type);
    }
    // empty path/data_b64 should be rejected by dispatch, not parse alone
    {
        std::string s="{\"rid\":\"r1\",\"type\":\"fs.write\",\"path\":\"\",\"data_b64\":\""+hello_b64+"\",\"size\":2}";
        auto r = cyberdeck_serial::dispatch_one(s.data(), s.size());
        CHECK(!r.ok);
        CHECK(r.envelope_json.find("\"rid\":\"r1\"")!=std::string::npos);
        CHECK(r.envelope_json.find("\"ok\":false")!=std::string::npos);
    }
}

void test_path_traversal_rejected(){
    auto expect_reject = [](const std::string &path){
        std::string b64 = b64_canonical("x");
        std::string line = make_fs_write_req("r-path",""+path, b64, 1);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.envelope_json.find("\"rid\":\"r-path\"")!=std::string::npos);
        CHECK(res.envelope_json.find("\"ok\":false")!=std::string::npos);
        CHECK(res.envelope_json.find('\n')==std::string::npos);
    };
    expect_reject("../escape.bin");
    expect_reject("/sdcard/../etc/passwd");
    expect_reject("/sdcard/a/../../b.bin");
    expect_reject("a/../b/../c");
    expect_reject("/etc/passwd");
    expect_reject("/sdcard/subdir/../..//escape");
    expect_reject(""); // empty
    expect_reject("/sdcard/"); // directory (trailing slash)
    // path com componente ".." deve ser rejeitado mesmo se normalizado aparentemente dentro
    expect_reject("/sdcard/a/b/../../../etc/passwd");
    // NUL dentro do path deve ser rejeitado (control char)
    {
        std::string bad_path = std::string("/sdcard/a")+char(0)+".bin";
        std::string b64 = b64_canonical("x");
        // construir JSON manualmente com NUL no path: parse deve rejeitar invalid_json/invalid_utf8
        std::string line = std::string("{\"rid\":\"r-nul\",\"type\":\"fs.write\",\"path\":\"")+bad_path+std::string("\",\"data_b64\":\"")+b64+"\",\"size\":1}";
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
        CHECK(!cyberdeck_serial::parse_ndjson_line(line.data(), line.size(), req, err));
    }
}

void test_size_and_base64_limits(){
    // 2048 bytes deve ser aceito; 2049 deve ser rejeitado
    std::string max_data(2048,'A');
    std::string max_b64 = b64_canonical(max_data);
    {
        std::string line = make_fs_write_req("r-max","/sdcard/max.bin", max_b64, 2048);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        // Antes da impl, este ainda falha como unknown_type; apos, deve ser ok (ou pelo menos nao unknown_type)
        // Para manter RED antes, checamos que nao seja unknown_type e que envelope seja NDJSON valido
        CHECK(res.envelope_json.find("\"rid\":\"r-max\"")!=std::string::npos);
        // Se implementado corretamente, deve ser ok; se nao, falha -> RED
        CHECK(res.ok);
        if(res.ok){
            CHECK(res.envelope_json.find("\"ok\":true")!=std::string::npos);
        }
    }
    {
        std::string over_data(2049,'B');
        std::string over_b64 = b64_canonical(over_data);
        std::string line = make_fs_write_req("r-over","/sdcard/over.bin", over_b64, 2049);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.envelope_json.find("\"ok\":false")!=std::string::npos);
    }
    // size mismatch: campo size != decoded len
    {
        std::string b64 = b64_canonical("hello"); // 5
        std::string line = make_fs_write_req("r-mismatch","/sdcard/a.bin", b64, 6);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
    }
    {
        std::string b64 = b64_canonical("hello");
        std::string line = make_fs_write_req("r-mismatch2","/sdcard/a.bin", b64, 4);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
    }
    // data_b64 muito grande decodificado mas size pequeno: deve rejeitar
    {
        std::string b64 = b64_canonical(std::string(100,'x'));
        std::string line = make_fs_write_req("r-mismatch3","/sdcard/a.bin", b64, 1);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
    }
}

void test_strict_canonical_base64(){
    auto expect_invalid_b64 = [](const std::string &bad_b64){
        std::string line = make_fs_write_req("r-b64","/sdcard/a.bin", bad_b64, 3);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        CHECK(!res.ok);
        CHECK(res.envelope_json.find("\"ok\":false")!=std::string::npos);
    };
    // sem padding quando necessario
    expect_invalid_b64("YWI"); // "ab" sem "="
    // padding extra nao canonico
    expect_invalid_b64("YWI=="); // "ab" com "==" nao canonico (deveria ser "YWI=")
    // caracteres invalidos
    expect_invalid_b64("YWI*");
    expect_invalid_b64("Y W I=");
    // whitespace
    expect_invalid_b64(" YWI=");
    expect_invalid_b64("YWI= ");
    // nao canonico: "YWJj" vs "YWJj==" etc. "abc" canonico é "YWJj", nao "YWJj=="
    expect_invalid_b64("YWJj=="); // extra padding
    // tamanho nao multiplo de 4 sem padding correto
    expect_invalid_b64("YWJjYQ"); // 6 chars, invalid
    // valido canonico deve ser aceito (se path/size tambem validos)
    {
        std::string good = b64_canonical("ab"); // "YWI="
        std::string line = make_fs_write_req("r-good","/sdcard/good.bin", good, 2);
        auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
        // Antes da impl, falha; apos, deve ser ok
        CHECK(res.ok);
        CHECK(res.envelope_json.find("\"ok\":true")!=std::string::npos);
    }
    {
        std::string good2 = b64_canonical("abc"); // "YWJj"
        std::string line2 = make_fs_write_req("r-good2","/sdcard/good2.bin", good2, 3);
        auto res2 = cyberdeck_serial::dispatch_one(line2.data(), line2.size());
        CHECK(res2.ok);
    }
    {
        std::string empty_b64 = b64_canonical(""); // ""
        std::string line3 = make_fs_write_req("r-empty","/sdcard/empty.bin", empty_b64, 0);
        auto res3 = cyberdeck_serial::dispatch_one(line3.data(), line3.size());
        // empty file (0 bytes) deve ser permitido ou pelo menos nao crashar; se rejeitar, deve ser erro controlado
        CHECK(res3.envelope_json.find("\"rid\":\"r-empty\"")!=std::string::npos);
        CHECK(res3.envelope_json.find('\n')==std::string::npos);
    }
}

void test_ndjson_error_envelope_and_rid_echo(){
    // linha >4096 deve ser too_large com envelope valido
    {
        std::string big(4097,'x');
        auto r = cyberdeck_serial::dispatch_one(big.data(), big.size());
        CHECK(!r.ok);
        CHECK(r.envelope_json.find("\"ok\":false")!=std::string::npos);
        CHECK(r.envelope_json.find('\n')==std::string::npos);
    }
    // JSON invalido deve retornar envelope com rid sintetizado ou vazio mas valido NDJSON
    {
        std::string bad="not json";
        auto r = cyberdeck_serial::dispatch_one(bad.data(), bad.size());
        CHECK(!r.ok);
        CHECK(r.envelope_json.front()=='{' && r.envelope_json.back()=='}');
        CHECK(r.envelope_json.find("\"ok\":false")!=std::string::npos);
    }
    // fs.write com campo size como string deve ser rejeitado (tipo invalido) mas com envelope rid ecoado
    {
        std::string s="{\"rid\":\"r-strsize\",\"type\":\"fs.write\",\"path\":\"/sdcard/a.bin\",\"data_b64\":\"YWI=\",\"size\":\"2\"}";
        auto r = cyberdeck_serial::dispatch_one(s.data(), s.size());
        CHECK(!r.ok);
        CHECK(r.envelope_json.find("\"rid\":\"r-strsize\"")!=std::string::npos);
    }
    // fs.write com data_b64 como numero deve ser rejeitado
    {
        std::string s="{\"rid\":\"r-numb64\",\"type\":\"fs.write\",\"path\":\"/sdcard/a.bin\",\"data_b64\":123,\"size\":2}";
        auto r = cyberdeck_serial::dispatch_one(s.data(), s.size());
        CHECK(!r.ok);
        CHECK(r.envelope_json.find("\"rid\":\"r-numb64\"")!=std::string::npos);
    }
}

void test_preserve_existing_serial_contracts(){
    // ping ainda deve funcionar
    {
        const char* s="{\"rid\":\"r-ping\",\"type\":\"ping\"}";
        auto r = cyberdeck_serial::dispatch_one(s, std::strlen(s));
        CHECK(r.envelope_json.find("\"rid\":\"r-ping\"")!=std::string::npos);
        CHECK(r.envelope_json.find("\"ok\"")!=std::string::npos);
        CHECK(r.envelope_json.find('\n')==std::string::npos);
    }
    // ui.echo ainda deve ser conhecido
    {
        const char* s="{\"rid\":\"r-echo\",\"type\":\"ui.echo\",\"text\":\"hi\"}";
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
        CHECK(cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err==cyberdeck_serial::dispatch_error::none);
        CHECK(req.type=="ui.echo");
    }
    // sys.info ainda deve ser conhecido
    {
        const char* s="{\"rid\":\"r-sys\",\"type\":\"sys.info\"}";
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
        CHECK(cyberdeck_serial::parse_ndjson_line(s, std::strlen(s), req, err));
        CHECK(err==cyberdeck_serial::dispatch_error::none);
    }
    // cat contrato preservado: local shell cat ainda deve existir via shell (nao via serial, mas garantir que serial nao quebrou)
    // Verifica que k_max_ndjson_line e k_max_rid_len ainda sao os valores fixados
    CHECK(cyberdeck_serial::k_max_ndjson_line==4096);
    CHECK(cyberdeck_serial::k_max_rid_len==64);
    CHECK(cyberdeck_serial::k_screen_chunk_bytes==1024);
}

void test_response_contains_crc_and_size(){
    std::string data="hello world";
    std::string b64 = b64_canonical(data);
    std::string line = make_fs_write_req("r-crc","/sdcard/crc.bin", b64, (int)data.size());
    auto res = cyberdeck_serial::dispatch_one(line.data(), line.size());
    // Antes da impl, res.ok sera false; apos, deve ser true e conter size/crc
    CHECK(res.envelope_json.find("\"rid\":\"r-crc\"")!=std::string::npos);
    if(res.ok){
        CHECK(res.envelope_json.find("\"ok\":true")!=std::string::npos);
        // espera-se que result contenha size e crc32 ou bytes_written
        bool has_size = res.envelope_json.find("size")!=std::string::npos || res.envelope_json.find("bytes")!=std::string::npos;
        bool has_crc = res.envelope_json.find("crc")!=std::string::npos || res.envelope_json.find("crc32")!=std::string::npos;
        CHECK(has_size);
        CHECK(has_crc);
        CHECK(res.envelope_json.find('\n')==std::string::npos);
    } else {
        // Se ainda nao implementado, este teste marca falha para indicar RED
        CHECK(false);
    }
}

} // namespace

int main(){
    /* Keep first so the collision regression starts at the first temp name. */
    test_preexisting_temp_candidate_preserved();
    test_known_type_and_constants();
    test_missing_fields_rejected();
    test_path_traversal_rejected();
    test_size_and_base64_limits();
    test_strict_canonical_base64();
    test_ndjson_error_envelope_and_rid_echo();
    test_preserve_existing_serial_contracts();
    test_response_contains_crc_and_size();
    if(s_failures==0){ std::printf("PASS: fs_write_dispatch (%d checks)\n", s_checks); return 0; }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks); return 1;
}
