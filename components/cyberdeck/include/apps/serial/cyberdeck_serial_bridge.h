#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/*
 * Ponte manual USB Serial-JTAG NDJSON (REQ-002/003/005/006/007/008/009) e
 * fs.write seguro (REQ-001..REQ-011).
 *
 * Este header e PURO: nao inclui esp_err.h, FreeRTOS, LVGL ou qualquer
 * dependencia de hardware. A logica host-testavel vive em
 * cyberdeck_serial_bridge.cpp; o lado de dispositivo (task, driver
 * USB Serial-JTAG, hooks de UI/Wi-Fi) fica atras de `#ifdef ESP_PLATFORM`
 * e e iniciado por bridge_start(), chamado por main/app_main.cpp.
 *
 * Contrato NDJSON:
 *   - Entrada limitada a k_max_ndjson_line (4096) bytes por linha; linhas
 *     maiores sao rejeitadas com dispatch_error::too_large.
 *   - Toda resposta e UM objeto JSON de uma linha, SEM '\\n' interno e com o
 *     `rid` da requisicao ecoado.
 *   - Sucesso: {"rid":"...","ok":true,"result":<json>}
 *   - Erro:    {"rid":"...","ok":false,"error":"<detalle>","error_code":"<enum>"}
 *   - fs.write: {"rid","type":"fs.write","path","data_b64","size"} com path
 *     confinado a /sdcard, Base64 estrito canonico de ate k_fs_write_max_bytes
 *     bytes decodificados (NUL-safe), commit seguro por temp unico e limitado
 *     no mesmo diretorio + rename e resposta {"size":<n>,"crc32":<ieee>}. O
 *     temp e criado com O_CREAT|O_EXCL em um numero limitado de tentativas;
 *     somente o nome efetivamente criado por esta invocacao pode ser removido
 *     em falha, nunca um candidato preexistente.
 *     No host POSIX, renameat pode substituir o destino atomicamente; no
 *     ESP/FATFS, um destino existente e rejeitado (io_error) porque o VFS nao
 *     garante replace atomico, e rename nunca e seguido por unlink do destino.
 */
namespace cyberdeck_serial {

/* Limites do protocolo (contrato fixado pelos testes host). */
constexpr std::size_t k_max_ndjson_line = 4096;
constexpr std::size_t k_max_rid_len = 64;
constexpr std::size_t k_screen_chunk_bytes = 1024;
/* Limite de BYTES decodificados por fs.write (REQ-001..REQ-011). */
constexpr std::size_t k_fs_write_max_bytes = 2048;

/* Erros tipados de dispatch. `internal` e aditivo (falha de hardware/UI no
 * dispositivo) e nao fazia parte da enum original; `invalid_path`,
 * `invalid_payload` e `io_error` tambem sao aditivos, criados para os
 * erros tipados de fs.write; os testes preservam os codigos originais. */
enum class dispatch_error {
    none = 0,
    too_large,
    invalid_json,
    missing_rid,
    missing_type,
    unknown_type,
    invalid_utf8,
    internal,
    invalid_path,
    invalid_payload,
    io_error,
};

struct request {
    std::string rid;
    std::string type;
    /* Objeto JSON original (recortado), preservado para extracao de
     * argumentos pelo dispatch (text, x, y, target, symbol...). */
    std::string payload_raw;
};

struct response {
    std::string rid;
    bool ok = false;
    std::string result_json;
    dispatch_error err = dispatch_error::none;
    std::string err_detail;
};

struct dispatch_result {
    bool ok = false;
    std::string envelope_json;
    dispatch_error err = dispatch_error::none;
    std::string rid;
    std::string type;
};

/* Parseia UMA linha NDJSON em uma requisicao. Retorna false preenchendo
 * `err`; `out.rid`/`out.type` podem ja ter sido preenchidos quando o falha
 * ocorre depois deles (ex.: unknown_type), permitindo ecoar o rid no erro. */
bool parse_ndjson_line(const char *data, std::size_t len, request &out, dispatch_error &err);

/* Monta o envelope de uma linha (sucesso ou erro) sem '\\n'. */
std::string build_envelope(const response &r);
std::string build_error_envelope(const std::string &rid, dispatch_error err, const std::string &detail);

/* Parseia + despacha UMA linha. Nunca aloca acima de k_max_ndjson_line para
 * a linha de entrada; o envelope de saida e limitado pelo resultado. */
dispatch_result dispatch_one(const char *line, std::size_t len);

/* ------------------------------------------------------------------ */
/* sys.info / wifi (REQ-008/REQ-009): JSON puro, sem hardware.         */
/* ------------------------------------------------------------------ */

struct SysInfo {
    std::string fw_version;
    std::string idf_version;
    std::string chip;
    int free_heap = 0;
    std::string uptime;
};

struct WifiNet {
    std::string ssid;
    int rssi = 0;
    bool open = false;
};

std::string sys_info_to_json(const SysInfo &info);
bool sys_info_from_json(const std::string &json, SysInfo &out);
std::string wifi_scan_to_json(const std::vector<WifiNet> &nets);
std::string handle_sys_info(const std::string &rid);
std::string handle_wifi_scan(const std::string &rid, const std::vector<WifiNet> &nets);

/* ------------------------------------------------------------------ */
/* screen.dump: BMP 24-bit em chunks com CRC IEEE (REQ-006/REQ-007).  */
/* ------------------------------------------------------------------ */

/* CRC32 IEEE (polinomio 0xEDB88320). crc32(nullptr, 0) == 0. */
std::uint32_t crc32(const std::uint8_t *data, std::size_t len);

/* Paridade exata com screenshot_bmp_calc_size() (reimplementado aqui para o
 * bridge nao depender de um objeto a mais no link dos testes host). */
std::size_t screen_bmp_size(int width, int height);

/* Faixa de linhas [start, end) de um chunk de altura, em passos de
 * k_screen_chunk_bytes/4 linhas (256). API auxiliar: o chunking do dump e
 * por BYTES do BMP, nao por linha. */
bool screen_chunk_bounds(int height, std::uint32_t chunk_index, std::uint32_t *out_start,
                         std::uint32_t *out_end);

/* Prepara a sessao de dump: valida dimensoes/pixel_count, monta o BMP
 * top-down (linha 0 do framebuffer primeiro) e publica total/crc/chunks.
 * Uma falha NAO destrói a sessao ativa anterior. */
bool screen_dump_init(const char *rid, int width, int height, const std::uint16_t *rgb565,
                      std::size_t pixel_count, std::size_t *out_total_bmp_bytes,
                      std::uint32_t *out_total_crc, std::uint32_t *out_total_chunks);
inline bool screen_dump_init(const std::string &rid, int width, int height, const std::uint16_t *rgb565,
                             std::size_t pixel_count, std::size_t *out_total_bmp_bytes,
                             std::uint32_t *out_total_crc, std::uint32_t *out_total_chunks)
{
    return screen_dump_init(rid.c_str(), width, height, rgb565, pixel_count, out_total_bmp_bytes,
                            out_total_crc, out_total_chunks);
}

/* Devolve o slice [idx*1024, ...) da sessao ativa. rid deve casar; indice
 * fora do intervalo rejeita. Ordem de leitura livre (deterministica). */
bool screen_dump_get_chunk(const char *rid, std::uint32_t chunk_index, std::vector<std::uint8_t> &out_bytes,
                           std::uint32_t &out_crc, bool &out_is_last);
inline bool screen_dump_get_chunk(const std::string &rid, std::uint32_t chunk_index,
                                  std::vector<std::uint8_t> &out_bytes, std::uint32_t &out_crc,
                                  bool &out_is_last)
{
    return screen_dump_get_chunk(rid.c_str(), chunk_index, out_bytes, out_crc, out_is_last);
}

/* ------------------------------------------------------------------ */
/* Feeder tolerante a logs/leitura fragmentada (REQ-007/REQ-008).      */
/* ------------------------------------------------------------------ */

/* Monta linhas a partir de bytes arbitrarios (leituras cortadas em qualquer
 * ponto). Linhas vazias sao entregues; o consumidor filtra. `\r` final e
 * removido (CRLF/LF produzem a mesma linha). NUL nao trunca. Linha que
 * exceder k_max_ndjson_line e descartada ate o proximo `\n`. */
class LineAssembler {
public:
    void feed(const char *data, std::size_t len);
    bool next_line(std::string &out);
    void reset();

private:
    std::string m_buf;
    std::deque<std::string> m_lines;
    bool m_discarding = false;
};

/* Linha (recortada) que NAO comeca com '{': log/console, nao-JSON. */
bool is_log_line(const std::string &line);
/* Extrai a linha se ela for um envelope NDJSON (objeto com "rid" e "ok"). */
bool extract_envelope(const std::string &line, std::string &envelope);

/* ------------------------------------------------------------------ */
/* Lado de dispositivo (somente ESP-IDF; ausente no build host).       */
/* ------------------------------------------------------------------ */

#ifdef ESP_PLATFORM
/* Cria o mutex de frames (compartilhado com o writer de log), instala o
 * driver USB Serial-JTAG e inicia a task da ponte. Retorno false em falha. */
bool bridge_start(void);
#endif

} // namespace cyberdeck_serial
