/*
 * Host-side TDD RED contract for sys.info/wifi contratos (REQ-008/REQ-009).
 * Sem hardware real.
 *
 * Contrato esperado (producao ausente -> RED):
 *   Header: components/cyberdeck/include/features/serial/cyberdeck_serial_bridge.h
 *   Tipos:
 *     struct SysInfo { std::string fw_version; std::string idf_version; std::string chip; int free_heap; std::string uptime; };
 *     struct WifiNet { std::string ssid; int rssi; bool open; };
 *
 *   Funcoes puras (sem wifi/esp real, dados injetados ou mock leve host):
 *     std::string sys_info_to_json(const SysInfo&);          // produz JSON valido, campos obrigatorios
 *     bool sys_info_from_json(const std::string& json, SysInfo& out); // parse inverso, rejeita campos faltantes
 *     std::string wifi_scan_to_json(const std::vector<WifiNet>&);
 *     std::string handle_sys_info(const std::string& rid);   // dispatch -> envelope com result contendo SysInfo
 *     std::string handle_wifi_scan(const std::string& rid, const std::vector<WifiNet>& nets);
 *
 *   Invariantes:
 *     - sys.info sempre contem fw_version, idf_version, chip, free_heap
 *     - wifi scan JSON contem array "networks" com ssid/rssi/open, ordenavel
 *     - handlers retornam envelope NDJSON com rid ecoado e ok:true
 */
#include "features/serial/cyberdeck_serial_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int s_failures=0; int s_checks=0;
#define CHECK(c) do{++s_checks; if(!(c)){++s_failures; std::printf("FAIL %s:%d CHECK(%s)\n",__FILE__,__LINE__,#c);} }while(0)

void test_sys_info_json_contract(){
    cyberdeck_serial::SysInfo info;
    info.fw_version="1.2.3";
    info.idf_version="5.5.5";
    info.chip="esp32p4";
    info.free_heap=123456;
    info.uptime="00:01:23";
    std::string j = cyberdeck_serial::sys_info_to_json(info);
    CHECK(j.find("\"fw_version\"")!=std::string::npos);
    CHECK(j.find("1.2.3")!=std::string::npos);
    CHECK(j.find("\"idf_version\"")!=std::string::npos);
    CHECK(j.find("\"chip\"")!=std::string::npos);
    CHECK(j.find("\"free_heap\"")!=std::string::npos);
    CHECK(j.front()=='{' && j.back()=='}');
    CHECK(j.find('\n')==std::string::npos);
    // No secrets should leak in sys.info (password/psk etc must not appear)
    {
        std::string lower=j; for(char& c: lower) c=char(::tolower((unsigned char)c));
        CHECK(lower.find("password")==std::string::npos);
        CHECK(lower.find("psk")==std::string::npos);
    }
    cyberdeck_serial::SysInfo out;
    CHECK(cyberdeck_serial::sys_info_from_json(j, out));
    CHECK(out.fw_version=="1.2.3");
    CHECK(out.idf_version=="5.5.5");
    CHECK(out.chip=="esp32p4");
    CHECK(out.free_heap==123456);
    CHECK(out.uptime=="00:01:23");
    // out must be deterministic across parses
    cyberdeck_serial::SysInfo out2; CHECK(cyberdeck_serial::sys_info_from_json(j, out2));
    CHECK(out2.fw_version==out.fw_version && out2.free_heap==out.free_heap);
    CHECK(!cyberdeck_serial::sys_info_from_json("{\"fw_version\":\"1\"}", out));
    CHECK(!cyberdeck_serial::sys_info_from_json("not json", out));
    CHECK(!cyberdeck_serial::sys_info_from_json("", out));
    CHECK(!cyberdeck_serial::sys_info_from_json("{\"fw_version\":\"1\",\"idf_version\":\"2\",\"chip\":\"x\"}", out)); // missing free_heap
}

void test_wifi_scan_json_contract(){
    std::vector<cyberdeck_serial::WifiNet> nets = {
        {"home", -42, false},
        {"guest", -80, true},
        {"cafe", -60, false},
    };
    std::string j = cyberdeck_serial::wifi_scan_to_json(nets);
    CHECK(j.find("\"networks\"")!=std::string::npos);
    CHECK(j.find("\"home\"")!=std::string::npos);
    CHECK(j.find("\"rssi\"")!=std::string::npos);
    CHECK(j.find("\"open\"")!=std::string::npos);
    CHECK(j.find('\n')==std::string::npos);
    CHECK(j.front()=='{' && j.back()=='}');
    CHECK(j.size() < cyberdeck_serial::k_max_ndjson_line);
    // Exactly one ssid entry per network (no duplication)
    CHECK(j.size() > 10);
    size_t cnt=0; for(size_t p=j.find("\"ssid\""); p!=std::string::npos; p=j.find("\"ssid\"", p+1)) ++cnt;
    CHECK(cnt==3);
    std::string j2 = cyberdeck_serial::wifi_scan_to_json({});
    CHECK(j2.find("\"networks\"")!=std::string::npos);
    CHECK(j2.find('\n')==std::string::npos);
}

void test_handlers_return_envelope_with_rid(){
    {
        auto env1 = cyberdeck_serial::handle_sys_info("rid-sys-1");
        CHECK(env1.find("\"rid\":\"rid-sys-1\"")!=std::string::npos);
        CHECK(env1.find("\"ok\":true")!=std::string::npos);
        CHECK(env1.find("fw_version")!=std::string::npos);
        CHECK(env1.find('\n')==std::string::npos);
        CHECK(env1.front()=='{' && env1.back()=='}');
    }
    {
        auto env2 = cyberdeck_serial::handle_wifi_scan("rid-wifi-2", {{"a",-10,true}});
        CHECK(env2.find("\"rid\":\"rid-wifi-2\"")!=std::string::npos);
        CHECK(env2.find("\"ok\":true")!=std::string::npos);
        CHECK(env2.find("networks")!=std::string::npos);
        CHECK(env2.find('\n')==std::string::npos);
    }
    {
        const char* s1b = "{\"rid\":\"r9\",\"type\":\"sys.info\"}";
        auto r1 = cyberdeck_serial::dispatch_one(s1b, strlen(s1b));
        CHECK(r1.ok); CHECK(r1.envelope_json.find("\"rid\":\"r9\"")!=std::string::npos);
        CHECK(r1.envelope_json.find('\n')==std::string::npos);
    }
    {
        const char* sc = "{\"rid\":\"r10\",\"type\":\"wifi.scan\"}";
        auto r2 = cyberdeck_serial::dispatch_one(sc, strlen(sc));
        CHECK(r2.ok || r2.envelope_json.find("\"rid\":\"r10\"")!=std::string::npos);
        CHECK(r2.envelope_json.find('\n')==std::string::npos);
    }
    {
        cyberdeck_serial::request req; cyberdeck_serial::dispatch_error err;
        const char* s1 = "{\"rid\":\"r11\",\"type\":\"wifi.scan\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(s1, strlen(s1), req, err));
        CHECK(err==cyberdeck_serial::dispatch_error::none);
        const char* s2 = "{\"rid\":\"r12\",\"type\":\"sys.info\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(s2, strlen(s2), req, err));
        CHECK(err==cyberdeck_serial::dispatch_error::none);
        // screen.dump via dispatch must also be known type
        const char* s3 = "{\"rid\":\"r13\",\"type\":\"screen.dump\"}";
        CHECK(cyberdeck_serial::parse_ndjson_line(s3, strlen(s3), req, err));
        CHECK(err==cyberdeck_serial::dispatch_error::none);
    }
}

void test_sys_info_bounded_and_valid_json(){
    cyberdeck_serial::SysInfo big; big.fw_version=std::string(200,'v'); big.idf_version=std::string(200,'i'); big.chip="esp32p4"; big.free_heap=999999; big.uptime="99:99:99";
    std::string j = cyberdeck_serial::sys_info_to_json(big);
    CHECK(j.size() < cyberdeck_serial::k_max_ndjson_line);
    CHECK(j.front()=='{' && j.back()=='}');
}

} // namespace
int main(){
    test_sys_info_json_contract();
    test_wifi_scan_json_contract();
    test_handlers_return_envelope_with_rid();
    test_sys_info_bounded_and_valid_json();
    if(s_failures==0){ std::printf("PASS: serial_sysinfo_wifi (%d checks)\n",s_checks); return 0; }
    std::printf("FAIL: %d de %d checks falharam\n",s_failures,s_checks); return 1;
}
