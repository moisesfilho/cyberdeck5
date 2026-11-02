#include "platform/audio/cyberdeck_audio_output.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
extern "C" std::int64_t esp_timer_get_time(void);
#endif
#include <algorithm>

namespace cyberdeck_audio_output {
namespace {
void *codec = nullptr;
bool opened = false;
bool amplifier = false;
bool route_fault = false;
std::uint32_t active_rate = 0;
std::uint8_t active_channels = 0;
std::uint8_t active_volume = 0;
TickType_t last_route_check = 0;
#ifndef ESP_PLATFORM
clock_us_fn test_clock = nullptr;
#endif

std::uint64_t monotonic_us() {
#ifndef ESP_PLATFORM
    if (test_clock != nullptr)
        return test_clock();
#endif
    return static_cast<std::uint64_t>(esp_timer_get_time());
}

void *default_initialize(void *) {
    return bsp_audio_codec_speaker_init();
}
bool default_mute(void *, void *handle, bool enabled) {
    return esp_codec_dev_set_out_mute(static_cast<esp_codec_dev_handle_t>(handle), enabled) == ESP_CODEC_DEV_OK;
}
bool default_open(void *, void *handle, std::uint32_t rate, std::uint8_t channels) {
    esp_codec_dev_sample_info_t info{};
    info.bits_per_sample = 16;
    info.channel = channels;
    info.sample_rate = rate;
    info.mclk_multiple = 0;
    return esp_codec_dev_open(static_cast<esp_codec_dev_handle_t>(handle), &info) == ESP_CODEC_DEV_OK;
}
bool default_set_volume(void *, void *handle, std::uint8_t volume) {
    return esp_codec_dev_set_out_vol(static_cast<esp_codec_dev_handle_t>(handle), volume) == ESP_CODEC_DEV_OK;
}
bool default_write(void *, void *handle, const std::int16_t *samples, std::size_t count) {
    return esp_codec_dev_write(static_cast<esp_codec_dev_handle_t>(handle), const_cast<std::int16_t *>(samples),
                               static_cast<int>(count * sizeof(std::int16_t))) == ESP_CODEC_DEV_OK;
}
bool default_close(void *, void *handle) {
    return esp_codec_dev_close(static_cast<esp_codec_dev_handle_t>(handle)) == ESP_CODEC_DEV_OK;
}
bool default_headphone_connected(void *) {
    return bsp_headphone_is_connected();
}
bool default_set_amplifier(void *, bool enabled) {
    return bsp_feature_enable(BSP_FEATURE_SPEAKER, enabled) == ESP_OK;
}

backend active_backend{nullptr,       default_initialize,          default_mute,
                       default_open,  default_set_volume,          default_write,
                       default_close, default_headphone_connected, default_set_amplifier};

bool update_route(bool force = false, bool *attempted = nullptr, std::uint64_t *elapsed_us = nullptr) {
    if (!opened)
        return false;
    const TickType_t now = xTaskGetTickCount();
    if (!force && !route_fault && static_cast<TickType_t>(now - last_route_check) < pdMS_TO_TICKS(500))
        return true;
    if (attempted != nullptr)
        *attempted = true;
    const auto started_us = monotonic_us();
    last_route_check = now;
    const bool need_amplifier = !active_backend.headphone_connected(active_backend.context);
    if (need_amplifier == amplifier) {
        route_fault = false;
        if (elapsed_us != nullptr) {
            const auto ended_us = monotonic_us();
            *elapsed_us = ended_us >= started_us ? ended_us - started_us : 0;
        }
        return true;
    }
    if (!active_backend.set_speaker_amplifier(active_backend.context, need_amplifier)) {
        route_fault = true;
        if (elapsed_us != nullptr) {
            const auto ended_us = monotonic_us();
            *elapsed_us = ended_us >= started_us ? ended_us - started_us : 0;
        }
        return false;
    }
    amplifier = need_amplifier;
    route_fault = false;
    if (elapsed_us != nullptr) {
        const auto ended_us = monotonic_us();
        *elapsed_us = ended_us >= started_us ? ended_us - started_us : 0;
    }
    return true;
}
} // namespace

bool install_backend(const backend &device) {
    if (codec != nullptr || opened || device.initialize == nullptr || device.mute == nullptr ||
        device.open == nullptr || device.set_volume == nullptr || device.write == nullptr || device.close == nullptr ||
        device.headphone_connected == nullptr || device.set_speaker_amplifier == nullptr)
        return false;
    active_backend = device;
    return true;
}

bool start(std::uint32_t rate, std::uint8_t channels, std::uint8_t volume, route_timing *timing) {
    if (timing != nullptr)
        *timing = {};
    if (rate < 8000 || rate > 48000 || (channels != 1 && channels != 2) || volume > 50)
        return false;
    if (codec != nullptr && opened && active_rate == rate && active_channels == channels) {
        /* Callers re-enter start() once per audio frame: keep this path free of
         * codec I2C traffic (volume write + jack probe) unless something really
         * changed, otherwise the bus contention starves the DMA and the output
         * stutters. */
        bool route_attempted = false;
        std::uint64_t route_elapsed_us = 0;
        const bool route_ok = update_route(false, timing != nullptr ? &route_attempted : nullptr,
                                           timing != nullptr ? &route_elapsed_us : nullptr);
        if (timing != nullptr && route_attempted) {
            timing->attempted = true;
            timing->elapsed_us = route_elapsed_us;
        }
        if (!route_ok)
            return false;
        if (volume == active_volume)
            return true;
        if (!active_backend.set_volume(active_backend.context, codec, volume))
            return false;
        active_volume = volume;
        return true;
    }
    stop();
    if (amplifier || route_fault)
        return false;
    if (codec == nullptr)
        codec = active_backend.initialize(active_backend.context);
    if (codec == nullptr || !active_backend.mute(active_backend.context, codec, true))
        return false;
    if (!active_backend.open(active_backend.context, codec, rate, channels))
        return false;
    opened = true;
    active_rate = rate;
    active_channels = channels;
    if (!active_backend.set_volume(active_backend.context, codec, volume)) {
        stop();
        return false;
    }
    active_volume = volume;
    bool route_attempted = false;
    std::uint64_t route_elapsed_us = 0;
    const bool route_ok = update_route(true, timing != nullptr ? &route_attempted : nullptr,
                                       timing != nullptr ? &route_elapsed_us : nullptr);
    if (timing != nullptr && route_attempted) {
        timing->attempted = true;
        timing->elapsed_us = route_elapsed_us;
    }
    if (!route_ok) {
        stop();
        return false;
    }
    if (!active_backend.mute(active_backend.context, codec, false)) {
        stop();
        return false;
    }
    return true;
}

bool write(const std::int16_t *samples, std::size_t count, write_timing *timing) {
    if (timing != nullptr)
        *timing = {};
    if (codec == nullptr || !opened)
        return false;
    bool route_attempted = false;
    std::uint64_t route_elapsed_us = 0;
    const bool route_ok = update_route(false, timing != nullptr ? &route_attempted : nullptr,
                                       timing != nullptr ? &route_elapsed_us : nullptr);
    if (route_attempted && timing != nullptr) {
        timing->route.attempted = true;
        timing->route.elapsed_us = route_elapsed_us;
    }
    if (!route_ok || samples == nullptr || count == 0 || count > 2304)
        return false;
    const auto backend_started = timing != nullptr ? monotonic_us() : 0;
    const bool result = active_backend.write(active_backend.context, codec, samples, count);
    if (timing != nullptr) {
        timing->backend_attempted = true;
        const auto ended = monotonic_us();
        timing->backend_us = ended >= backend_started ? ended - backend_started : 0;
    }
    return result;
}

#ifndef ESP_PLATFORM
void set_clock_for_test(clock_us_fn clock) { test_clock = clock; }
#endif

bool set_volume(std::uint8_t volume) {
    if (codec == nullptr || !opened || route_fault || volume > 50)
        return false;
    if (!active_backend.set_volume(active_backend.context, codec, volume))
        return false;
    active_volume = volume;
    return true;
}

void stop() {
    if (codec != nullptr)
        (void)active_backend.mute(active_backend.context, codec, true);
    if (amplifier || route_fault) {
        const bool amplifier_disabled = active_backend.set_speaker_amplifier(active_backend.context, false);
        amplifier = !amplifier_disabled;
        route_fault = !amplifier_disabled;
    }
    if (codec != nullptr && opened)
        (void)active_backend.close(active_backend.context, codec);
    opened = false;
    active_rate = 0;
    active_channels = 0;
    active_volume = 0;
}
} // namespace cyberdeck_audio_output
