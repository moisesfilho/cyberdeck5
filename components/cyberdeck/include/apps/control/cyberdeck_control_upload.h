#pragma once

#include "apps/control/cyberdeck_control_protocol.h"
#include "apps/image/cyberdeck_image_bmp.h"
#include "apps/image/cyberdeck_image_list.h"

#include <cstddef>
#include <cstdint>
#include <string>

/*
 * POST /control/upload - transporte autenticado de UM arquivo BMP para o SD.
 *
 * Motivo: `fs.write` da ponte USB limita o payload a 2048 bytes e nao serve
 * para uma fixture full-res (1280x720x24 + 54 = 2.764.854 B). A rota reusa a
 * chave dedicada do canal de controle e o nonce de uso unico, com mensagem
 * propria (`build_upload_message`), e nunca a chave OTA.
 *
 * Regras do recorte aprovado:
 *  - destino fixo `/sdcard/screenshots` (visivel como `/screenshots/<nome>`);
 *    o cliente so escolhe um BASENAME `.bmp`, nunca um diretorio;
 *  - o basename validado entra na mensagem autenticada
 *    (`build_upload_message`/`verify_upload`, sete campos): trocar o nome
 *    invalida o MAC antes de qualquer abertura de arquivo;
 *  - corpo `<= k_max_upload_bytes`, lido em chunks de tamanho constante;
 *  - o digest declarado e reconferido incrementalmente;
 *  - publicacao sem sobrescrita: temporario exclusivo no mesmo diretorio,
 *    `fsync`+`close` e entao publicacao (no host, `link`+`unlink`; no FATFS o
 *    `rename` so acontece depois de o alvo ser observado ausente, mesma
 *    limitacao documentada de `fs.write`);
 *  - em qualquer falha, apenas o temporario possuido por esta transacao e
 *    removido.
 *
 * Toda a logica abaixo e pura: a criptografia (SHA-256/HMAC) e o acesso a
 * arquivos entram por funcoes injetadas, entao o host exercita o mesmo codigo.
 */
namespace cyberdeck_control {

/* Teto do corpo. Fica abaixo do limite do visualizador para que qualquer
 * arquivo aceito aqui seja listavel por `image` em `/screenshots`. */
inline constexpr std::size_t k_max_upload_bytes = 3u * 1024u * 1024u;
static_assert(k_max_upload_bytes < cyberdeck_image::k_max_file_bytes, "an upload must stay inside the viewer file cap");

/* Buffer de leitura fixo: nenhuma alocacao depende do tamanho declarado. */
inline constexpr std::size_t k_upload_chunk_bytes = 4096;

/* Defesa secundaria do stream: timeouts de recv SEM progresso entre eles
 * abortam. O contador zera a cada chunk positivo; o prazo absoluto acima e a
 * defesa primaria contra progresso lento. */
inline constexpr unsigned k_upload_max_consecutive_timeouts = 3;

/* Duracao total maxima de UMA transferencia. E um orcamento absoluto por
 * request: progresso (bytes parciais) NAO renova o prazo, entao um peer que
 * goteja bytes antes de cada janela de recv nao mantem a task HTTP, o lease
 * WRITE e o temporario ocupados indefinidamente. 120 s correspondem a
 * >= 26 KiB/s para o teto de 3 MiB (a fixture de 2.764.854 B exige
 * >= 23 KiB/s), ordens de grandeza abaixo de qualquer LAN/Wi-Fi aceitavel.
 * Nao depende de `declared_size`. */
inline constexpr std::int64_t k_max_upload_duration_us = 120'000'000;
static_assert(k_max_upload_duration_us > 0, "the upload deadline must be a positive monotonic budget");

/* Mesmo limite de nome da listagem do visualizador: um nome aceito aqui nunca
 * e descartado por comprimento na pagina do viewer, e todo nome aceito cabe na
 * mensagem autenticada. */
inline constexpr std::size_t k_max_upload_name_bytes = cyberdeck_image::k_max_name_bytes;
static_assert(k_max_upload_name_bytes <= k_upload_name_capacity,
              "every accepted basename must fit the authenticated upload message");
inline constexpr std::size_t k_max_upload_digest_chars = 2 * 32;
inline constexpr std::size_t k_max_upload_json_bytes = 320;

inline constexpr char k_upload_directory[] = "/sdcard/screenshots";
inline constexpr char k_upload_viewer_directory[] = "/screenshots";

enum class upload_status : std::uint8_t {
    ok = 0,
    empty,
    too_large,
    invalid_name,
    conflict,
    checksum,
    truncated,
    timeout,
    io_error,
    internal,
};

struct upload_digest_ops {
    void *context = nullptr;
    bool (*start)(void *context) = nullptr;
    bool (*update)(void *context, const std::uint8_t *data, std::size_t length) = nullptr;
    bool (*finish)(void *context, std::uint8_t *out) = nullptr;
};

/* Proximo chunk do corpo: >0 bytes, 0 fim do corpo, -1 timeout de recv (o
 * modulo puro conta os consecutivos), -2 erro de socket. */
using upload_read_fn = int (*)(void *context, std::uint8_t *buffer, std::size_t capacity);

/* Fonte de tempo do upload. A producao injeta `esp_timer_get_time()`; o host
 * injeta um relogio falso determinista. Unidade: microssegundos monotonos. */
struct upload_clock {
    void *context = nullptr;
    std::int64_t (*now_us)(void *context) = nullptr;
};

inline constexpr int k_upload_publish_ok = 0;
inline constexpr int k_upload_publish_exists = 1;
inline constexpr int k_upload_publish_failed = 2;

struct upload_file_sink {
    void *context = nullptr;
    /* O alvo ja existe (arquivo, diretorio ou symlink)? Consulta antes de
     * criar o temporario para nao gastar a transferencia. */
    bool (*target_exists)(void *context, const char *name) = nullptr;
    /* Cria um temporario unico no diretorio e devolve o basename em `out`. */
    bool (*open_temp)(void *context, char *out, std::size_t capacity) = nullptr;
    bool (*write)(void *context, const std::uint8_t *data, std::size_t length) = nullptr;
    /* `fsync` + `close`, conferindo o tamanho gravado. */
    bool (*close_temp)(void *context, std::uint64_t expected_bytes) = nullptr;
    int (*publish)(void *context, const char *temp_name, const char *target_name) = nullptr;
    /* Remove SOMENTE o temporario que esta transacao criou. */
    void (*discard)(void *context, const char *temp_name) = nullptr;
};

struct upload_outcome {
    std::uint64_t bytes = 0;
    std::uint8_t digest[32]{};
};

/* Nome: basename ASCII imprimivel `[A-Za-z0-9._-]`, sem `..`, sem comecar por
 * `.`, extensao `.bmp` case-insensitive e comprimento 5..k_max_upload_name_bytes.
 * Ele sempre passa no filtro de extensao do visualizador. */
bool validate_upload_name(const char *name, std::size_t length);

/* Decimal estrito sem sinal/zero inicial, com teto de overflow em uint64. */
bool parse_upload_size(const char *text, std::size_t length, std::uint64_t *out);

/* Executa a transacao completa (validacao, stream, digest, fsync, publicacao)
 * e limpa o temporario em toda falha. `scratch` e um buffer fixo do chamador.
 * `clock` e consultado no inicio e no maximo uma vez por iteracao do laco; o
 * prazo `k_max_upload_duration_us` e absoluto por request e nao e renovado por
 * progresso. */
upload_status receive_upload(const upload_file_sink &files, const upload_digest_ops &digest, upload_read_fn read,
                             void *read_context, const upload_clock &clock, const char *name,
                             std::uint64_t declared_size, const std::uint8_t *expected_digest, std::uint8_t *scratch,
                             std::size_t scratch_capacity, upload_outcome *outcome);

/* JSON deterministico: {"name","bytes","sha256","path"}. O nome ja e validado,
 * logo nao ha aspas/controle para escapar. */
bool format_upload_json(char *out, std::size_t capacity, const char *name, std::uint64_t bytes,
                        const std::uint8_t *digest);

/* Sink POSIX/VFS limitado a um diretorio. Temporarios usam o prefixo oculto
 * `.upload.<sequencia>.tmp`, criado com O_CREAT|O_EXCL (nunca reaproveitado) e
 * removido apenas quando pertence a esta instancia. */
struct upload_files {
    upload_files() = default;
    upload_files(const upload_files &) = delete;
    upload_files &operator=(const upload_files &) = delete;
    ~upload_files();

    std::string directory;
    std::string temp_name;
    std::uint32_t sequence = 0;
    int descriptor = -1;
    bool owns_temp = false;
    std::uint64_t written = 0;
};

bool upload_files_open(upload_files &files, const char *directory);
upload_file_sink upload_files_sink(upload_files &files);
void upload_files_reset(upload_files &files);

} // namespace cyberdeck_control
