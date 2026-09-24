#pragma once

/*
 * Contrato host-only para a camada injetavel de persistencia da auditoria
 * Wi-Fi.  Este header e uma declaracao de interface, nao uma implementacao.
 * O coder deve criar o header publico e o fonte de producao com os mesmos
 * simbolos antes de ligar este teste.
 *
 * O contrato e deliberadamente pequeno e sem ESP-IDF/FreeRTOS:
 *
 *  - file_ops injeta open-exclusive, write (inclusive escrita parcial),
 *    fsync, close, rename, unlink e inspecao de artefatos;
 *  - audit_persistence possui uma fila bounded de uma vaga. A vaga funciona
 *    como single-shot: uma segunda solicitacao enquanto a vaga esta ocupada
 *    retorna submit_status::rejected_queue_full e nao executa I/O;
 *  - enqueue copia token/path/payload e nao chama file_ops;
 *  - pump_one executa no maximo uma transacao.  A publicacao so pode ser
 *    reportada depois de write -> fsync -> close -> rename bem-sucedidos;
 *  - completion_sink::publish recebe o resultado. Se ele retorna false, a
 *    transacao ja publicada nao pode ser repetida: o resultado fica em uma
 *    vaga bounded e drain() entrega esse ACK uma unica vez;
 *  - drain() tambem libera o single-shot para resultados de sucesso e falha;
 *  - teardown() descarta a vaga e torna o objeto inerte.
 *
 * A semantica de arquivos e a mesma aprovada para FatFs: destino em
 * /sdcard/wifi-audit.txt, candidato estavel /sdcard/.wifi-audit.tmp e backup
 * /sdcard/.wifi-audit.bak.  Em caso de publicacao com destino existente, o
 * destino antigo e movido para .bak antes do candidato.  Se o rollback do .bak
 * falhar, .tmp e .bak sao preservados e o resultado deve falhar; nunca se
 * descarta a unica copia recuperavel.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cyberdeck_wifi_audit_persistence {

inline constexpr std::size_t max_payload = 2048;
inline constexpr std::size_t queue_capacity = 1;

enum class artifact_state { missing, regular, other, error };

enum class persistence_error {
    none,
    invalid_request,
    open_failed,
    write_failed,
    fsync_failed,
    close_failed,
    rename_failed,
    recovery_failed,
    rollback_failed
};

enum class submit_status {
    accepted,
    rejected_invalid,
    rejected_not_initialized,
    rejected_queue_full
};

struct completion {
    bool ok{false};
    std::uint64_t token{0};
    std::string path{};
    std::string data{};
    std::size_t bytes{0};
    persistence_error error{persistence_error::none};
};

/* Adapter injetavel. Implementacoes de host e firmware devem honorar a
 * semantica de retorno: 0/size maior que zero indica sucesso; -1 (ou zero
 * para write) indica falha. inspect deve falhar fechado para erros de I/O. */
class file_ops {
public:
    virtual ~file_ops() = default;

    virtual artifact_state inspect(std::string_view path) = 0;
    virtual int open_exclusive(std::string_view path) = 0;
    virtual std::ptrdiff_t write(int fd, std::string_view data) = 0;
    virtual int fsync(int fd) = 0;
    virtual int close(int fd) = 0;
    virtual int rename(std::string_view from, std::string_view to) = 0;
    virtual int unlink(std::string_view path) = 0;
};

/* Sink de ACK/completion. false significa que a entrega falhou, mas o
 * resultado deve permanecer limitado e ser disponivel para drain(). */
class completion_sink {
public:
    virtual ~completion_sink() = default;
    virtual bool publish(const completion &value) = 0;
};

/* Worker/coordenador injetavel, executado passo a passo no host. */
class audit_persistence {
public:
    audit_persistence(file_ops &operations, completion_sink &sink);
    ~audit_persistence();
    audit_persistence(const audit_persistence &) = delete;
    audit_persistence &operator=(const audit_persistence &) = delete;

    bool initialize();
    void teardown();
    bool initialized() const;

    submit_status enqueue(std::uint64_t token,
                          std::string_view path,
                          std::string_view data);
    /* Consome no maximo um item. Retorna true quando um item foi retirado,
     * mesmo que a transacao tenha resultado em falha. */
    bool pump_one();
    /* Entrega uma completion retida por publish()==false. */
    bool drain(completion &out);
    bool in_flight() const;
    std::size_t pending() const;

private:
    struct implementation;
    implementation *impl_;
};

} // namespace cyberdeck_wifi_audit_persistence
