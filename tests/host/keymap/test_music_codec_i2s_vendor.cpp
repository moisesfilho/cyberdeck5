#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "audio_codec_data_if.h"
#include "audio_codec_sw_vol.h"
#include "codec_dev_mirror.h"
#include "driver/i2s_test_control.h"
#include "driver/i2s_std.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
int checks;
int failures;
bool acceptance_mode;
int codec_setup_calls;
int codec_enable_calls;
int codec_disable_calls;
int codec_last_rate;
int codec_last_channels;
int codec_last_bits;
int codec_next_enable_error;
int codec_disable_error;

void check(bool condition, const char *description) {
    ++checks;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", description);
}

void expect_acceptance(bool condition, const char *description) {
    if (!acceptance_mode) return;
    check(condition, description);
}

int codec_set_fs(const audio_codec_if_t *, esp_codec_dev_sample_info_t *format) {
    music_i2s_record_call(MUSIC_I2S_CALL_CODEC_SETUP);
    ++codec_setup_calls;
    codec_last_rate = format->sample_rate;
    codec_last_channels = format->channel;
    codec_last_bits = format->bits_per_sample;
    return ESP_CODEC_DEV_OK;
}

int codec_enable(const audio_codec_if_t *, bool enabled) {
    music_i2s_record_call(enabled ? MUSIC_I2S_CALL_CODEC_ENABLE : MUSIC_I2S_CALL_CODEC_DISABLE);
    enabled ? ++codec_enable_calls : ++codec_disable_calls;
    if (!enabled && codec_disable_error != ESP_CODEC_DEV_OK) {
        const int result = codec_disable_error;
        codec_disable_error = ESP_CODEC_DEV_OK;
        return result;
    }
    const int result = codec_next_enable_error;
    codec_next_enable_error = ESP_CODEC_DEV_OK;
    return result;
}

const audio_codec_if_t codec_if = {
    .open = nullptr,
    .is_open = nullptr,
    .enable = codec_enable,
    .set_fs = codec_set_fs,
    .mute = nullptr,
    .set_vol = nullptr,
    .set_mic_gain = nullptr,
    .set_mic_channel_gain = nullptr,
    .mute_mic = nullptr,
    .set_reg = nullptr,
    .get_reg = nullptr,
    .dump_reg = nullptr,
    .close = nullptr,
};

const audio_codec_data_if_t *make_data_if(int *channel_token) {
    music_i2s_configure_channel(channel_token, I2S_DIR_TX, nullptr, 0);
    audio_codec_i2s_cfg_t cfg{};
    cfg.port = 0;
    cfg.tx_handle = channel_token;
    return audio_codec_new_i2s_data(&cfg);
}

const audio_codec_data_if_t *make_duplex_data_if(int *rx, int *tx) {
    music_i2s_configure_channel(rx, I2S_DIR_RX, tx, 0);
    music_i2s_configure_channel(tx, I2S_DIR_TX, rx, 0);
    audio_codec_i2s_cfg_t cfg{};
    cfg.port = 1;
    cfg.rx_handle = rx;
    cfg.tx_handle = tx;
    return audio_codec_new_i2s_data(&cfg);
}

esp_codec_dev_sample_info_t sample_format() {
    esp_codec_dev_sample_info_t format{};
    format.sample_rate = 48000;
    format.channel = 2;
    format.bits_per_sample = 16;
    return format;
}

void reset_observers() {
    music_i2s_reset();
    codec_setup_calls = codec_enable_calls = codec_disable_calls = 0;
    codec_last_rate = codec_last_channels = codec_last_bits = 0;
    codec_next_enable_error = ESP_CODEC_DEV_OK;
    codec_disable_error = ESP_CODEC_DEV_OK;
}

void test_public_retry_with_distinct_format() {
    int token = 39;
    const auto *data = make_data_if(&token);
    check(data != nullptr, "public retry data interface constructed");
    if (!data) return;
    esp_codec_dev_cfg_t config{};
    config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    config.codec_if = &codec_if;
    config.data_if = data;
    auto dev = esp_codec_dev_new(&config);
    check(dev != nullptr, "public retry codec constructed");
    if (!dev) { data->close(data); return; }

    auto first = sample_format();
    first.sample_rate = 32000;
    first.bits_per_sample = 16;
    reset_observers();
    music_i2s_fail_next_slot(ESP_FAIL);
    const int first_result = esp_codec_dev_open(dev, &first);
    expect_acceptance(first_result != ESP_CODEC_DEV_OK && codec_setup_calls == 0,
                      "public open stops before codec callback after I2S format failure");
    esp_codec_dev_close(dev);

    auto retry = sample_format();
    retry.sample_rate = 44100;
    retry.bits_per_sample = 24;
    reset_observers();
    const int retry_result = esp_codec_dev_open(dev, &retry);
    expect_acceptance(retry_result == ESP_CODEC_DEV_OK && codec_setup_calls == 1 &&
                          codec_last_rate == 44100 && codec_last_channels == 2 &&
                          codec_last_bits == 24,
                      "public retry callback observes distinct format, not prior request");
    expect_acceptance(music_i2s_trace_size() == 5 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK &&
                          music_i2s_trace_at(2) == MUSIC_I2S_CALL_ENABLE &&
                          music_i2s_trace_at(3) == MUSIC_I2S_CALL_CODEC_SETUP &&
                          music_i2s_trace_at(4) == MUSIC_I2S_CALL_CODEC_ENABLE,
                      "distinct-format retry reconfigures and opens through public API");
    expect_acceptance(esp_codec_dev_close(dev) == ESP_CODEC_DEV_OK && codec_disable_calls == 1,
                      "distinct-format retry closes cleanly through public API");
    esp_codec_dev_delete(dev);
    data->close(data);
}

void test_current_vendor_characterization() {
    int channel_token = 1;
    const audio_codec_data_if_t *data = make_data_if(&channel_token);
    check(data != nullptr, "vendor data interface constructed");
    if (!data) return;

    auto format = sample_format();
    reset_observers();
    music_i2s_fail_next_disable_for(&channel_token, ESP_FAIL);
    const int result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    check(result == ESP_CODEC_DEV_OK, "inactive format setup succeeds");
    check(music_i2s_trace_size() == 2 && music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
              music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK,
          "inactive format setup skips disable and configures slot then clock");

    reset_observers();
    music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 1);
    music_i2s_fail_next_disable_for(&channel_token, ESP_FAIL);
    const int failed_disable = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    check(failed_disable != ESP_CODEC_DEV_OK && music_i2s_channel_active_for(&channel_token),
          "active disable failure propagates and preserves channel state");
    check(music_i2s_trace_size() == 1 && music_i2s_trace_at(0) == MUSIC_I2S_CALL_DISABLE,
          "active disable failure short-circuits reconfiguration");

    data->close(data);
}

void test_acceptance_active_order() {
    int channel_token = 2;
    const audio_codec_data_if_t *data = make_data_if(&channel_token);
    check(data != nullptr, "active-order data interface constructed");
    if (!data) return;
    auto format = sample_format();
    reset_observers();
    music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 1);
    const int format_result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    expect_acceptance(format_result == ESP_CODEC_DEV_OK, "active format setup succeeds");
    expect_acceptance(music_i2s_trace_size() == 3 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_DISABLE &&
                          music_i2s_trace_handle_at(0) == &channel_token &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_handle_at(1) == &channel_token &&
                          music_i2s_trace_at(2) == MUSIC_I2S_CALL_CLOCK &&
                          music_i2s_trace_handle_at(2) == &channel_token,
                      "active reconfiguration order is disable -> slot -> clock");
    const int enable_result = data->enable(data, ESP_CODEC_DEV_TYPE_OUT, true);
    expect_acceptance(enable_result == ESP_CODEC_DEV_OK && music_i2s_channel_active(),
                      "successful reconfiguration re-enables channel");
    expect_acceptance(music_i2s_trace_size() == 4 &&
                          music_i2s_trace_at(3) == MUSIC_I2S_CALL_ENABLE,
                      "active sequence ends with enable");

    reset_observers();
    format = sample_format();
    expect_acceptance(data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format) == ESP_CODEC_DEV_OK,
                      "inactive channel format setup succeeds");
    expect_acceptance(music_i2s_disable_calls() == 0,
                      "inactive OUT channel avoids disable call");
    data->close(data);
}

void test_acceptance_faults() {
    int channel_token = 3;
    const audio_codec_data_if_t *data = make_data_if(&channel_token);
    check(data != nullptr, "fault-test data interface constructed");
    if (!data) return;
    auto format = sample_format();

    reset_observers();
    music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 1);
    music_i2s_fail_next_disable_for(&channel_token, ESP_FAIL);
    int result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "disable failure propagates from set_fmt");
    expect_acceptance(music_i2s_channel_active(), "disable failure preserves active state");
    expect_acceptance(music_i2s_trace_size() == 1 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_DISABLE,
                      "disable failure prevents slot and clock reconfiguration");

    reset_observers();
    format = sample_format();
    music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 0);
    music_i2s_fail_next_slot(ESP_FAIL);
    result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "slot failure propagates from set_fmt");
    expect_acceptance(music_i2s_trace_size() == 1 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT,
                      "slot failure prevents clock reconfiguration");
    expect_acceptance(!music_i2s_channel_active(), "slot failure leaves channel disabled");

    reset_observers();
    format = sample_format();
    music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 0);
    music_i2s_fail_next_clock(ESP_FAIL);
    result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_OUT, &format);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "clock failure propagates from set_fmt");
    expect_acceptance(music_i2s_trace_size() == 2 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK,
                      "clock failure occurs after slot");
    expect_acceptance(music_i2s_clock_calls() == 1 && !music_i2s_channel_active(),
                      "clock failure leaves channel disabled after one clock attempt");

    reset_observers();
    music_i2s_fail_next_enable(ESP_FAIL);
    result = data->enable(data, ESP_CODEC_DEV_TYPE_OUT, true);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "enable failure propagates from data interface");
    expect_acceptance(!music_i2s_channel_active(), "enable failure leaves channel inactive");
    expect_acceptance(music_i2s_trace_size() == 1 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_ENABLE,
                      "enable failure has only the enable operation");
    data->close(data);
}

void test_codec_open_failure(int operation) {
    int channel_token = operation + 10;
    const audio_codec_data_if_t *data = make_data_if(&channel_token);
    check(data != nullptr, "codec-open data interface constructed");
    if (!data) return;
    esp_codec_dev_cfg_t config{};
    config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    config.codec_if = &codec_if;
    config.data_if = data;
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&config);
    check(dev != nullptr, "real codec device constructed");
    if (!dev) {
        data->close(data);
        return;
    }

    reset_observers();
    auto format = sample_format();
    if (operation == MUSIC_I2S_CALL_DISABLE) {
        music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 1);
        music_i2s_fail_next_disable_for(&channel_token, ESP_FAIL);
    } else if (operation == MUSIC_I2S_CALL_SLOT) {
        music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 0);
        music_i2s_fail_next_slot(ESP_FAIL);
    } else if (operation == MUSIC_I2S_CALL_CLOCK) {
        music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 0);
        music_i2s_fail_next_clock(ESP_FAIL);
    } else {
        music_i2s_configure_channel(&channel_token, I2S_DIR_TX, nullptr, 0);
        music_i2s_fail_next_enable(ESP_FAIL);
    }

    const int failed_open = esp_codec_dev_open(dev, &format);
    expect_acceptance(failed_open != ESP_CODEC_DEV_OK,
                      "codec open propagates injected I2S operation failure");
    expect_acceptance(codec_setup_calls == 0 && codec_enable_calls == 0,
                      "failed I2S setup does not configure or enable codec");
    expect_acceptance(music_i2s_channel_active_for(&channel_token) == (operation == MUSIC_I2S_CALL_DISABLE),
                      "open failure preserves expected I2S active state");
    if (operation == MUSIC_I2S_CALL_DISABLE) {
        expect_acceptance(music_i2s_trace_size() == 1 &&
                              music_i2s_trace_at(0) == MUSIC_I2S_CALL_DISABLE,
                          "disable failure stops codec open before later operations");
    }

    reset_observers();
    const int failed_close = esp_codec_dev_close(dev);
    expect_acceptance(failed_close == ESP_CODEC_DEV_OK && codec_disable_calls == 0,
                      "close after failed open is safe and does not disable unopened codec");

    reset_observers();
    format = sample_format();
    const int retry_open = esp_codec_dev_open(dev, &format);
    expect_acceptance(retry_open == ESP_CODEC_DEV_OK,
                      "open after failed attempt retries configuration");
    expect_acceptance(codec_setup_calls == 1 && codec_enable_calls == 1,
                      "successful retry reaches codec setup and enable");
    expect_acceptance(music_i2s_trace_size() == 5 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK &&
                          music_i2s_trace_at(2) == MUSIC_I2S_CALL_ENABLE &&
                          music_i2s_trace_at(3) == MUSIC_I2S_CALL_CODEC_SETUP &&
                          music_i2s_trace_at(4) == MUSIC_I2S_CALL_CODEC_ENABLE,
                      "inactive codec open skips I2S disable before slot-clock-enable");
    const int close_result = esp_codec_dev_close(dev);
    expect_acceptance(close_result == ESP_CODEC_DEV_OK && codec_disable_calls == 1,
                      "successful retry closes codec and cleans up once");
    expect_acceptance(music_i2s_trace_size() == 7 &&
                          music_i2s_trace_at(5) == MUSIC_I2S_CALL_CODEC_DISABLE &&
                          music_i2s_trace_at(6) == MUSIC_I2S_CALL_DISABLE,
                      "close cleanup trace disables codec then I2S once");
    esp_codec_dev_delete(dev);
    data->close(data);
}

void test_acceptance_codec_open_rollback() {
    test_codec_open_failure(MUSIC_I2S_CALL_DISABLE);
    test_codec_open_failure(MUSIC_I2S_CALL_SLOT);
    test_codec_open_failure(MUSIC_I2S_CALL_CLOCK);
    test_codec_open_failure(MUSIC_I2S_CALL_ENABLE);
}

void test_cleanup_failures_are_retryable() {
    reset_observers();
    int token = 81;
    const auto *data = make_data_if(&token);
    esp_codec_dev_cfg_t config{};
    config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    config.codec_if = &codec_if;
    config.data_if = data;
    auto dev = esp_codec_dev_new(&config);
    auto format = sample_format();
    check(data && dev && esp_codec_dev_open(dev, &format) == ESP_CODEC_DEV_OK,
          "cleanup retry fixture opens");
    if (!dev) { if (data) data->close(data); return; }

    music_i2s_clear_trace();
    music_i2s_fail_next_disable_for(&token, ESP_FAIL);
    const int delete_error = esp_codec_dev_delete(dev);
    expect_acceptance(delete_error != ESP_CODEC_DEV_OK,
                      "delete propagates close failure and retains the handle");
    expect_acceptance(esp_codec_dev_delete(dev) == ESP_CODEC_DEV_OK,
                      "retained handle can be deleted after cleanup retry");
    expect_acceptance(codec_disable_calls == 1 && music_i2s_trace_size() == 3 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_CODEC_DISABLE &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_DISABLE &&
                          music_i2s_trace_at(2) == MUSIC_I2S_CALL_DISABLE,
                      "successful cleanup stage is not repeated while failed stage retries");
    data->close(data);

    token = 82;
    const auto *rollback_data = make_data_if(&token);
    config.data_if = rollback_data;
    dev = esp_codec_dev_new(&config);
    reset_observers();
    codec_next_enable_error = ESP_CODEC_DEV_DRV_ERR;
    codec_disable_error = ESP_CODEC_DEV_DRV_ERR;
    format = sample_format();
    const int open_error = esp_codec_dev_open(dev, &format);
    expect_acceptance(open_error != ESP_CODEC_DEV_OK && codec_setup_calls == 1,
                      "codec enable error rolls back without reporting open success");
    expect_acceptance(esp_codec_dev_delete(dev) == ESP_CODEC_DEV_OK &&
                          codec_disable_calls == 2,
                      "failed codec rollback remains owned and delete retries it");
    rollback_data->close(rollback_data);
}

void test_duplex_disable_both_and_preserve_first_error() {
    int rx = 41, tx = 42;
    const auto *data = make_duplex_data_if(&rx, &tx);
    check(data != nullptr, "duplex data interface constructed");
    if (!data) return;
    music_i2s_reset();
    music_i2s_configure_channel(&rx, I2S_DIR_RX, &tx, 1);
    music_i2s_configure_channel(&tx, I2S_DIR_TX, &rx, 1);
    music_i2s_fail_next_disable_for(&tx, ESP_FAIL);
    auto format = sample_format();
    const int result = data->set_fmt(data, ESP_CODEC_DEV_TYPE_IN_OUT, &format);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "duplex set_fmt propagates TX disable error");
    expect_acceptance(music_i2s_trace_size() == 1 && music_i2s_trace_handle_at(0) == &tx,
                      "duplex stops after first disable failure");
    reset_observers();
    music_i2s_configure_channel(&rx, I2S_DIR_RX, &tx, 1);
    music_i2s_configure_channel(&tx, I2S_DIR_TX, &rx, 1);
    const int ok = data->set_fmt(data, ESP_CODEC_DEV_TYPE_IN_OUT, &format);
    const int expected_calls[] = {MUSIC_I2S_CALL_DISABLE, MUSIC_I2S_CALL_DISABLE,
                                   MUSIC_I2S_CALL_SLOT, MUSIC_I2S_CALL_CLOCK,
                                   MUSIC_I2S_CALL_SLOT, MUSIC_I2S_CALL_CLOCK};
    void *expected_handles[] = {&tx, &rx, &tx, &tx, &rx, &rx};
    bool exact_trace = music_i2s_trace_size() == 6;
    for (int i = 0; exact_trace && i < 6; ++i) {
        exact_trace = music_i2s_trace_at(i) == expected_calls[i] &&
                      music_i2s_trace_handle_at(i) == expected_handles[i];
    }
    expect_acceptance(ok == ESP_CODEC_DEV_OK && exact_trace,
                      "duplex success has exact RX/TX operation and handle trace");
    expect_acceptance(music_i2s_channel_active_for(&rx) == 0 &&
                          music_i2s_channel_active_for(&tx) == 0,
                      "duplex format leaves both directions disabled until enable");
    reset_observers();
    const int rx_enable = data->enable(data, ESP_CODEC_DEV_TYPE_IN, true);
    expect_acceptance(rx_enable == ESP_CODEC_DEV_OK && music_i2s_channel_active_for(&rx) &&
                          !music_i2s_channel_active_for(&tx) && music_i2s_trace_size() == 1 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_ENABLE &&
                          music_i2s_trace_handle_at(0) == &rx,
                      "duplex RX enable targets only RX and publishes active state");
    reset_observers();
    const int tx_enable = data->enable(data, ESP_CODEC_DEV_TYPE_OUT, true);
    expect_acceptance(tx_enable == ESP_CODEC_DEV_OK && music_i2s_channel_active_for(&tx) &&
                          music_i2s_trace_size() == 1 &&
                          music_i2s_trace_handle_at(0) == &tx,
                      "duplex TX enable targets only TX and publishes active state");
    data->close(data);
}

void test_duplex_direction_failure(int direction, int operation) {
    int rx = 43, tx = 44;
    const auto *data = make_duplex_data_if(&rx, &tx);
    check(data != nullptr, "duplex failure interface constructed");
    if (!data) return;
    music_i2s_configure_channel(&rx, I2S_DIR_RX, &tx, 1);
    music_i2s_configure_channel(&tx, I2S_DIR_TX, &rx, 1);
    void *target = direction == ESP_CODEC_DEV_TYPE_IN ? static_cast<void *>(&rx)
                                                      : static_cast<void *>(&tx);
    if (operation == MUSIC_I2S_CALL_DISABLE) music_i2s_fail_next_disable_for(target, ESP_FAIL);
    if (operation == MUSIC_I2S_CALL_SLOT) music_i2s_fail_next_slot_for(target, ESP_FAIL);
    if (operation == MUSIC_I2S_CALL_CLOCK) music_i2s_fail_next_clock_for(target, ESP_FAIL);
    auto format = sample_format();
    const int result = data->set_fmt(data, static_cast<esp_codec_dev_type_t>(direction), &format);
    expect_acceptance(result != ESP_CODEC_DEV_OK,
                      "direction-specific disable/slot/clock failure propagates");
    bool target_seen = false;
    int failure_index = -1;
    for (int i = 0; i < music_i2s_trace_size(); ++i) {
        if (music_i2s_trace_handle_at(i) == target && music_i2s_trace_at(i) == operation) {
            target_seen = true;
            failure_index = i;
        }
    }
    expect_acceptance(target_seen && failure_index == music_i2s_trace_size() - 1,
                      "direction-specific failure is final operation (short circuit)");
    expect_acceptance(music_i2s_channel_active_for(target) ==
                          (operation == MUSIC_I2S_CALL_DISABLE),
                      "direction state reflects whether disable itself failed");
    data->close(data);
}

void test_duplex_enable_failure(int direction) {
    int rx = 45, tx = 46;
    const auto *data = make_duplex_data_if(&rx, &tx);
    check(data != nullptr, "duplex enable-failure interface constructed");
    if (!data) return;
    music_i2s_configure_channel(&rx, I2S_DIR_RX, &tx, 0);
    music_i2s_configure_channel(&tx, I2S_DIR_TX, &rx, 0);
    auto format = sample_format();
    data->set_fmt(data, ESP_CODEC_DEV_TYPE_IN_OUT, &format);
    music_i2s_clear_trace();
    void *target = direction == ESP_CODEC_DEV_TYPE_IN ? static_cast<void *>(&rx)
                                                      : static_cast<void *>(&tx);
    music_i2s_fail_next_enable_for(target, ESP_FAIL);
    const int result = data->enable(data, static_cast<esp_codec_dev_type_t>(direction), true);
    expect_acceptance(result != ESP_CODEC_DEV_OK, "duplex direction enable failure propagates");
    expect_acceptance(music_i2s_trace_size() == 1 &&
                          music_i2s_trace_at(0) == MUSIC_I2S_CALL_ENABLE &&
                          music_i2s_trace_handle_at(0) == target &&
                          !music_i2s_channel_active_for(target),
                      "failed direction enable has exact trace and remains inactive");
    data->close(data);
}

void test_paired_peer_selection_and_channel_direction() {
    int rx = 51, tx = 52;
    music_i2s_configure_channel(&rx, I2S_DIR_RX, &tx, 0);
    audio_codec_i2s_cfg_t capture_cfg{};
    capture_cfg.port = 0;
    capture_cfg.rx_handle = &rx;
    const auto *capture = audio_codec_new_i2s_data(&capture_cfg);
    audio_codec_i2s_cfg_t playback_cfg{};
    playback_cfg.port = 0;
    playback_cfg.tx_handle = &tx;
    music_i2s_configure_channel(&tx, I2S_DIR_TX, nullptr, 0);
    const auto *playback = audio_codec_new_i2s_data(&playback_cfg);
    check(capture && playback, "paired data interfaces constructed");
    if (!capture || !playback) { if (capture) capture->close(capture); if (playback) playback->close(playback); return; }
    reset_observers();
    auto format = sample_format();
    expect_acceptance(capture->set_fmt(capture, ESP_CODEC_DEV_TYPE_IN, &format) == ESP_CODEC_DEV_OK,
                      "paired capture format accepted");
    expect_acceptance(music_i2s_trace_size() == 4 && music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_handle_at(0) == &rx &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK && music_i2s_trace_handle_at(1) == &rx &&
                          music_i2s_trace_at(2) == MUSIC_I2S_CALL_SLOT && music_i2s_trace_handle_at(2) == &tx &&
                          music_i2s_trace_at(3) == MUSIC_I2S_CALL_CLOCK && music_i2s_trace_handle_at(3) == &tx,
                      "paired RX setup selects RX target and peer TX handle");
    capture->close(capture);
    playback->close(playback);
}

void test_peer_preference_and_duplex_fallback() {
    int duplex_rx = 71, duplex_tx = 72, split_rx = 73, split_tx = 74;
    music_i2s_configure_channel(&duplex_rx, I2S_DIR_RX, nullptr, 0);
    music_i2s_configure_channel(&duplex_tx, I2S_DIR_TX, nullptr, 0);
    music_i2s_configure_channel(&split_rx, I2S_DIR_RX, &split_tx, 0);
    music_i2s_configure_channel(&split_tx, I2S_DIR_TX, &split_rx, 0);
    audio_codec_i2s_cfg_t duplex_cfg{}; duplex_cfg.port = 4; duplex_cfg.rx_handle = &duplex_rx; duplex_cfg.tx_handle = &duplex_tx;
    audio_codec_i2s_cfg_t split_rx_cfg{}; split_rx_cfg.port = 4; split_rx_cfg.rx_handle = &split_rx;
    audio_codec_i2s_cfg_t split_tx_cfg{}; split_tx_cfg.port = 4; split_tx_cfg.tx_handle = &split_tx;
    const auto *duplex = audio_codec_new_i2s_data(&duplex_cfg);
    const auto *capture = audio_codec_new_i2s_data(&split_rx_cfg);
    const auto *playback = audio_codec_new_i2s_data(&split_tx_cfg);
    check(duplex && capture && playback, "mixed paired/fallback interfaces constructed");
    if (!duplex || !capture || !playback) {
        if (duplex) duplex->close(duplex);
        if (capture) capture->close(capture);
        if (playback) playback->close(playback);
        return;
    }
    auto format = sample_format(); format.bits_per_sample = 24;
    reset_observers();
    expect_acceptance(capture->set_fmt(capture, ESP_CODEC_DEV_TYPE_IN, &format) == ESP_CODEC_DEV_OK,
                      "matching half-duplex peer setup succeeds");
    bool selected_split = false, selected_fallback = false;
    for (int i = 0; i < music_i2s_trace_size(); ++i) {
        selected_split |= music_i2s_trace_handle_at(i) == &split_tx;
        selected_fallback |= music_i2s_trace_handle_at(i) == &duplex_tx;
    }
    expect_acceptance(selected_split && !selected_fallback,
                      "matching half-duplex peer outranks earlier full-duplex fallback");
    playback->close(playback); capture->close(capture); duplex->close(duplex);

    int lone_rx = 75, fallback_rx = 76, fallback_tx = 77;
    music_i2s_configure_channel(&lone_rx, I2S_DIR_RX, nullptr, 0);
    music_i2s_configure_channel(&fallback_rx, I2S_DIR_RX, nullptr, 0);
    music_i2s_configure_channel(&fallback_tx, I2S_DIR_TX, nullptr, 0);
    audio_codec_i2s_cfg_t fallback_cfg{}; fallback_cfg.port = 5; fallback_cfg.rx_handle = &fallback_rx; fallback_cfg.tx_handle = &fallback_tx;
    audio_codec_i2s_cfg_t lone_cfg{}; lone_cfg.port = 5; lone_cfg.rx_handle = &lone_rx;
    const auto *fallback = audio_codec_new_i2s_data(&fallback_cfg);
    const auto *lone = audio_codec_new_i2s_data(&lone_cfg);
    check(fallback && lone, "sole full-duplex fallback interfaces constructed");
    if (fallback && lone) {
        reset_observers();
        expect_acceptance(lone->set_fmt(lone, ESP_CODEC_DEV_TYPE_IN, &format) == ESP_CODEC_DEV_OK,
                          "sole full-duplex fallback setup succeeds");
        bool saw_fallback = false;
        for (int i = 0; i < music_i2s_trace_size(); ++i) saw_fallback |= music_i2s_trace_handle_at(i) == &fallback_tx;
        expect_acceptance(saw_fallback, "full-duplex peer is used when no half-duplex peer exists");
    }
    if (lone) lone->close(lone);
    if (fallback) fallback->close(fallback);
}

void test_simplex_input_uses_rx_handle() {
    int rx = 56;
    music_i2s_configure_channel(&rx, I2S_DIR_RX, nullptr, 0);
    audio_codec_i2s_cfg_t cfg{};
    cfg.port = 3;
    cfg.rx_handle = &rx;
    const auto *input = audio_codec_new_i2s_data(&cfg);
    check(input != nullptr, "simplex IN data interface constructed");
    if (!input) return;
    music_i2s_reset();
    auto format = sample_format();
    input->set_fmt(input, ESP_CODEC_DEV_TYPE_IN, &format);
    expect_acceptance(music_i2s_trace_size() == 2 && music_i2s_trace_at(0) == MUSIC_I2S_CALL_SLOT &&
                          music_i2s_trace_handle_at(0) == &rx &&
                          music_i2s_trace_at(1) == MUSIC_I2S_CALL_CLOCK && music_i2s_trace_handle_at(1) == &rx,
                      "inactive simplex IN skips disable and configures identified RX handle");
    input->close(input);
}

void test_peer_expansion_failure(int operation, bool fail_peer) {
    int rx = 61, tx = 62;
    music_i2s_configure_channel(&rx, I2S_DIR_RX, nullptr, 0);
    music_i2s_configure_channel(&tx, I2S_DIR_TX, nullptr, 0);
    audio_codec_i2s_cfg_t rx_cfg{};
    rx_cfg.port = 2;
    rx_cfg.rx_handle = &rx;
    audio_codec_i2s_cfg_t tx_cfg{};
    tx_cfg.port = 2;
    tx_cfg.tx_handle = &tx;
    const auto *capture = audio_codec_new_i2s_data(&rx_cfg);
    const auto *playback = audio_codec_new_i2s_data(&tx_cfg);
    check(capture && playback, "peer-expansion interfaces constructed");
    if (!capture || !playback) { if (capture) capture->close(capture); if (playback) playback->close(playback); return; }
    auto peer_format = sample_format();
    expect_acceptance(playback->set_fmt(playback, ESP_CODEC_DEV_TYPE_OUT, &peer_format) == ESP_CODEC_DEV_OK,
                      "peer baseline format succeeds");
    expect_acceptance(playback->enable(playback, ESP_CODEC_DEV_TYPE_OUT, true) == ESP_CODEC_DEV_OK,
                      "peer channel enabled before expansion");
    music_i2s_clear_trace();
    const auto failed_handle = fail_peer ? static_cast<void *>(&tx) : static_cast<void *>(&rx);
    if (!fail_peer) music_i2s_configure_channel(&rx, I2S_DIR_RX, nullptr, 1);
    if (operation == MUSIC_I2S_CALL_DISABLE) music_i2s_fail_next_disable_for(failed_handle, ESP_FAIL);
    if (operation == MUSIC_I2S_CALL_SLOT) music_i2s_fail_next_slot_for(failed_handle, ESP_FAIL);
    if (operation == MUSIC_I2S_CALL_CLOCK) music_i2s_fail_next_clock_for(failed_handle, ESP_FAIL);
    if (operation == MUSIC_I2S_CALL_ENABLE) music_i2s_fail_next_enable_for(failed_handle, ESP_FAIL);
    auto expanded = sample_format();
    expanded.bits_per_sample = 32;
    const int result = capture->set_fmt(capture, ESP_CODEC_DEV_TYPE_IN, &expanded);
    if (acceptance_mode && result == ESP_CODEC_DEV_OK) {
        std::fprintf(stderr, "peer case operation=%d fail_peer=%d unexpectedly succeeded\n", operation, fail_peer);
    }
    expect_acceptance(result != ESP_CODEC_DEV_OK, "peer expansion operation failure propagates");
    bool saw_failed_channel = false;
    for (int i = 0; i < music_i2s_trace_size(); ++i) {
        if (music_i2s_trace_handle_at(i) == failed_handle && music_i2s_trace_at(i) == operation) saw_failed_channel = true;
    }
    expect_acceptance(saw_failed_channel, "fault injection reached requested channel and operation");
    const int failed_index = [&] { for (int i = 0; i < music_i2s_trace_size(); ++i) if (music_i2s_trace_handle_at(i) == failed_handle && music_i2s_trace_at(i) == operation) return i; return -1; }();
    expect_acceptance(failed_index >= 0 && failed_index == music_i2s_trace_size() - 1,
                      "no I2S operation follows the injected expansion failure");
    expect_acceptance(music_i2s_channel_active_for(failed_handle) ==
                          (operation == MUSIC_I2S_CALL_DISABLE),
                      "peer expansion failure leaves channel state consistent with failed stage");
    capture->close(capture);
    playback->close(playback);
}
}  // namespace

extern "C" {
esp_codec_dev_mutex_handle_t esp_codec_dev_mutex_create(void) { return std::malloc(1); }
int esp_codec_dev_mutex_lock(esp_codec_dev_mutex_handle_t, int) { return 0; }
int esp_codec_dev_mutex_unlock(esp_codec_dev_mutex_handle_t) { return 0; }
void esp_codec_dev_mutex_destroy(esp_codec_dev_mutex_handle_t mutex) { std::free(mutex); }
void esp_codec_dev_sleep(int) {}
const audio_codec_vol_if_t *audio_codec_new_sw_vol(void) { return nullptr; }
int audio_codec_delete_vol_if(const audio_codec_vol_if_t *) { return ESP_CODEC_DEV_OK; }
int codec_dev_mirror_init(int, codec_dev_mirror_handle_t *) { return ESP_CODEC_DEV_NOT_SUPPORT; }
int codec_dev_mirror_write(codec_dev_mirror_handle_t, const uint8_t *, int) { return ESP_CODEC_DEV_NOT_SUPPORT; }
int codec_dev_mirror_read(codec_dev_mirror_handle_t, uint8_t *, int, int, int *) { return ESP_CODEC_DEV_NOT_SUPPORT; }
int codec_dev_mirror_deinit(codec_dev_mirror_handle_t) { return ESP_CODEC_DEV_OK; }
}

int main(int argc, char **argv) {
    acceptance_mode = argc == 2 && std::strcmp(argv[1], "--accept") == 0;
    if (argc > 2 || (argc == 2 && !acceptance_mode)) {
        std::fprintf(stderr, "usage: %s [--accept]\n", argv[0]);
        return 2;
    }

    if (acceptance_mode) {
        test_acceptance_active_order();
        test_acceptance_faults();
        test_acceptance_codec_open_rollback();
        test_cleanup_failures_are_retryable();
        test_public_retry_with_distinct_format();
        test_duplex_disable_both_and_preserve_first_error();
        for (int direction : {ESP_CODEC_DEV_TYPE_IN, ESP_CODEC_DEV_TYPE_OUT}) {
            test_duplex_direction_failure(direction, MUSIC_I2S_CALL_DISABLE);
            test_duplex_direction_failure(direction, MUSIC_I2S_CALL_SLOT);
            test_duplex_direction_failure(direction, MUSIC_I2S_CALL_CLOCK);
            test_duplex_enable_failure(direction);
        }
        test_paired_peer_selection_and_channel_direction();
        test_peer_preference_and_duplex_fallback();
        test_simplex_input_uses_rx_handle();
        test_peer_expansion_failure(MUSIC_I2S_CALL_DISABLE, true);
        test_peer_expansion_failure(MUSIC_I2S_CALL_DISABLE, false);
        test_peer_expansion_failure(MUSIC_I2S_CALL_SLOT, false);
        test_peer_expansion_failure(MUSIC_I2S_CALL_SLOT, true);
        test_peer_expansion_failure(MUSIC_I2S_CALL_CLOCK, false);
        test_peer_expansion_failure(MUSIC_I2S_CALL_CLOCK, true);
        test_peer_expansion_failure(MUSIC_I2S_CALL_ENABLE, true);
        test_peer_expansion_failure(MUSIC_I2S_CALL_ENABLE, false);
    } else {
        test_current_vendor_characterization();
    }
    std::printf("%s: %d assertions, failures=%d\n",
                acceptance_mode ? "acceptance" : "baseline characterization", checks, failures);
    return failures == 0 ? 0 : 1;
}
