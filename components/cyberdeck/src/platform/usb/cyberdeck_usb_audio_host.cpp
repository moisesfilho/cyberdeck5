#include "platform/usb/cyberdeck_usb_audio_host.h"

#include "bsp/m5stack_tab5.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/audio/cyberdeck_audio_output.h"
#include "usb/uac_host.h"
#include "usb/usb_host.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace cyberdeck_usb_audio_host {
namespace {
constexpr char k_tag[] = "usb_audio";
constexpr std::size_t k_event_depth = 8;
constexpr std::uint32_t k_usb_task_stack_bytes = 6144;
constexpr std::uint32_t k_worker_stack_bytes = 8192;
constexpr std::uint32_t k_device_buffer_bytes = 4096;
constexpr std::uint32_t k_device_buffer_threshold = 2048;
constexpr std::uint32_t k_stop_timeout_ms = 2000;
constexpr std::uint32_t k_client_queue_depth = 5;
constexpr std::uint32_t k_worker_poll_ticks = pdMS_TO_TICKS(50);
constexpr std::uint8_t k_descriptor_interface = 0x04;
constexpr std::uint8_t k_descriptor_endpoint = 0x05;
constexpr std::uint8_t k_descriptor_cs_interface = 0x24;
constexpr std::uint8_t k_class_audio = 0x01;
constexpr std::uint8_t k_subclass_audio_control = 0x01;
constexpr std::uint8_t k_subclass_audio_streaming = 0x02;
constexpr std::uint8_t k_cs_interface_header = 0x01;
constexpr std::uint8_t k_transfer_isochronous = 0x01;
constexpr std::uint8_t k_playback_bit_resolution = 16;
constexpr std::uint8_t k_player_max_volume = 50;
constexpr std::uint32_t k_playback_buffer_bytes = 16384;
constexpr std::uint32_t k_playback_write_timeout_ms = 20;
constexpr std::uint32_t k_playback_write_wait_ms = 200;
constexpr std::size_t k_playback_max_samples = 2304;

enum class event_kind : std::uint8_t { tx_connected, disconnected, device_new, device_gone, stop };
struct host_event {
    event_kind kind;
    std::uint8_t address;
    std::uint8_t interface_number;
};

/* Rotation of interface/endpoint facts found in a configuration descriptor. */
struct descriptor_facts {
    std::uint8_t playback_streams = 0;
    std::uint8_t recording_streams = 0;
    bool audio_control = false;
};

QueueHandle_t s_events = nullptr;
SemaphoreHandle_t s_state_mutex = nullptr;
SemaphoreHandle_t s_worker_stopped = nullptr;
TaskHandle_t s_worker = nullptr;
usb_host_client_handle_t s_client = nullptr;
bool s_host_started = false;
bool s_uac_started = false;

/* Two independent observers feed one published view: the passive client reads
 * raw descriptors (identity, audio class version, endpoint direction) while
 * the UAC class driver reports stream formats and strings. Each keeps its own
 * facts so one cannot erase the other, and the snapshot is composed from both.
 * All three are guarded by s_state_mutex. */
struct descriptor_state {
    std::uint8_t address = 0;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::uint16_t usb_version = 0;
    std::uint8_t audio_spec = 0;
    std::uint8_t playback_streams = 0;
    std::uint8_t recording_streams = 0;
    bool audio_control = false;
    bool valid = false;
} s_descriptors;

struct stream_state {
    std::uint8_t address = 0;
    std::uint8_t interface_number = 0;
    char manufacturer[k_max_string_bytes]{};
    char product[k_max_string_bytes]{};
    audio_format formats[k_max_formats]{};
    std::size_t format_count = 0;
} s_stream;

state s_status = state::stopped;
std::uint32_t s_error_code = 0;
snapshot s_snapshot{};

/* Playback is driven by the player worker task; start/stop serialise with
 * s_playback_mutex and write only reads the handle. */
SemaphoreHandle_t s_playback_mutex = nullptr;
uac_host_device_handle_t s_playback_handle = nullptr;
std::uint32_t s_playback_rate = 0;
std::uint8_t s_playback_channels = 0;
/* Player volume is 0..50. The headset gets it through its Feature Unit when
 * available; otherwise the PCM is attenuated, so the player control always has
 * an audible effect. */
std::uint32_t s_playback_gain_q15 = 32767;
bool s_device_volume = false;
std::int16_t s_scaled_samples[k_playback_max_samples];

void compose_locked() {
    snapshot value{};
    value.status = s_status;
    value.error_code = s_error_code;
    if (s_descriptors.valid) {
        value.address = s_descriptors.address;
        value.vendor_id = s_descriptors.vendor_id;
        value.product_id = s_descriptors.product_id;
        value.usb_version = s_descriptors.usb_version;
        value.audio_spec = s_descriptors.audio_spec;
        value.playback_streams = s_descriptors.playback_streams;
        value.recording_streams = s_descriptors.recording_streams;
        value.audio_control = s_descriptors.audio_control;
    }
    if (s_stream.format_count != 0 || s_stream.product[0] != '\0' || s_stream.manufacturer[0] != '\0') {
        value.interface_number = s_stream.interface_number;
        value.format_count = s_stream.format_count;
        for (std::size_t index = 0; index < s_stream.format_count; ++index)
            value.formats[index] = s_stream.formats[index];
        std::memcpy(value.manufacturer, s_stream.manufacturer, sizeof(value.manufacturer));
        std::memcpy(value.product, s_stream.product, sizeof(value.product));
    }
    value.updated_at_us = static_cast<std::uint64_t>(esp_timer_get_time());
    s_snapshot = value;
}

template <typename Mutation> void mutate(Mutation mutation) {
    if (s_state_mutex == nullptr || xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    mutation();
    compose_locked();
    xSemaphoreGive(s_state_mutex);
}

void set_status(state status, std::uint32_t error = 0) {
    mutate([status, error]() {
        s_status = status;
        s_error_code = error;
    });
}

void clear_device_facts() {
    s_descriptors = descriptor_state{};
    s_stream = stream_state{};
}

bool format_supports(const snapshot &value, std::uint32_t sample_rate, std::uint8_t channels) {
    for (std::size_t index = 0; index < value.format_count; ++index) {
        const audio_format &format = value.formats[index];
        if (format.channels != channels || format.bit_resolution < k_playback_bit_resolution) continue;
        if (sample_rate < format.sample_rate_min || sample_rate > format.sample_rate_max) continue;
        return true;
    }
    return false;
}

void playback_device_event(uac_host_device_handle_t, const uac_host_device_event_t event, void *) {
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) s_playback_handle = nullptr;
}

void copy_ascii(char *out, std::size_t capacity, const wchar_t *in, std::size_t source_capacity) {
    if (capacity == 0) return;
    std::size_t index = 0;
    while (in != nullptr && index < source_capacity && in[index] != L'\0' && index + 1 < capacity) {
        const wchar_t ch = in[index];
        const bool safe = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                          (ch >= L'0' && ch <= L'9') || ch == L' ' || ch == L'.' ||
                          ch == L'-' || ch == L'_';
        out[index] = safe ? static_cast<char>(ch) : '?';
        ++index;
    }
    out[index] = '\0';
}

bool enqueue(const host_event &item) {
    if (s_events == nullptr) return false;
    return xQueueSend(s_events, &item, 0) == pdTRUE;
}

void driver_event(std::uint8_t address, std::uint8_t interface_number,
                  const uac_host_driver_event_t event, void *) {
    ESP_LOGI(k_tag, "uac event=%d addr=%u iface=%u", static_cast<int>(event), address, interface_number);
    if (event != UAC_HOST_DRIVER_EVENT_TX_CONNECTED) return;
    if (!enqueue(host_event{event_kind::tx_connected, address, interface_number}))
        set_status(state::error, ESP_ERR_NO_MEM);
}

void device_event(uac_host_device_handle_t, const uac_host_device_event_t event, void *) {
    ESP_LOGI(k_tag, "uac device event=%d", static_cast<int>(event));
    if (event != UAC_HOST_DRIVER_EVENT_DISCONNECTED) return;
    (void)enqueue(host_event{event_kind::disconnected, 0, 0});
}

/* Passive client: reports every enumerated device and, when it can, the audio
 * facts from the configuration descriptor. It never claims an interface, so it
 * cannot conflict with the UAC class driver. This is what distinguishes "no
 * device on the bus" from "device enumerated but not a UAC 1.0 playback
 * interface", which the class driver cannot report. */
void client_event(const usb_host_client_event_msg_t *message, void *) {
    if (message == nullptr) return;
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        (void)enqueue(host_event{event_kind::device_new, message->new_dev.address, 0});
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        (void)enqueue(host_event{event_kind::device_gone, 0, 0});
    }
}

bool scan_configuration(const usb_config_desc_t *config, descriptor_facts &facts, std::uint8_t &audio_spec) {
    if (config == nullptr) return false;
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(config);
    const std::size_t total = config->wTotalLength;
    std::size_t offset = 0;
    std::uint8_t current_subclass = 0;
    while (offset + 2 <= total) {
        const std::uint8_t length = bytes[offset];
        const std::uint8_t type = bytes[offset + 1];
        if (length < 2 || offset + length > total) return false;
        if (type == k_descriptor_interface && length >= 9) {
            const std::uint8_t interface_class = bytes[offset + 5];
            current_subclass = interface_class == k_class_audio ? bytes[offset + 6] : 0;
            if (current_subclass == k_subclass_audio_control) facts.audio_control = true;
        } else if (type == k_descriptor_endpoint && length >= 7 &&
                   current_subclass == k_subclass_audio_streaming) {
            const std::uint8_t address = bytes[offset + 2];
            const std::uint8_t attributes = bytes[offset + 3];
            if ((attributes & 0x03) == k_transfer_isochronous) {
                if ((address & 0x80) != 0) {
                    if (facts.recording_streams < UINT8_MAX) ++facts.recording_streams;
                } else if (facts.playback_streams < UINT8_MAX) {
                    ++facts.playback_streams;
                }
            }
        } else if (type == k_descriptor_cs_interface && length >= 5 &&
                   bytes[offset + 2] == k_cs_interface_header) {
            const std::uint16_t bcd_adc = static_cast<std::uint16_t>(bytes[offset + 3] | (bytes[offset + 4] << 8));
            audio_spec = bcd_adc >= 0x0200 ? 2 : 1;
        }
        offset += length;
    }
    return true;
}

/* Read-only probe of a freshly enumerated device. Returns false when the
 * descriptors cannot be read; the published view then keeps whatever the class
 * driver already reported. */
bool probe_descriptors(std::uint8_t address, descriptor_state &facts) {
    usb_device_handle_t device = nullptr;
    esp_err_t result = usb_host_device_open(s_client, address, &device);
    if (result != ESP_OK) {
        ESP_LOGW(k_tag, "device open failed: %s", esp_err_to_name(result));
        return false;
    }
    facts.address = address;
    const usb_device_desc_t *device_desc = nullptr;
    result = usb_host_get_device_descriptor(device, &device_desc);
    if (result == ESP_OK && device_desc != nullptr) {
        facts.usb_version = device_desc->bcdUSB;
        facts.vendor_id = device_desc->idVendor;
        facts.product_id = device_desc->idProduct;
    }
    const usb_config_desc_t *config = nullptr;
    descriptor_facts found{};
    std::uint8_t audio_spec = 0;
    result = usb_host_get_active_config_descriptor(device, &config);
    if (result == ESP_OK && scan_configuration(config, found, audio_spec)) {
        facts.audio_spec = audio_spec;
        facts.playback_streams = found.playback_streams;
        facts.recording_streams = found.recording_streams;
        facts.audio_control = found.audio_control;
    } else {
        ESP_LOGW(k_tag, "configuration descriptor unavailable: %s", esp_err_to_name(result));
    }
    const esp_err_t close_result = usb_host_device_close(s_client, device);
    if (close_result != ESP_OK) ESP_LOGW(k_tag, "device close failed: %s", esp_err_to_name(close_result));
    facts.valid = true;
    ESP_LOGI(k_tag, "device addr=%u usb=%04x vid=%04x pid=%04x spec=%u play=%u rec=%u ac=%d", address,
             facts.usb_version, facts.vendor_id, facts.product_id, facts.audio_spec, facts.playback_streams,
             facts.recording_streams, facts.audio_control ? 1 : 0);
    return true;
}

void handle_new_device(std::uint8_t address) {
    descriptor_state facts{};
    if (!probe_descriptors(address, facts)) {
        set_status(state::error, ESP_ERR_NOT_FOUND);
        return;
    }
    mutate([&facts]() {
        s_descriptors = facts;
        /* Keep the class-driver facts only while they belong to this device. */
        if (s_stream.address != facts.address) s_stream = stream_state{};
        if (s_stream.format_count != 0) {
            s_status = state::ready;
            s_error_code = 0;
        } else if (facts.audio_spec >= 2 || (!facts.audio_control && facts.playback_streams == 0)) {
            s_status = state::unsupported;
            s_error_code = 0;
        } else {
            s_status = state::waiting;
            s_error_code = 0;
        }
    });
    if (facts.audio_spec >= 2)
        ESP_LOGW(k_tag, "device declares UAC %u; only UAC 1.0 playback is implemented", facts.audio_spec);
    else if (!facts.audio_control && facts.playback_streams == 0)
        ESP_LOGW(k_tag, "device does not expose a UAC playback interface");
}

bool inspect_device(const host_event &event) {
    set_status(state::inspecting);
    uac_host_device_config_t config{};
    config.addr = event.address;
    config.iface_num = event.interface_number;
    config.buffer_size = k_device_buffer_bytes;
    config.buffer_threshold = k_device_buffer_threshold;
    config.callback = device_event;

    uac_host_device_handle_t handle = nullptr;
    esp_err_t result = uac_host_device_open(&config, &handle);
    if (result != ESP_OK) {
        ESP_LOGW(k_tag, "uac open failed: %s", esp_err_to_name(result));
        set_status(state::error, static_cast<std::uint32_t>(result));
        return false;
    }

    uac_host_dev_info_t info{};
    result = uac_host_get_device_info(handle, &info);
    stream_state stream{};
    stream.address = event.address;
    stream.interface_number = event.interface_number;
    copy_ascii(stream.manufacturer, sizeof(stream.manufacturer), info.iManufacturer, UAC_STR_DESC_MAX_LENGTH);
    copy_ascii(stream.product, sizeof(stream.product), info.iProduct, UAC_STR_DESC_MAX_LENGTH);

    if (result == ESP_OK) {
        for (std::uint8_t alternate = 1; alternate <= info.iface_alt_num && stream.format_count < k_max_formats;
             ++alternate) {
            uac_host_dev_alt_param_t params{};
            if (uac_host_get_device_alt_param(handle, alternate, &params) != ESP_OK || params.format != 1 ||
                params.channels == 0 || params.bit_resolution == 0)
                continue;
            audio_format &format = stream.formats[stream.format_count++];
            format.channels = params.channels;
            format.subframe_bytes = params.subframe_size;
            format.bit_resolution = params.bit_resolution;
            if (params.sample_freq_type == 0) {
                format.sample_rate_min = params.sample_freq_lower;
                format.sample_rate_max = params.sample_freq_upper;
            } else {
                std::uint32_t low = UINT32_MAX;
                std::uint32_t high = 0;
                for (std::size_t index = 0; index < UAC_FREQ_NUM_MAX; ++index) {
                    const std::uint32_t rate = params.sample_freq[index];
                    if (rate == 0) break;
                    low = std::min(low, rate);
                    high = std::max(high, rate);
                }
                if (high != 0) {
                    format.sample_rate_min = low;
                    format.sample_rate_max = high;
                }
            }
        }
    }
    const esp_err_t close_result = uac_host_device_close(handle);
    if (result != ESP_OK || close_result != ESP_OK) {
        ESP_LOGW(k_tag, "uac info failed: %s", esp_err_to_name(result != ESP_OK ? result : close_result));
        set_status(state::error, static_cast<std::uint32_t>(result != ESP_OK ? result : close_result));
        return false;
    }
    ESP_LOGI(k_tag, "uac formats=%u channels=%u bits=%u", static_cast<unsigned>(stream.format_count),
             stream.format_count != 0 ? stream.formats[0].channels : 0,
             stream.format_count != 0 ? stream.formats[0].bit_resolution : 0);
    mutate([&stream]() {
        s_stream = stream;
        s_status = stream.format_count == 0 ? state::unsupported : state::ready;
        s_error_code = 0;
    });
    return true;
}

void worker_task(void *) {
    while (true) {
        /* Pump the passive client first; its events are what prove a device is
         * physically present. */
        if (s_client != nullptr) (void)usb_host_client_handle_events(s_client, 0);
        host_event event{};
        if (xQueueReceive(s_events, &event, k_worker_poll_ticks) != pdTRUE) continue;
        if (event.kind == event_kind::stop) break;
        if (event.kind == event_kind::disconnected || event.kind == event_kind::device_gone) {
            mutate([]() {
                clear_device_facts();
                s_status = state::waiting;
                s_error_code = 0;
            });
            continue;
        }
        if (event.kind == event_kind::device_new) {
            handle_new_device(event.address);
            continue;
        }
        (void)inspect_device(event);
    }
    s_worker = nullptr;
    if (s_worker_stopped != nullptr) xSemaphoreGive(s_worker_stopped);
    vTaskDelete(nullptr);
}

bool send_stop_event() {
    const host_event stop{event_kind::stop, 0, 0};
    if (xQueueSend(s_events, &stop, 0) == pdTRUE) return true;
    (void)xQueueReset(s_events);
    return xQueueSend(s_events, &stop, 0) == pdTRUE;
}

bool playback_available() {
    snapshot value{};
    if (!get_snapshot(value)) return false;
    return value.status == state::ready && value.format_count != 0;
}

bool playback_start(std::uint32_t sample_rate, std::uint8_t channels) {
    if (s_playback_mutex == nullptr) s_playback_mutex = xSemaphoreCreateMutex();
    if (s_playback_mutex == nullptr || xSemaphoreTake(s_playback_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool started = false;
    if (s_playback_handle == nullptr) {
        snapshot value{};
        if (get_snapshot(value) && value.status == state::ready &&
            format_supports(value, sample_rate, channels)) {
            const uac_host_device_config_t config{
                .addr = value.address,
                .iface_num = value.interface_number,
                .buffer_size = k_playback_buffer_bytes,
                .buffer_threshold = k_playback_buffer_bytes / 2,
                .callback = playback_device_event,
                .callback_arg = nullptr,
            };
            uac_host_device_handle_t handle = nullptr;
            esp_err_t result = uac_host_device_open(&config, &handle);
            if (result == ESP_OK) {
                const uac_host_stream_config_t stream{
                    .channels = channels,
                    .bit_resolution = k_playback_bit_resolution,
                    .sample_freq = sample_rate,
                    .flags = 0,
                };
                result = uac_host_device_start(handle, &stream);
                if (result == ESP_OK) {
                    s_playback_handle = handle;
                    s_playback_rate = sample_rate;
                    s_playback_channels = channels;
                    s_device_volume = false;
                    started = true;
                    ESP_LOGI(k_tag, "playback stream started rate=%lu channels=%u",
                             static_cast<unsigned long>(sample_rate), channels);
                } else {
                    ESP_LOGW(k_tag, "stream start failed: %s", esp_err_to_name(result));
                    (void)uac_host_device_close(handle);
                }
            } else {
                ESP_LOGW(k_tag, "playback open failed: %s", esp_err_to_name(result));
            }
        }
    }
    xSemaphoreGive(s_playback_mutex);
    return started;
}

bool playback_set_volume(std::uint8_t volume) {
    if (volume > k_player_max_volume) return false;
    const std::uint32_t percent = static_cast<std::uint32_t>(volume) * 100 / k_player_max_volume;
    s_playback_gain_q15 = percent == 0 ? 0 : (percent * 32767) / 100;
    if (s_playback_handle != nullptr) {
        const esp_err_t result = uac_host_device_set_volume(s_playback_handle, static_cast<std::uint8_t>(percent));
        const bool supported = result == ESP_OK;
        if (!supported && result != ESP_ERR_NOT_SUPPORTED)
            ESP_LOGW(k_tag, "device volume failed: %s", esp_err_to_name(result));
        if (supported != s_device_volume) {
            s_device_volume = supported;
            ESP_LOGI(k_tag, "volume path=%s percent=%lu", supported ? "feature-unit" : "digital",
                     static_cast<unsigned long>(percent));
        }
    }
    return true;
}

bool playback_write(const std::int16_t *samples, std::size_t sample_count) {
    if (s_playback_handle == nullptr || samples == nullptr || sample_count == 0 ||
        sample_count > k_playback_max_samples)
        return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(sample_count * sizeof(std::int16_t));
    auto *payload = reinterpret_cast<std::uint8_t *>(const_cast<std::int16_t *>(samples));
    if (!s_device_volume && s_playback_gain_q15 < 32767) {
        for (std::size_t index = 0; index < sample_count; ++index) {
            const std::int32_t scaled =
                (static_cast<std::int32_t>(samples[index]) * static_cast<std::int32_t>(s_playback_gain_q15)) >> 15;
            s_scaled_samples[index] = static_cast<std::int16_t>(scaled);
        }
        payload = reinterpret_cast<std::uint8_t *>(s_scaled_samples);
    }
    /* The decoder runs far faster than the isochronous clock, so the driver
     * ring buffer fills and write() reports a busy timeout. Blocking here is
     * what paces playback to the device; returning that as a failure made the
     * player abort after under a second of audio. */
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(k_playback_write_wait_ms);
    while (s_playback_handle != nullptr) {
        const esp_err_t result = uac_host_device_write(s_playback_handle, payload, bytes,
                                                      pdMS_TO_TICKS(k_playback_write_timeout_ms));
        if (result == ESP_OK) return true;
        if (result != ESP_ERR_TIMEOUT && result != ESP_FAIL) return false;
        if (static_cast<std::int32_t>(xTaskGetTickCount() - deadline) >= 0) return false;
        vTaskDelay(1);
    }
    return false;
}

void playback_stop() {
    if (s_playback_mutex == nullptr || xSemaphoreTake(s_playback_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    if (s_playback_handle != nullptr) {
        (void)uac_host_device_stop(s_playback_handle);
        (void)uac_host_device_close(s_playback_handle);
        s_playback_handle = nullptr;
        ESP_LOGI(k_tag, "playback stream stopped");    }
    s_playback_rate = 0;
    s_playback_channels = 0;
    xSemaphoreGive(s_playback_mutex);
}

void install_output_hooks() {
    const cyberdeck_audio_output::usb_output_hooks hooks{playback_available, playback_start, playback_set_volume,
                                                         playback_write, playback_stop};
    if (!cyberdeck_audio_output::install_usb_output(hooks))
        ESP_LOGW(k_tag, "audio output already open; USB route not installed");
}

void remove_output_hooks() {
    (void)cyberdeck_audio_output::install_usb_output(cyberdeck_audio_output::usb_output_hooks{});
}

void cleanup_start_failure() {
    if (s_client != nullptr) {
        (void)usb_host_client_deregister(s_client);
        s_client = nullptr;
    }
    if (s_uac_started) {
        (void)uac_host_uninstall();
        s_uac_started = false;
    }
    if (s_host_started) {
        (void)bsp_usb_host_stop();
        s_host_started = false;
    }
}
} // namespace

bool start() {
    if (s_worker != nullptr) return true;
    if (s_state_mutex == nullptr) s_state_mutex = xSemaphoreCreateMutex();
    if (s_events == nullptr) s_events = xQueueCreate(k_event_depth, sizeof(host_event));
    if (s_worker_stopped == nullptr) s_worker_stopped = xSemaphoreCreateBinary();
    if (s_state_mutex == nullptr || s_events == nullptr || s_worker_stopped == nullptr) {
        set_status(state::error, ESP_ERR_NO_MEM);
        return false;
    }
    (void)xQueueReset(s_events);
    set_status(state::starting);
    if (bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true) != ESP_OK) {
        set_status(state::error, ESP_FAIL);
        return false;
    }
    s_host_started = true;

    const uac_host_driver_config_t config{
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = k_usb_task_stack_bytes,
        .core_id = tskNO_AFFINITY,
        .callback = driver_event,
        .callback_arg = nullptr,
    };
    const esp_err_t install_result = uac_host_install(&config);
    if (install_result != ESP_OK) {
        ESP_LOGE(k_tag, "uac_host_install failed: %s", esp_err_to_name(install_result));
        set_status(state::error, static_cast<std::uint32_t>(install_result));
        cleanup_start_failure();
        return false;
    }
    s_uac_started = true;

    const usb_host_client_config_t client_config{
        .is_synchronous = false,
        .max_num_event_msg = k_client_queue_depth,
        .flags = {},
        .async =
            {
                .client_event_callback = client_event,
                .callback_arg = nullptr,
            },
    };
    const esp_err_t client_result = usb_host_client_register(&client_config, &s_client);
    if (client_result != ESP_OK) {
        ESP_LOGE(k_tag, "usb_host_client_register failed: %s", esp_err_to_name(client_result));
        s_client = nullptr;
        set_status(state::error, static_cast<std::uint32_t>(client_result));
        cleanup_start_failure();
        return false;
    }

    if (xTaskCreate(worker_task, "usb_audio_info", k_worker_stack_bytes, nullptr,
                    tskIDLE_PRIORITY + 2, &s_worker) != pdPASS) {
        s_worker = nullptr;
        set_status(state::error, ESP_ERR_NO_MEM);
        cleanup_start_failure();
        return false;
    }
    set_status(state::waiting);
    install_output_hooks();
    return true;
}

bool stop(std::uint32_t timeout_ms) {
    playback_stop();
    remove_output_hooks();
    if (s_worker != nullptr) {
        if (!send_stop_event() || xSemaphoreTake(s_worker_stopped, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
            return false;
    }
    if (s_client != nullptr) {
        const esp_err_t result = usb_host_client_deregister(s_client);
        if (result != ESP_OK) return false;
        s_client = nullptr;
    }
    if (s_uac_started) {
        const esp_err_t result = uac_host_uninstall();
        if (result != ESP_OK) return false;
        s_uac_started = false;
    }
    if (s_host_started) {
        const esp_err_t result = bsp_usb_host_stop();
        if (result != ESP_OK) return false;
        s_host_started = false;
    }
    set_status(state::stopped);
    return true;
}

bool get_snapshot(snapshot &out) {
    if (s_state_mutex == nullptr || xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    out = s_snapshot;
    xSemaphoreGive(s_state_mutex);
    return true;
}

} // namespace cyberdeck_usb_audio_host
