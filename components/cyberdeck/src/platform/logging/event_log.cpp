#include "platform/logging/event_log.h"
#include "platform/display/cyberdeck_clock.h"
#include "platform/logging/event_log_recent.h"

#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "bsp/esp-bsp.h"
#include "cyberdeck_paths.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#if CONFIG_CYBERDECK_LOG_LINES < 1 || CONFIG_CYBERDECK_LOG_LINES > 64
#error "CONFIG_CYBERDECK_LOG_LINES must be in the range 1..64"
#endif

namespace {

constexpr uint32_t RECORD_MAGIC = 0x354C4F47; // "GOL5"
constexpr size_t RECORD_SIZE = 256;
constexpr size_t LOG_CAPACITY = (16U * 1024U * 1024U) / RECORD_SIZE;
constexpr size_t MESSAGE_SIZE = 192;
constexpr size_t TAG_SIZE = 32;
constexpr size_t QUEUE_LENGTH = 32;
constexpr size_t DURABLE_QUEUE_LENGTH = 4;
constexpr size_t RECENT_CAPACITY = EVENT_LOG_RECENT_MAX_CAPACITY;
constexpr TickType_t RETRY_INTERVAL = pdMS_TO_TICKS(5000);
constexpr size_t TEXT_FILE_LIMIT = 1024U * 1024U;
constexpr size_t TEXT_ROTATION_COUNT = 7;
constexpr TickType_t TEXT_MUTEX_TIMEOUT = pdMS_TO_TICKS(100);

struct __attribute__((packed)) LogRecord {
    uint32_t magic;
    uint32_t sequence;
    int64_t unix_us;
    int64_t uptime_us;
    uint8_t level;
    uint8_t reserved[3];
    char tag[TAG_SIZE];
    char message[MESSAGE_SIZE];
    uint32_t checksum;
};

static_assert(sizeof(LogRecord) == RECORD_SIZE, "unexpected log record size");

QueueHandle_t s_queue = nullptr;
StaticQueue_t s_queue_struct;
uint8_t s_queue_storage[QUEUE_LENGTH * sizeof(LogRecord)];
QueueHandle_t s_text_queue = nullptr;
StaticQueue_t s_text_queue_struct;
uint8_t s_text_queue_storage[QUEUE_LENGTH * sizeof(LogRecord)];
QueueHandle_t s_durable_queue = nullptr;
StaticQueue_t s_durable_queue_struct;
struct DurableRequest;
uint8_t s_durable_queue_storage[DURABLE_QUEUE_LENGTH * sizeof(DurableRequest *)];
StaticSemaphore_t s_recent_mutex_storage;
SemaphoreHandle_t s_recent_mutex = nullptr;
StaticSemaphore_t s_text_mutex_storage;
SemaphoreHandle_t s_text_mutex = nullptr;
LogRecord s_recent[RECENT_CAPACITY];
LogRecord s_rebuild_recent[RECENT_CAPACITY];
size_t s_recent_count = 0;
size_t s_recent_next = 0;
TaskHandle_t s_task = nullptr;
TaskHandle_t s_text_task = nullptr;
vprintf_like_t s_serial_vprintf = nullptr;
bool s_initialized = false;

struct DurableRequest {
    LogRecord record{};
    SemaphoreHandle_t complete = nullptr;
    StaticSemaphore_t complete_storage;
    bool persisted = false;
    std::atomic<uint8_t> references{1};
};

void release_durable_request(DurableRequest *request) {
    if (request != nullptr && request->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete request;
    }
}

void text_path(size_t generation, char *path, size_t capacity) {
    if (generation == 0)
        snprintf(path, capacity, "%s", CYBERDECK_TEXT_LOG_PATH);
    else
        snprintf(path, capacity, "%s.%u", CYBERDECK_TEXT_LOG_PATH, static_cast<unsigned>(generation));
}

struct TextSnapshot {
    FILE *files[TEXT_ROTATION_COUNT + 1] = {};
    size_t sizes[TEXT_ROTATION_COUNT + 1] = {};
    size_t count = 0;
    size_t total = 0;
};

void close_text_snapshot(TextSnapshot *snapshot) {
    for (size_t index = 0; index < snapshot->count; ++index) {
        if (snapshot->files[index] != nullptr)
            fclose(snapshot->files[index]);
        snapshot->files[index] = nullptr;
    }
    snapshot->count = 0;
}

bool open_text_snapshot(TextSnapshot *snapshot) {
    for (size_t generation = TEXT_ROTATION_COUNT + 1; generation-- > 0;) {
        char path[96];
        text_path(generation, path, sizeof(path));
        FILE *file = fopen(path, "rb");
        if (file == nullptr) {
            if (errno == ENOENT)
                continue;
            close_text_snapshot(snapshot);
            return false;
        }
        struct stat info = {};
        if (fstat(fileno(file), &info) != 0 || info.st_size < 0) {
            fclose(file);
            close_text_snapshot(snapshot);
            return false;
        }
        snapshot->files[snapshot->count] = file;
        snapshot->sizes[snapshot->count] = static_cast<size_t>(info.st_size);
        snapshot->total += snapshot->sizes[snapshot->count];
        ++snapshot->count;
    }
    return true;
}

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
        }
    }
    return ~crc;
}

bool valid_record(const LogRecord &record) {
    if (record.magic != RECORD_MAGIC) {
        return false;
    }
    return crc32(reinterpret_cast<const uint8_t *>(&record), RECORD_SIZE - sizeof(record.checksum)) == record.checksum;
}

bool sequence_is_newer(uint32_t candidate, uint32_t reference) {
    return candidate != reference && static_cast<int32_t>(candidate - reference) > 0;
}

void insert_recent(LogRecord *records, size_t *count, const LogRecord &record) {
    size_t position = 0;
    while (position < *count && !sequence_is_newer(records[position].sequence, record.sequence)) {
        ++position;
    }

    if (*count == RECENT_CAPACITY && position == 0) {
        return;
    }
    if (*count < RECENT_CAPACITY) {
        for (size_t index = *count; index > position; --index) {
            records[index] = records[index - 1];
        }
        ++*count;
    } else {
        for (size_t index = 1; index < RECENT_CAPACITY; ++index) {
            records[index - 1] = records[index];
        }
        --position;
        for (size_t index = *count - 1; index > position; --index) {
            records[index] = records[index - 1];
        }
    }
    records[position] = record;
}

void copy_text(char *destination, size_t destination_size, const char *source) {
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, destination_size, "%s", source);
}

char detect_level(const char *text) {
    if (text != nullptr && (text[0] == 'E' || text[0] == 'W' || text[0] == 'I' || text[0] == 'D' || text[0] == 'V')) {
        return text[0];
    }
    return 'I';
}

bool ensure_directory(const char *path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

bool ensure_sd_mounted() {
    if (bsp_sdcard_get_handle() != nullptr) {
        return true;
    }
    return bsp_sdcard_mount() == ESP_OK;
}

void populate_record(LogRecord *record, char level, const char *tag, const char *message) {
    memset(record, 0, sizeof(*record));
    record->magic = RECORD_MAGIC;
    record->unix_us = static_cast<int64_t>(time(nullptr)) * 1000000LL;
    record->uptime_us = esp_timer_get_time();
    record->level = static_cast<uint8_t>(level);
    copy_text(record->tag, sizeof(record->tag), tag != nullptr ? tag : "event");
    copy_text(record->message, sizeof(record->message), message);
}

void wake_log_task() {
    if (s_task != nullptr)
        xTaskNotifyGive(s_task);
}

void enqueue_record(char level, const char *tag, const char *message) {
    if (s_queue == nullptr) {
        return;
    }
    LogRecord record;
    populate_record(&record, level, tag, message);
    if (xQueueSend(s_queue, &record, 0) == pdTRUE)
        wake_log_task();
}

void remember_record(const LogRecord &record) {
    if (s_recent_mutex == nullptr || xSemaphoreTake(s_recent_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_recent[s_recent_next] = record;
    s_recent_next = (s_recent_next + 1) % RECENT_CAPACITY;
    if (s_recent_count < RECENT_CAPACITY) {
        s_recent_count++;
    }
    xSemaphoreGive(s_recent_mutex);
}

bool rebuild_state(FILE *file, uint32_t *next_slot, uint32_t *next_sequence) {
    struct stat info;
    int fd = fileno(file);
    if (fstat(fd, &info) != 0) {
        *next_slot = 0;
        *next_sequence = 0;
        return false;
    }

    const size_t slot_count = static_cast<size_t>(info.st_size / RECORD_SIZE);
    const size_t slots_to_scan = slot_count < LOG_CAPACITY ? slot_count : LOG_CAPACITY;
    uint32_t latest_sequence = 0;
    uint32_t latest_slot = 0;
    bool found = false;
    size_t recent_count = 0;
    LogRecord record;

    for (size_t slot = 0; slot < slots_to_scan; ++slot) {
        if (fseek(file, static_cast<long>(slot * RECORD_SIZE), SEEK_SET) != 0 ||
            fread(&record, sizeof(record), 1, file) != 1 || !valid_record(record)) {
            continue;
        }
        if (!found || sequence_is_newer(record.sequence, latest_sequence)) {
            found = true;
            latest_sequence = record.sequence;
            latest_slot = static_cast<uint32_t>(slot);
        }
        insert_recent(s_rebuild_recent, &recent_count, record);
    }

    *next_slot = found ? (latest_slot + 1U) % LOG_CAPACITY : 0;
    *next_sequence = found ? latest_sequence + 1U : 0;

    if (s_recent_mutex != nullptr && xSemaphoreTake(s_recent_mutex, portMAX_DELAY) == pdTRUE) {
        s_recent_count = recent_count;
        s_recent_next = recent_count == RECENT_CAPACITY ? 0 : recent_count;
        for (size_t index = 0; index < recent_count; ++index) {
            s_recent[index] = s_rebuild_recent[index];
        }
        xSemaphoreGive(s_recent_mutex);
    }
    return true;
}

bool open_log_file(FILE **file, uint32_t *next_slot, uint32_t *next_sequence) {
    if (!ensure_sd_mounted() || !ensure_directory(CYBERDECK_CONFIG_DIR) || !ensure_directory(CYBERDECK_LOG_DIR)) {
        return false;
    }

    FILE *opened = fopen(CYBERDECK_LOG_PATH, "r+b");
    if (opened == nullptr) {
        opened = fopen(CYBERDECK_LOG_PATH, "w+b");
    }
    if (opened == nullptr) {
        return false;
    }

    char text_file[96];
    text_path(0, text_file, sizeof(text_file));
    FILE *projection = fopen(text_file, "ab");
    if (projection != nullptr)
        fclose(projection);

    if (!rebuild_state(opened, next_slot, next_sequence)) {
        fclose(opened);
        return false;
    }
    *file = opened;
    return true;
}

bool rotate_text_if_needed(size_t incoming) {
    char current[96];
    text_path(0, current, sizeof(current));
    struct stat info = {};
    if (stat(current, &info) != 0)
        return errno == ENOENT;
    if (info.st_size < 0 || static_cast<size_t>(info.st_size) + incoming <= TEXT_FILE_LIMIT)
        return true;
    char from[96], to[96];
    for (size_t generation = TEXT_ROTATION_COUNT; generation > 1; --generation) {
        text_path(generation - 1, from, sizeof(from));
        text_path(generation, to, sizeof(to));
        (void)unlink(to);
        if (rename(from, to) != 0 && errno != ENOENT)
            return false;
    }
    text_path(1, to, sizeof(to));
    (void)unlink(to);
    return rename(current, to) == 0;
}

bool append_text_projection(const LogRecord &record) {
    char line[RECORD_SIZE];
    char timestamp[24];
    char tag[TAG_SIZE];
    char message[MESSAGE_SIZE];
    const time_t seconds = static_cast<time_t>(record.unix_us / 1000000LL);
    struct tm utc = {};
    if (seconds >= 1577836800 && gmtime_r(&seconds, &utc) != nullptr &&
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &utc) != 0) {
    } else {
        snprintf(timestamp, sizeof(timestamp), "up:%" PRId64 "ms", record.uptime_us / 1000LL);
    }
    copy_text(tag, sizeof(tag), record.tag);
    copy_text(message, sizeof(message), record.message);
    for (char *p = tag; *p != '\0'; ++p) {
        if (*p == '\r' || *p == '\n')
            *p = ' ';
    }
    for (char *p = message; *p != '\0'; ++p) {
        if (*p == '\r' || *p == '\n')
            *p = ' ';
    }
    const int written = snprintf(line, sizeof(line), "%s %c %s: %s\n", timestamp, record.level, tag, message);
    if (written < 0)
        return false;
    const size_t length =
        written >= static_cast<int>(sizeof(line) - 1) ? sizeof(line) - 1 : static_cast<size_t>(written);
    /* A projection record is always one complete physical line. */
    line[length - 1] = '\n';
    if (length > TEXT_FILE_LIMIT || !rotate_text_if_needed(length))
        return false;
    char path[96];
    text_path(0, path, sizeof(path));
    FILE *file = fopen(path, "ab");
    if (file == nullptr)
        return false;
    const bool ok = fwrite(line, 1, length, file) == length && fflush(file) == 0 && fsync(fileno(file)) == 0;
    fclose(file);
    return ok;
}

void text_log_task(void *) {
    LogRecord record;
    while (true) {
        if (xQueueReceive(s_text_queue, &record, portMAX_DELAY) == pdTRUE && s_text_mutex != nullptr &&
            xSemaphoreTake(s_text_mutex, portMAX_DELAY) == pdTRUE) {
            (void)append_text_projection(record);
            xSemaphoreGive(s_text_mutex);
        }
    }
}

bool write_record(FILE *file, LogRecord *record, uint32_t *next_slot, uint32_t *next_sequence) {
    LogRecord candidate = *record;
    candidate.sequence = *next_sequence;
    candidate.checksum = crc32(reinterpret_cast<const uint8_t *>(&candidate), RECORD_SIZE - sizeof(candidate.checksum));

    if (fseek(file, static_cast<long>(*next_slot * RECORD_SIZE), SEEK_SET) != 0 ||
        fwrite(&candidate, sizeof(candidate), 1, file) != 1 || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        return false;
    }
    *record = candidate;
    *next_sequence = candidate.sequence + 1U;
    (*next_slot)++;
    if (*next_slot >= LOG_CAPACITY) {
        *next_slot = 0;
    }
    return true;
}

void complete_durable_request(FILE *file, DurableRequest *request, uint32_t *next_slot, uint32_t *next_sequence) {
    request->persisted = write_record(file, &request->record, next_slot, next_sequence);
    if (request->persisted && s_text_queue != nullptr && xQueueSend(s_text_queue, &request->record, 0) != pdTRUE) {
        /* events.log is authoritative; text is explicitly best-effort. */
        ESP_LOGW("event_log", "text projection queue full; durable event retained");
    }
    (void)xSemaphoreGive(request->complete);
}

void log_task(void *) {
    FILE *file = nullptr;
    uint32_t next_slot = 0;
    uint32_t next_sequence = 0;
    TickType_t next_retry = 0;
    LogRecord record{};

    while (true) {
        if (file == nullptr && xTaskGetTickCount() >= next_retry) {
            if (!open_log_file(&file, &next_slot, &next_sequence)) {
                next_retry = xTaskGetTickCount() + RETRY_INTERVAL;
            }
        }

        DurableRequest *durable = nullptr;
        if (s_durable_queue != nullptr && xQueueReceive(s_durable_queue, &durable, 0) == pdTRUE) {
            if (file != nullptr) {
                complete_durable_request(file, durable, &next_slot, &next_sequence);
            } else {
                durable->persisted = false;
                (void)xSemaphoreGive(durable->complete);
            }
            release_durable_request(durable);
            continue;
        }

        if (xQueueReceive(s_queue, &record, 0) == pdTRUE) {
            if (file == nullptr) {
                remember_record(record);
                continue;
            }

            if (!write_record(file, &record, &next_slot, &next_sequence)) {
                fclose(file);
                file = nullptr;
                next_retry = xTaskGetTickCount() + RETRY_INTERVAL;
                remember_record(record);
                continue;
            }
            /* Never perform projection I/O in the binary writer. A full or slow
             * text path can only drop its best-effort projection queue entry. */
            if (s_text_queue != nullptr)
                (void)xQueueSend(s_text_queue, &record, 0);
            remember_record(record);
            continue;
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}

int log_vprintf(const char *format, va_list args) {
    va_list serial_args;
    va_copy(serial_args, args);
    int result = s_serial_vprintf != nullptr ? s_serial_vprintf(format, serial_args) : 0;
    va_end(serial_args);

    char message[MESSAGE_SIZE];
    va_list log_args;
    va_copy(log_args, args);
    vsnprintf(message, sizeof(message), format, log_args);
    va_end(log_args);
    enqueue_record(detect_level(message), "esp_log", message);
    return result;
}

} // namespace

extern "C" esp_err_t event_log_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    s_queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(LogRecord), s_queue_storage, &s_queue_struct);
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    s_recent_mutex = xSemaphoreCreateMutexStatic(&s_recent_mutex_storage);
    if (s_recent_mutex == nullptr) {
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_text_mutex = xSemaphoreCreateMutexStatic(&s_text_mutex_storage);
    if (s_text_mutex == nullptr) {
        s_recent_mutex = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_text_queue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(LogRecord), s_text_queue_storage, &s_text_queue_struct);
    s_durable_queue = xQueueCreateStatic(DURABLE_QUEUE_LENGTH, sizeof(DurableRequest *), s_durable_queue_storage,
                                         &s_durable_queue_struct);
    if (s_text_queue == nullptr || s_durable_queue == nullptr ||
        xTaskCreate(text_log_task, "event_text", 4096, nullptr, 1, &s_text_task) != pdPASS) {
        s_text_task = nullptr;
        s_text_queue = nullptr;
        s_durable_queue = nullptr;
        s_text_mutex = nullptr;
        s_recent_mutex = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_serial_vprintf = esp_log_set_vprintf(log_vprintf);
    if (xTaskCreate(log_task, "event_log", 6144, nullptr, 2, &s_task) != pdPASS) {
        esp_log_set_vprintf(s_serial_vprintf);
        s_serial_vprintf = nullptr;
        if (s_text_task != nullptr) {
            vTaskDelete(s_text_task);
            s_text_task = nullptr;
        }
        s_text_queue = nullptr;
        s_durable_queue = nullptr;
        s_text_mutex = nullptr;
        s_recent_mutex = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    return ESP_OK;
}

extern "C" void event_log_write(char level, const char *tag, const char *message) {
    enqueue_record(level, tag, message);
}

extern "C" esp_err_t event_log_write_durable(char level, const char *tag, const char *message) {
    if (!s_initialized || s_durable_queue == nullptr)
        return ESP_ERR_INVALID_STATE;

    DurableRequest *request = new (std::nothrow) DurableRequest;
    if (request == nullptr)
        return ESP_ERR_NO_MEM;
    populate_record(&request->record, level, tag, message);
    request->complete = xSemaphoreCreateBinaryStatic(&request->complete_storage);
    if (request->complete == nullptr) {
        release_durable_request(request);
        return ESP_ERR_NO_MEM;
    }

    DurableRequest *request_pointer = request;
    for (int attempt = 0; attempt < 3; ++attempt) {
        request->references.store(2, std::memory_order_release);
        if (xQueueSend(s_durable_queue, &request_pointer, 0) == pdTRUE) {
            wake_log_task();
            /* The caller and writer own independent references. This keeps a
             * timed-out request valid until the writer drains it. */
            if (xSemaphoreTake(request->complete, pdMS_TO_TICKS(250)) == pdTRUE) {
                const bool persisted = request->persisted;
                release_durable_request(request);
                return persisted ? ESP_OK : ESP_FAIL;
            }
            release_durable_request(request);
            ESP_LOGW("event_log", "durable event acknowledgement timed out");
            return ESP_ERR_TIMEOUT;
        }
        request->references.store(1, std::memory_order_release);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW("event_log", "durable event queue full; event not acknowledged");
    release_durable_request(request);
    /* No queue send accepted this request, so callers may retry safely. */
    return ESP_ERR_NO_MEM;
}

extern "C" size_t event_log_latest(size_t max_events, event_log_line_callback_t callback, void *context) {
    if (callback == nullptr || s_recent_mutex == nullptr || max_events == 0) {
        return 0;
    }

    size_t indices[RECENT_CAPACITY];
    size_t count = 0;
    if (xSemaphoreTake(s_recent_mutex, portMAX_DELAY) == pdTRUE) {
        count = event_log_recent_indices(s_recent_count, s_recent_next, max_events, RECENT_CAPACITY, indices);
        xSemaphoreGive(s_recent_mutex);
    }

    for (size_t i = 0; i < count; ++i) {
        LogRecord record;
        if (xSemaphoreTake(s_recent_mutex, portMAX_DELAY) != pdTRUE) {
            return i;
        }
        record = s_recent[indices[i]];
        xSemaphoreGive(s_recent_mutex);

        char timestamp[24];
        const time_t seconds = static_cast<time_t>(record.unix_us / 1000000LL);
        struct tm utc_time;
        cyberdeck_clock_time_t utc = {};
        cyberdeck_clock_time_t configured_time = {};
        if (seconds >= 1577836800 && gmtime_r(&seconds, &utc_time) != nullptr) {
            utc.year = static_cast<int16_t>(utc_time.tm_year + 1900);
            utc.month = static_cast<uint8_t>(utc_time.tm_mon + 1);
            utc.day = static_cast<uint8_t>(utc_time.tm_mday);
            utc.hour = static_cast<uint8_t>(utc_time.tm_hour);
            utc.minute = static_cast<uint8_t>(utc_time.tm_min);
        }
        if (!cyberdeck_clock_from_utc(&utc, CYBERDECK_CLOCK_GMT_MINUS_3_OFFSET_MIN, &configured_time)) {
            snprintf(timestamp, sizeof(timestamp), "up:%" PRId64 "ms", record.uptime_us / 1000LL);
        } else {
            struct tm output_time = utc_time;
            output_time.tm_year = configured_time.year - 1900;
            output_time.tm_mon = configured_time.month - 1;
            output_time.tm_mday = configured_time.day;
            output_time.tm_hour = configured_time.hour;
            output_time.tm_min = configured_time.minute;
            if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &output_time) == 0) {
                snprintf(timestamp, sizeof(timestamp), "up:%" PRId64 "ms", record.uptime_us / 1000LL);
            }
        }

        char line[RECORD_SIZE];
        snprintf(line, sizeof(line), "%s %c %s: %s", timestamp, record.level, record.tag, record.message);
        for (char *p = line; *p != '\0'; ++p) {
            if (*p == '\r' || *p == '\n') {
                *p = ' ';
            }
        }
        callback(line, context);
    }
    return count;
}

extern "C" size_t event_log_text_size(void) {
    if (s_text_mutex == nullptr || xSemaphoreTake(s_text_mutex, TEXT_MUTEX_TIMEOUT) != pdTRUE)
        return 0;
    size_t total = 0;
    char path[96];
    for (size_t generation = TEXT_ROTATION_COUNT;; --generation) {
        text_path(generation, path, sizeof(path));
        struct stat info = {};
        if (stat(path, &info) == 0 && info.st_size > 0)
            total += static_cast<size_t>(info.st_size);
        if (generation == 0)
            break;
    }
    xSemaphoreGive(s_text_mutex);
    return total;
}

extern "C" esp_err_t event_log_text_read(size_t offset, uint8_t *buffer, size_t capacity, size_t *out_read) {
    if (out_read == nullptr || buffer == nullptr || capacity == 0 || capacity > 1024 || s_text_mutex == nullptr)
        return ESP_ERR_INVALID_ARG;
    *out_read = 0;
    if (xSemaphoreTake(s_text_mutex, TEXT_MUTEX_TIMEOUT) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    /* Keep descriptors open after releasing the coordination mutex. Renames can
     * then proceed without changing this bounded, generation-consistent view. */
    TextSnapshot snapshot;
    const bool snapshot_ok = open_text_snapshot(&snapshot);
    xSemaphoreGive(s_text_mutex);
    if (!snapshot_ok)
        return ESP_FAIL;
    if (offset >= snapshot.total) {
        close_text_snapshot(&snapshot);
        return ESP_OK;
    }

    size_t relative = offset;
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (relative >= snapshot.sizes[index]) {
            relative -= snapshot.sizes[index];
            continue;
        }
        if (fseek(snapshot.files[index], static_cast<long>(relative), SEEK_SET) != 0) {
            close_text_snapshot(&snapshot);
            return ESP_FAIL;
        }
        *out_read = fread(buffer, 1, capacity, snapshot.files[index]);
        const bool read_ok = *out_read != 0;
        close_text_snapshot(&snapshot);
        /* A non-empty snapshot position must never look like a successful EOF. */
        return read_ok ? ESP_OK : ESP_FAIL;
    }
    close_text_snapshot(&snapshot);
    return ESP_FAIL;
}
