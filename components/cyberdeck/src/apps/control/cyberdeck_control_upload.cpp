#include "apps/control/cyberdeck_control_upload.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace cyberdeck_control {
namespace {

constexpr std::size_t k_upload_temp_attempts = 8;
constexpr std::size_t k_max_upload_directory_bytes = 160;
constexpr char k_upload_temp_prefix[] = ".upload.";
constexpr char k_upload_temp_suffix[] = ".tmp";

std::atomic<std::uint32_t> s_temp_sequence{0};

bool is_ascii_alnum(char value) {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

char ascii_lower(char value) {
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

std::string join_path(const std::string &directory, const std::string &name) {
    return directory + "/" + name;
}

/* Remove apenas o temporario registrado pela instancia, nunca um caminho
 * arbitrario recebido de fora. Idempotente. */
void discard_owned(upload_files &files) {
    if (files.descriptor >= 0) {
        (void)::close(files.descriptor);
        files.descriptor = -1;
    }
    if (!files.owns_temp)
        return;
    const std::string path = join_path(files.directory, files.temp_name);
    (void)::unlink(path.c_str());
    files.owns_temp = false;
    files.temp_name.clear();
    files.written = 0;
}

bool hook_target_exists(void *context, const char *name) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || name == nullptr)
        return false;
    const std::string path = join_path(files->directory, name);
    struct stat info{};
    /* No host `lstat` nunca segue symlink, entao o alvo e recusado como
     * existente.  FATFS nao possui symlink e o ESP-IDF expoe apenas `stat`. */
#ifdef ESP_PLATFORM
    return ::stat(path.c_str(), &info) == 0;
#else
    return ::lstat(path.c_str(), &info) == 0;
#endif
}

bool hook_open_temp(void *context, char *out, std::size_t capacity) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || out == nullptr || capacity == 0)
        return false;
    for (std::size_t attempt = 0; attempt < k_upload_temp_attempts; ++attempt) {
        const std::uint32_t sequence = files->sequence++;
        char name[48]{};
        const int written = std::snprintf(name, sizeof(name), "%s%lu%s", k_upload_temp_prefix,
                                          static_cast<unsigned long>(sequence), k_upload_temp_suffix);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(name) ||
            static_cast<std::size_t>(written) >= capacity)
            return false;
        const std::string path = join_path(files->directory, name);
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int descriptor = ::open(path.c_str(), flags, 0600);
        if (descriptor >= 0) {
            files->descriptor = descriptor;
            files->temp_name = name;
            files->owns_temp = true;
            files->written = 0;
            std::memcpy(out, name, static_cast<std::size_t>(written) + 1);
            return true;
        }
        /* Nome ocupado: gera o proximo candidato. Nunca remove o alheio. */
        if (errno != EEXIST)
            return false;
    }
    return false;
}

bool hook_write(void *context, const std::uint8_t *data, std::size_t length) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || files->descriptor < 0 || data == nullptr)
        return false;
    std::size_t written = 0;
    while (written < length) {
        const ssize_t chunk = ::write(files->descriptor, data + written, length - written);
        if (chunk < 0 && errno == EINTR)
            continue;
        if (chunk <= 0)
            return false;
        written += static_cast<std::size_t>(chunk);
    }
    files->written += written;
    return true;
}

bool hook_close_temp(void *context, std::uint64_t expected_bytes) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || files->descriptor < 0)
        return false;
    bool ok = ::fsync(files->descriptor) == 0;
    if (ok) {
        struct stat info{};
        ok = ::fstat(files->descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
             static_cast<std::uint64_t>(info.st_size) == expected_bytes && files->written == expected_bytes;
    }
    if (::close(files->descriptor) != 0)
        ok = false;
    files->descriptor = -1;
    return ok;
}

int hook_publish(void *context, const char *temp_name, const char *target_name) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || temp_name == nullptr || target_name == nullptr)
        return k_upload_publish_failed;
    if (!files->owns_temp || files->temp_name != temp_name)
        return k_upload_publish_failed;
    const std::string temp_path = join_path(files->directory, temp_name);
    const std::string target_path = join_path(files->directory, target_name);
#ifdef ESP_PLATFORM
    /* FATFS nao documenta substituicao atomica. Mesmo precedente de `fs.write`:
     * recusa um alvo observado e nunca remove o destino para tentar de novo. */
    struct stat info{};
    if (::stat(target_path.c_str(), &info) == 0)
        return k_upload_publish_exists;
    if (errno != ENOENT)
        return k_upload_publish_failed;
    if (::rename(temp_path.c_str(), target_path.c_str()) != 0)
        return k_upload_publish_failed;
#else
    /* `link` publica sem clobber de forma atomica; o nome temporario e removido
     * depois, sem afetar o alvo ja publicado. */
    if (::link(temp_path.c_str(), target_path.c_str()) != 0) {
        return errno == EEXIST ? k_upload_publish_exists : k_upload_publish_failed;
    }
    if (::unlink(temp_path.c_str()) != 0) {
        /* Publicado; mantem a posse para uma limpeza posterior no reset. */
        return k_upload_publish_ok;
    }
#endif
    files->owns_temp = false;
    files->temp_name.clear();
    files->written = 0;
    return k_upload_publish_ok;
}

void hook_discard(void *context, const char *temp_name) {
    auto *files = static_cast<upload_files *>(context);
    if (files == nullptr || temp_name == nullptr)
        return;
    if (!files->owns_temp || files->temp_name != temp_name)
        return;
    discard_owned(*files);
}

} // namespace

upload_files::~upload_files() {
    upload_files_reset(*this);
}

bool validate_upload_name(const char *name, std::size_t length) {
    if (name == nullptr)
        return false;
    if (length < 5 || length > k_max_upload_name_bytes)
        return false;
    if (!is_ascii_alnum(name[0]))
        return false;
    for (std::size_t index = 0; index < length; ++index) {
        const char value = name[index];
        if (!is_ascii_alnum(value) && value != '.' && value != '_' && value != '-')
            return false;
        if (value == '.' && index + 1 < length && name[index + 1] == '.')
            return false;
    }
    const std::size_t extension = length - 4;
    return name[extension] == '.' && ascii_lower(name[extension + 1]) == 'b' &&
           ascii_lower(name[extension + 2]) == 'm' && ascii_lower(name[extension + 3]) == 'p';
}

bool parse_upload_size(const char *text, std::size_t length, std::uint64_t *out) {
    if (out == nullptr)
        return false;
    *out = 0;
    if (text == nullptr || length == 0 || length > 20)
        return false;
    if (length > 1 && text[0] == '0')
        return false;
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < length; ++index) {
        const char digit_char = text[index];
        if (digit_char < '0' || digit_char > '9')
            return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(digit_char - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

upload_status receive_upload(const upload_file_sink &files, const upload_digest_ops &digest, upload_read_fn read,
                             void *read_context, const upload_clock &clock, const char *name,
                             std::uint64_t declared_size, const std::uint8_t *expected_digest, std::uint8_t *scratch,
                             std::size_t scratch_capacity, upload_outcome *outcome) {
    if (outcome != nullptr)
        *outcome = upload_outcome{};
    if (!validate_upload_name(name, name == nullptr ? 0 : std::strlen(name)))
        return upload_status::invalid_name;
    if (declared_size == 0)
        return upload_status::empty;
    if (declared_size > k_max_upload_bytes)
        return upload_status::too_large;
    if (read == nullptr || scratch == nullptr || scratch_capacity == 0 || expected_digest == nullptr ||
        clock.now_us == nullptr || files.target_exists == nullptr || files.open_temp == nullptr ||
        files.write == nullptr || files.close_temp == nullptr || files.publish == nullptr || files.discard == nullptr ||
        digest.start == nullptr || digest.update == nullptr || digest.finish == nullptr)
        return upload_status::internal;
    if (files.target_exists(files.context, name))
        return upload_status::conflict;

    char temp_name[64]{};
    if (!files.open_temp(files.context, temp_name, sizeof(temp_name)))
        return upload_status::io_error;

    /* Orcamento absoluto por request: lido UMA vez no inicio e comparado no
     * topo de cada iteracao; bytes recebidos nao renovam o prazo. */
    const std::int64_t started_us = clock.now_us(clock.context);
    std::uint64_t total = 0;
    unsigned consecutive_timeouts = 0;
    upload_status status = upload_status::ok;
    if (!digest.start(digest.context))
        status = upload_status::internal;
    while (status == upload_status::ok && total < declared_size) {
        if (clock.now_us(clock.context) - started_us >= k_max_upload_duration_us) {
            /* Timeout absoluto: vale mesmo com progresso continuo (o limite de
             * timeouts consecutivos continua como defesa secundaria). */
            status = upload_status::timeout;
            break;
        }
        std::size_t capacity = static_cast<std::size_t>(declared_size - total);
        if (capacity > scratch_capacity)
            capacity = scratch_capacity;
        const int chunk = read(read_context, scratch, capacity);
        if (chunk == -1) {
            /* Sem progresso: so aborta ao esgotar o orcamento consecutivo. */
            if (++consecutive_timeouts >= k_upload_max_consecutive_timeouts) {
                status = upload_status::timeout;
                break;
            }
            continue;
        }
        consecutive_timeouts = 0;
        if (chunk == 0)
            break; /* EOF antes do declarado: vira `truncated` abaixo. */
        if (chunk < 0 || static_cast<std::size_t>(chunk) > capacity) {
            status = upload_status::io_error;
            break;
        }
        const auto received = static_cast<std::size_t>(chunk);
        if (!digest.update(digest.context, scratch, received) || !files.write(files.context, scratch, received)) {
            status = upload_status::io_error;
            break;
        }
        total += received;
    }
    if (status == upload_status::ok && total != declared_size)
        status = upload_status::truncated;
    if (status == upload_status::ok && !files.close_temp(files.context, total))
        status = upload_status::io_error;

    std::uint8_t actual[k_mac_bytes]{};
    if (status == upload_status::ok && !digest.finish(digest.context, actual))
        status = upload_status::internal;
    if (status == upload_status::ok && !constant_time_equal(actual, expected_digest, sizeof(actual))) {
        status = upload_status::checksum;
    }
    if (status == upload_status::ok) {
        const int published = files.publish(files.context, temp_name, name);
        if (published == k_upload_publish_exists)
            status = upload_status::conflict;
        else if (published != k_upload_publish_ok)
            status = upload_status::io_error;
    }
    if (status == upload_status::ok) {
        if (outcome != nullptr) {
            outcome->bytes = total;
            std::memcpy(outcome->digest, actual, sizeof(actual));
        }
    } else {
        files.discard(files.context, temp_name);
    }
    volatile std::uint8_t *wipe = actual;
    for (std::size_t index = 0; index < sizeof(actual); ++index)
        wipe[index] = 0;
    return status;
}

bool format_upload_json(char *out, std::size_t capacity, const char *name, std::uint64_t bytes,
                        const std::uint8_t *digest) {
    if (out == nullptr || name == nullptr || digest == nullptr || capacity == 0)
        return false;
    char digest_hex[k_max_upload_digest_chars + 1]{};
    if (!hex_encode(digest, k_mac_bytes, digest_hex, sizeof(digest_hex)))
        return false;
    const int written =
        std::snprintf(out, capacity, "{\"name\":\"%s\",\"bytes\":%llu,\"sha256\":\"%s\",\"path\":\"%s/%s\"}", name,
                      static_cast<unsigned long long>(bytes), digest_hex, k_upload_viewer_directory, name);
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

bool upload_files_open(upload_files &files, const char *directory) {
    files.directory.clear();
    files.temp_name.clear();
    files.descriptor = -1;
    files.owns_temp = false;
    files.written = 0;
    if (directory == nullptr || directory[0] != '/')
        return false;
    const std::size_t length = std::strlen(directory);
    if (length == 0 || length > k_max_upload_directory_bytes || directory[length - 1] == '/')
        return false;
    files.directory.assign(directory, length);
    files.sequence = s_temp_sequence.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

upload_file_sink upload_files_sink(upload_files &files) {
    upload_file_sink sink{};
    sink.context = &files;
    sink.target_exists = hook_target_exists;
    sink.open_temp = hook_open_temp;
    sink.write = hook_write;
    sink.close_temp = hook_close_temp;
    sink.publish = hook_publish;
    sink.discard = hook_discard;
    return sink;
}

void upload_files_reset(upload_files &files) {
    discard_owned(files);
}

} // namespace cyberdeck_control
