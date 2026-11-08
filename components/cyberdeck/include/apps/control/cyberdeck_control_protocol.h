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
 *   POST /control/upload     -> transporte binario autenticado (BMP) para o SD
 *
 * Cabecalhos das rotas autenticadas: `X-CTL-Nonce` (32 hex) e `X-CTL-MAC`
 * (64 hex). A mensagem autenticada de texto/terminal e
 *
 *   "<nonce>\n<METHOD>\n<uri>\n<sha256(body) hex>"
 *
 * O digest vincula o corpo exato (nao apenas o tamanho) e o nonce impede
 * replay; o mesmo nonce nunca autoriza duas requisicoes. HTTP nao oferece
 * confidencialidade: o HMAC autentica a origem logica, nao cifra o texto.
 *
 * O upload tem uma mensagem PROPRIA, com separacao de dominio, porque o corpo
 * nao pode ser mantido em RAM para calcular o digest antes da autorizacao:
 *
 *   "cyberdeck-control-upload/v1\n<nonce>\n<METHOD>\n<uri>\n<nome>\n<tamanho>\n<sha256>"
 *
 * O cliente calcula o SHA-256 do arquivo local e envia nome, tamanho e digest
 * nos cabecalhos; o dispositivo autentica esses metadados (inclusive o nome que
 * sera publicado) antes de abrir qualquer arquivo e reconfere o digest
 * incrementalmente enquanto recebe o corpo.
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

/* Rota, metodo e dominio da mensagem de upload.  O prefixo de dominio garante
 * que um MAC calculado para `/control/type` (4 campos) nunca valide um upload,
 * mesmo com nonce, URI, tamanho e digest identicos. */
constexpr char k_upload_method[] = "POST";
constexpr char k_upload_uri[] = "/control/upload";
constexpr char k_upload_domain[] = "cyberdeck-control-upload/v1";

/* Teto do basename que a mensagem aceita.  O validador do upload deriva o
 * proprio limite do visualizador e o `static_assert` em
 * `cyberdeck_control_upload.h` garante que todo nome aceito caiba aqui. */
constexpr std::size_t k_upload_name_capacity = 128;
/* Maior tamanho decimal de um uint64_t (18446744073709551615). */
constexpr std::size_t k_upload_size_digits = 20;

/* Capacidade da mensagem calculada a partir dos campos reais:
 * domain \n nonce \n method \n uri \n name \n size \n digest + NUL. */
constexpr std::size_t k_upload_message_bytes =
    (sizeof(k_upload_domain) - 1) + 1 + k_nonce_hex_chars + 1 +
    (sizeof(k_upload_method) - 1) + 1 + (sizeof(k_upload_uri) - 1) + 1 +
    k_upload_name_capacity + 1 + k_upload_size_digits + 1 + k_mac_hex_chars;
constexpr std::size_t k_max_upload_message_bytes = k_upload_message_bytes + 1;
static_assert(k_upload_name_capacity >= 5, "the upload name capacity must allow a shortest .bmp");
static_assert(k_upload_message_bytes > 27 + k_nonce_hex_chars + 4 + 15 + 5 + 20 + k_mac_hex_chars,
              "the upload MAC message must fit every field");

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
    invalid_request,
    invalid_path,
    conflict,
    checksum,
    storage_unavailable,
    io_error,
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

/* Mensagem do upload: dominio fixo + metodo + URI + nome do alvo + tamanho
 * declarado + digest. `name` precisa ser exatamente o basename validado que
 * sera publicado. `out` precisa comportar `k_max_upload_message_bytes`. */
bool build_upload_message(const char *nonce_hex, const char *method, const char *uri,
                          const char *name, std::uint64_t declared_size,
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

/*
 * Verificacao do upload. O digest do corpo e informado pelo cliente (hex
 * minusculo canonico de 64 caracteres) porque o corpo e transmitido depois da
 * autorizacao; o dispositivo reconfere o digest do stream. `name` e o basename
 * validado que sera publicado: ele entra na mensagem autenticada, entao trocar
 * o nome invalida o MAC. As mesmas regras de nonce valem aqui: MAC invalido nao
 * queima o desafio e um MAC valido sempre o consome.
 */
error verify_upload(nonce_store &store, const std::uint8_t *key, std::size_t key_length,
                    const char *nonce_hex, const char *mac_hex, const char *name,
                    std::uint64_t declared_size, const char *body_digest_hex,
                    std::int64_t now_us, mac_fn mac);

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
