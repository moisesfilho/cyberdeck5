#pragma once

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

/* Adapter injetavel. inspect() deve falhar fechado em caso de erro de I/O. */
class file_ops {
public:
    virtual ~file_ops() = default;

    virtual artifact_state inspect(std::string_view path) = 0;
    // Called by the worker before inspecting/creating sidecars.  The default
    // keeps older injected adapters source-compatible; the firmware adapter
    // overrides it to create the confined save directory.
    virtual void ensure_directory(std::string_view path) { (void)path; }
    virtual int open_exclusive(std::string_view path) = 0;
    virtual std::ptrdiff_t write(int fd, std::string_view data) = 0;
    virtual int fsync(int fd) = 0;
    virtual int close(int fd) = 0;
    virtual int rename(std::string_view from, std::string_view to) = 0;
    virtual int unlink(std::string_view path) = 0;
};

/* Sink de completion/ACK. false retém o resultado para drain(). */
class completion_sink {
public:
    virtual ~completion_sink() = default;
    virtual bool publish(const completion &value) = 0;
};

/**
 * Coordinator de persistencia com uma unica vaga bounded.  enqueue() copia
 * o request e nunca chama file_ops; pump_one() executa no maximo uma
 * transacao.  Um publish rejeitado mantem a vaga e o guard ate drain().
 */
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

    /* Retorna true quando um item foi retirado, mesmo em caso de falha. */
    bool pump_one();

    /* Entrega uma completion retida por publish()==false. */
    bool drain(completion &out);
    // These lifecycle and hand-off methods are safe to call concurrently;
    // teardown is terminal and invalidates the single slot before waiting for
    // an in-flight pump.
    bool in_flight() const;
    std::size_t pending() const;

private:
    struct implementation;
    implementation *impl_;
};

} // namespace cyberdeck_wifi_audit_persistence
