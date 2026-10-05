#include "cJSON.h"
#include <cstring>
#include <string>

static char *value(const std::string &s, const char *key) {
    const std::string marker = std::string("\"") + key + "\":\"";
    const auto begin = s.find(marker); if (begin == std::string::npos) return nullptr;
    const auto start = begin + marker.size(); const auto end = s.find('"', start);
    if (end == std::string::npos) return nullptr;
    const std::string out=s.substr(start,end-start); char *p=new char[out.size()+1]; std::memcpy(p,out.c_str(),out.size()+1); return p;
}
static bool number(const std::string &s, const char *key, int &out) {
    const std::string marker = std::string("\"") + key + "\":";
    const auto begin = s.find(marker); if (begin == std::string::npos) return false;
    const auto start = begin + marker.size(); size_t end = start;
    while (end < s.size() && (s[end] == '-' || (s[end] >= '0' && s[end] <= '9'))) ++end;
    if (end == start) return false;
    try { out = std::stoi(s.substr(start, end - start)); return true; } catch (...) { return false; }
}
cJSON *cJSON_ParseWithLength(const char *text, size_t n) {
    std::string s(text,n); if (s.empty() || s.front()!='{' || s.back()!='}') return nullptr;
    auto *r=new cJSON{}; r->object=1; r->timezone=value(s,"timezone"); r->time_zone=value(s,"timeZone"); r->utc_datetime=value(s,"utc_datetime");
    const char *keys[] = {"year", "month", "day", "hour", "minute", "seconds"};
    for (int i = 0; i < 6; ++i) {
        int parsed = 0;
        if (number(s, keys[i], parsed)) r->numeric[i] = parsed;
    }
    return r;
}
int cJSON_IsObject(const cJSON *x) { return x && x->object; }
int cJSON_IsString(const cJSON *x) { return x && x->string; }
int cJSON_IsNumber(const cJSON *x) { return x && x->number; }
const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *r,const char *key) {
    static thread_local cJSON x[9];
    int i = 0; const char *text = nullptr;
    if (std::strcmp(key, "timezone") == 0) { i = 0; text = r->timezone; }
    else if (std::strcmp(key, "timeZone") == 0) { i = 1; text = r->time_zone; }
    else if (std::strcmp(key, "utc_datetime") == 0) { i = 2; text = r->utc_datetime; }
    else { const char *keys[] = {"year", "month", "day", "hour", "minute", "seconds"}; for (int j = 0; j < 6; ++j) if (std::strcmp(key, keys[j]) == 0) { i = 3 + j; if (!r->numeric[j]) return nullptr; x[i] = {}; x[i].number = 1; x[i].valuedouble = x[i].valueint = r->numeric[j]; return &x[i]; } return nullptr; }
    if (!text) return nullptr;
    x[i] = {};
    x[i].string = 1;
    x[i].valuestring = const_cast<char *>(text);
    return &x[i];
}
void cJSON_Delete(cJSON *x) { if(x){ delete[] x->timezone; delete[] x->time_zone; delete[] x->utc_datetime; delete x; } }
