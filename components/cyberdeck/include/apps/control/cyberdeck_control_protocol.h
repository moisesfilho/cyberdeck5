#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Canal HTTP de comandos na LAN - camada PURA (sem ESP-IDF, FreeRTOS ou LVGL).
 *
 * O canal reproduz o efeito de `ui.type` da ponte USB Serial-JTAG sem ocupar a
 * porta serial, para que o operador possa manter o `idf.py monitor` aberto. A
 * autenticacao e dedicada (chave propria, nunca a chave OTA) e cada requisicao
 * usa um nonce de uso unico:
 *
 *   GET  /control/challenge  -> {"nonce":"<32 hex>","ttl_ms":60000}
 *   POST /control/type       -> injeta texto + Enter na UI ativa
 *   GET  /control/terminal   -> snapshot textual limitado do terminal
 *
 * Cabecalhos das rotas autenticadas: `X-CTL-Nonce` (32 hex) e `X-CTL-MAC`
 * (64 hex). A mensagem autenticada e
 *
 *   "<nonce>\n<METHOD>\n<uri>\n<sha256(body) hex>"
 *
 * O digest vincula o corpo exato (nao apenas o tamanho) e o nonce impede
 * replay; o mesmo nonce nunca autoriza duas requisicoes. HTTP nao oferece
 * confidencialidade: o HMAC autentica a origem logica, nao cifra o texto.
 */
namespace cyberdeck_control {

constexpr std::size_t k_key_bytes = 32;
constexpr std::size_t k_key_hex_chars = 2 * k_key_bytes;
constexpr std::size_t k_nonce_bytes = 16;
constexpr std::size_t k_nonce_hex_chars = 2 * k_nonce_bytes;
constexpr std::size_t k_mac_bytes = 32;
constexpr std::size_t k_mac_hex_chars = 2 * k_mac_bytes;
/* Teto por requisicao de /control/type (bytes UTF-8 antes da normalizacao). */
constexpr std::size_t k_max_text_bytes = 1024;
/* Teto de linhas para limitar a injecao na UI (cada linha gera um Enter). */
constexpr std::size_t k_max_lines = 32;
constexpr std::int64_t k_nonce_ttl_us = 60'000'000;
static_assert(k_nonce_ttl_us == 60 * 1000 * 1000, "nonce TTL is documented as 60 s");
constexpr std::size_t k_max_mac_message_bytes = 200;

struct segment {
    const char *data;
    std::size_t size;
};

/* Plano de injecao: cada linha recebe um Enter depois do texto. */
struct text_plan {
    segment lines[k_max_lines]{};
    std::size_t count = 0;
};

enum class error {
    none = 0,
    not_ready,
    no_challenge,
    expired,
    unauthorized,
    too_large,
    empty,
    invalid_utf8,
    too_many_lines,
    busy,
    screen_off,
    display_busy,
    internal,
};

const char *error_code(error value);
int http_status(error value);

/* Hex canonico minusculo (rejeita maiusculas, comprimento impar e lixo). */
bool hex_encode(const std::uint8_t *bytes, std::size_t length, char *out, std::size_t capacity);
bool hex_decode(const char *text, std::size_t length, std::uint8_t *out, std::size_t out_length);

/* Compara sem curto-circuito observavel para MACs de tamanho fixo. */
bool constant_time_equal(const std::uint8_t *a, const std::uint8_t *b, std::size_t length);

class nonce_store {
  public:
    void reset();
    bool issue(const std::uint8_t *random, std::size_t length, std::int64_t now_us, char *hex,
               std::size_t capacity);
    /* Nao consome: valida o nonce pendente e sinaliza expiracao. */
    bool pending(const char *hex, std::int64_t now_us, bool *expired) const;
    bool consume(const char *hex, std::int64_t now_us);
    bool outstanding() const { return valid_; }

  private:
    std::uint8_t value_[k_nonce_bytes]{};
    std::int64_t deadline_us_ = 0;
    bool valid_ = false;
};

/* Injetadas: a producao usa mbedtls; o host injeta a mesma primitiva testada. */
using digest_fn = bool (*)(const std::uint8_t *data, std::size_t length, std::uint8_t *out);
using mac_fn = bool (*)(const std::uint8_t *key, std::size_t key_length, const std::uint8_t *data,
                        std::size_t length, std::uint8_t *out);

bool build_message(const char *nonce_hex, const char *method, const char *uri,
                   const char *body_digest_hex, char *out, std::size_t capacity);

/*
 * Verifica o nonce pendente, o digest do corpo e o MAC; consome o nonce somente
 * quando o MAC confere. Um MAC invalido NAO queima o nonce (um atacante sem a
 * chave nao pode negar o desafio do operador), mas uma requisicao autenticada
 * sempre o consome, mesmo que o despacho falhe depois.
 */
error verify(nonce_store &store, const std::uint8_t *key, std::size_t key_length,
             const char *nonce_hex, const char *mac_hex, const char *method, const char *uri,
             const std::uint8_t *body, std::size_t body_length, std::int64_t now_us,
             digest_fn digest, mac_fn mac);

bool valid_utf8(const char *text, std::size_t length);

/*
 * Normaliza CRLF/CR para LF no proprio buffer, valida UTF-8 e separa as linhas.
 * O ultimo terminador nao cria linha vazia; uma linha vazia explicita ("\n")
 * continua valida e vira apenas um Enter.
 */
error plan_text(char *text, std::size_t length, std::size_t *normalized_length, text_plan *out);

std::size_t json_escape(const char *text, std::size_t length, char *out, std::size_t capacity);

bool format_terminal_json(char *out, std::size_t capacity, const char *text, std::size_t length,
                          bool truncated);

} // namespace cyberdeck_control
