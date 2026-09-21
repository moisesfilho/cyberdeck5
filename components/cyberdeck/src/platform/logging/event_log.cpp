#include "platform/logging/event_log.h"
#include "platform/logging/event_log_recent.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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

namespace {

constexpr uint32_t RECORD_MAGIC = 0x354C4F47; // "GOL5"
constexpr size_t RECORD_SIZE = 256;
constexpr size_t LOG_CAPACITY = (16U * 1024U * 1024U) / RECORD_SIZE;
constexpr size_t MESSAGE_SIZE = 192;
constexpr size_t TAG_SIZE = 32;
constexpr size_t QUEUE_LENGTH = 32;
constexpr size_t RECENT_COUNT = 10;
constexpr TickType_t RETRY_INTERVAL = pdMS_TO_TICKS(5000);

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
StaticSemaphore_t s_recent_mutex_storage;
SemaphoreHandle_t s_recent_mutex = nullptr;
LogRecord s_recent[RECENT_COUNT];
size_t s_recent_count = 0;
size_t s_recent_next = 0;
TaskHandle_t s_task = nullptr;
vprintf_like_t s_serial_vprintf = nullptr;
bool s_initialized = false;

uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
        }
    }
    return ~crc;
}

bool valid_record(const LogRecord &record)
{
    if (record.magic != RECORD_MAGIC) {
        return false;
    }
    return crc32(reinterpret_cast<const uint8_t *>(&record), RECORD_SIZE - sizeof(record.checksum)) ==
           record.checksum;
}

void copy_text(char *destination, size_t destination_size, const char *source)
{
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, destination_size, "%s", source);
}

char detect_level(const char *text)
{
    if (text != nullptr && (text[0] == 'E' || text[0] == 'W' || text[0] == 'I' || text[0] == 'D' ||
                            text[0] == 'V')) {
        return text[0];
    }
    return 'I';
}

bool ensure_directory(const char *path)
{
    if (mkdir(path, 0700) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

bool ensure_sd_mounted()
{
    if (bsp_sdcard_get_handle() != nullptr) {
        return true;
    }
    return bsp_sdcard_mount() == ESP_OK;
}

void populate_record(LogRecord *record, char level, const char *tag, const char *message)
{
    memset(record, 0, sizeof(*record));
    record->magic = RECORD_MAGIC;
    record->unix_us = static_cast<int64_t>(time(nullptr)) * 1000000LL;
    record->uptime_us = esp_timer_get_time();
    record->level = static_cast<uint8_t>(level);
    copy_text(record->tag, sizeof(record->tag), tag != nullptr ? tag : "event");
    copy_text(record->message, sizeof(record->message), message);
}

void enqueue_record(char level, const char *tag, const char *message)
{
    if (s_queue == nullptr) {
        return;
    }
    LogRecord record;
    populate_record(&record, level, tag, message);
    (void)xQueueSend(s_queue, &record, 0);
}

void remember_record(const LogRecord &record)
{
    if (s_recent_mutex == nullptr || xSemaphoreTake(s_recent_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_recent[s_recent_next] = record;
    s_recent_next = (s_recent_next + 1) % RECENT_COUNT;
    if (s_recent_count < RECENT_COUNT) {
        s_recent_count++;
    }
    xSemaphoreGive(s_recent_mutex);
}

int64_t find_latest_sequence(FILE *file, uint32_t *next_slot)
{
    struct stat info;
    int fd = fileno(file);
    if (fstat(fd, &info) != 0) {
        *next_slot = 0;
        return -1;
    }

    const size_t slot_count = static_cast<size_t>(info.st_size / RECORD_SIZE);
    const size_t slots_to_scan = slot_count < LOG_CAPACITY ? slot_count : LOG_CAPACITY;
    uint32_t latest_sequence = 0;
    uint32_t latest_slot = 0;
    bool found = false;
    LogRecord record;

    for (size_t slot = 0; slot < slots_to_scan; ++slot) {
        if (fseek(file, static_cast<long>(slot * RECORD_SIZE), SEEK_SET) != 0 ||
            fread(&record, sizeof(record), 1, file) != 1 || !valid_record(record)) {
            continue;
        }
        if (!found || record.sequence > latest_sequence) {
            found = true;
            latest_sequence = record.sequence;
            latest_slot = static_cast<uint32_t>(slot);
        }
    }

    *next_slot = found ? (latest_slot + 1U) % LOG_CAPACITY : 0;
    return found ? static_cast<int64_t>(latest_sequence) + 1 : 0;
}

bool open_log_file(FILE **file, uint32_t *next_slot, uint32_t *next_sequence)
{
    if (!ensure_sd_mounted() || !ensure_directory(CYBERDECK_CONFIG_DIR) ||
        !ensure_directory(CYBERDECK_LOG_DIR)) {
        return false;
    }

    FILE *opened = fopen(CYBERDECK_LOG_PATH, "r+b");
    if (opened == nullptr) {
        opened = fopen(CYBERDECK_LOG_PATH, "w+b");
    }
    if (opened == nullptr) {
        return false;
    }

    const int64_t sequence = find_latest_sequence(opened, next_slot);
    if (sequence < 0) {
        fclose(opened);
        return false;
    }
    *next_sequence = static_cast<uint32_t>(sequence);
    *file = opened;
    return true;
}

bool write_record(FILE *file, LogRecord *record, uint32_t *next_slot, uint32_t *next_sequence)
{
    record->sequence = (*next_sequence)++;
    record->checksum = crc32(reinterpret_cast<const uint8_t *>(record), RECORD_SIZE - sizeof(record->checksum));

    if (fseek(file, static_cast<long>(*next_slot * RECORD_SIZE), SEEK_SET) != 0 ||
        fwrite(record, sizeof(*record), 1, file) != 1) {
        return false;
    }
    fflush(file);
    (*next_slot)++;
    if (*next_slot >= LOG_CAPACITY) {
        *next_slot = 0;
    }
    return true;
}

void log_task(void *)
{
    FILE *file = nullptr;
    uint32_t next_slot = 0;
    uint32_t next_sequence = 0;
    TickType_t next_retry = 0;
    LogRecord record;

    while (true) {
        if (file == nullptr && xTaskGetTickCount() >= next_retry) {
            if (!open_log_file(&file, &next_slot, &next_sequence)) {
                next_retry = xTaskGetTickCount() + RETRY_INTERVAL;
            }
        }

        if (xQueueReceive(s_queue, &record, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        remember_record(record);

        if (file == nullptr) {
            continue;
        }

        if (!write_record(file, &record, &next_slot, &next_sequence)) {
            fclose(file);
            file = nullptr;
            next_retry = xTaskGetTickCount() + RETRY_INTERVAL;
        }
    }
}

int log_vprintf(const char *format, va_list args)
{
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

extern "C" esp_err_t event_log_init(void)
{
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

    s_serial_vprintf = esp_log_set_vprintf(log_vprintf);
    if (xTaskCreate(log_task, "event_log", 6144, nullptr, 2, &s_task) != pdPASS) {
        esp_log_set_vprintf(s_serial_vprintf);
        s_serial_vprintf = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    return ESP_OK;
}

extern "C" void event_log_write(char level, const char *tag, const char *message)
{
    enqueue_record(level, tag, message);
}

extern "C" size_t event_log_latest(size_t max_events, event_log_line_callback_t callback, void *context)
{
    if (callback == nullptr || s_recent_mutex == nullptr || max_events == 0) {
        return 0;
    }

    size_t indices[RECENT_COUNT];
    size_t count = 0;
    if (xSemaphoreTake(s_recent_mutex, portMAX_DELAY) == pdTRUE) {
        count = event_log_recent_indices(s_recent_count, s_recent_next, max_events,
                                         RECENT_COUNT, indices);
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
        if (seconds >= 1577836800) {
            struct tm local_time;
            localtime_r(&seconds, &local_time);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
        } else {
            snprintf(timestamp, sizeof(timestamp), "up:%" PRId64 "ms", record.uptime_us / 1000LL);
        }

        char line[RECORD_SIZE];
        snprintf(line, sizeof(line), "%s %c %s: %s", timestamp, record.level, record.tag,
                 record.message);
        for (char *p = line; *p != '\0'; ++p) {
            if (*p == '\r' || *p == '\n') {
                *p = ' ';
            }
        }
        callback(line, context);
    }
    return count;
}
