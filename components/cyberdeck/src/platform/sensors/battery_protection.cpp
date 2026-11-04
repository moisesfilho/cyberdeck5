#include "platform/sensors/battery_protection.h"
#include "platform/sensors/cyberdeck_battery_protection.h"
#include "platform/sensors/ina226_reader.h"
#include "platform/sensors/cyberdeck_expander_device.h"
#include "platform/sensors/cyberdeck_expander_broker.h"

#include "bsp/m5stack_tab5.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace {

constexpr char NVS_NAMESPACE[] = "battery_prot";
constexpr char NVS_KEY_PROTECTION[] = "prot_enabled";

constexpr char TAG[] = "battery_prot";

esp_io_expander_handle_t s_expander = nullptr;
bool s_expander_configured = false;
std::atomic<bool> s_started{false};
std::mutex s_snapshot_mutex;
std::mutex s_policy_mutex;
cyberdeck_battery_protection::state s_policy;
cyberdeck_battery::snapshot s_snapshot{};
std::int64_t s_policy_sampled_at_us = 0;
bool s_cached_chg_stat_valid = false;
bool s_cached_chg_stat_raw_low = false;
std::int64_t s_chg_stat_sampled_at_us = 0;
// Last level confirmed by a successful CHG_EN write, never an intention. Init
// preloads P7=0 (charger inhibited), so the confirmed level starts low and a
// failed write keeps this value so the next sample retries.
bool s_last_applied_charger_state = false;

bool read_chg_status(bool *out_valid, bool *out_raw_low)
{
    cyberdeck_expander_broker::lease lease(std::chrono::milliseconds(100));
    if (!lease) return false;
    if (s_expander == nullptr) {
        return false;
    }
    uint32_t level = 0;
    esp_err_t err = esp_io_expander_get_level(s_expander, IO_EXPANDER_PIN_NUM_6, &level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Charger stat read failed: %s", esp_err_to_name(err));
        return false;
    }
    *out_valid = true;
    *out_raw_low = (level == 0);
    return true;
}

// Single authority for the confirmed CHG_EN level: the cache only moves after a
// successful write, so a failed write leaves it stale and the next sample or
// set_enabled retries instead of assuming the request took effect.
esp_err_t write_chg_enable(bool enable)
{
    cyberdeck_expander_broker::lease lease(std::chrono::milliseconds(100));
    if (!lease) return ESP_ERR_TIMEOUT;
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t level = enable ? 1 : 0;
    esp_err_t err = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_7, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Charger enable write failed: %s", esp_err_to_name(err));
        return err;
    }
    s_last_applied_charger_state = enable;
    return ESP_OK;
}

esp_err_t init_expander()
{
    // CHG_STAT CHG_QC_EN CHG_EN
    if (s_expander_configured) {
        return ESP_OK;
    }

    if (s_expander == nullptr) {
        const int64_t expander_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "HOSTED_DIAG battery stage=expander_init begin t_us=%lld",
                 static_cast<long long>(expander_started_us));
        if (cyberdeck_expander_device_init() != ESP_OK) return ESP_FAIL;
        s_expander = cyberdeck_expander_device_handle();
        if (s_expander == nullptr) {
            ESP_LOGE(TAG, "HOSTED_DIAG battery stage=expander_init end result=ESP_FAIL duration_ms=%lld t_us=%lld",
                     static_cast<long long>((esp_timer_get_time() - expander_started_us) / 1000),
                     static_cast<long long>(esp_timer_get_time()));
            ESP_LOGE(TAG, "Failed to init expander-B");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "HOSTED_DIAG battery stage=expander_init end result=ESP_OK duration_ms=%lld t_us=%lld",
                 static_cast<long long>((esp_timer_get_time() - expander_started_us) / 1000),
                 static_cast<long long>(esp_timer_get_time()));
    }

    // The whole direction/latch/mode setup is one expander-B transaction. Its
    // lease must end before write_chg_enable(): that helper takes the same
    // non-recursive broker mutex, and a nested acquisition fails immediately
    // (EBUSY -> ESP_ERR_TIMEOUT), which would leave CHG_EN at the 0 preload.
    esp_err_t err = ESP_OK;
    {
        cyberdeck_expander_broker::lease lease(std::chrono::milliseconds(250));
        if (!lease) return ESP_ERR_TIMEOUT;

        err = esp_io_expander_set_dir(s_expander,
                                      IO_EXPANDER_PIN_NUM_6,  // CHG_STAT
                                      IO_EXPANDER_INPUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_STAT dir failed: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_pullupdown(s_expander,
                                             IO_EXPANDER_PIN_NUM_6,
                                             IO_EXPANDER_PULL_UP);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_STAT pull-up failed: %s", esp_err_to_name(err));
            return err;
        }

        // Establish safe inactive latches before enabling either output. The
        // expander powers up with its pins high-impedance, so direction alone is
        // insufficient: explicitly select push-pull drive for both controls.
        err = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_5, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_QC_EN inactive-high preload failed: %s",
                     esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_7, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_EN inactive-low preload failed: %s",
                     esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_dir(s_expander, IO_EXPANDER_PIN_NUM_5,
                                      IO_EXPANDER_OUTPUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_QC_EN dir failed; charging remains inhibited: %s",
                     esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_output_mode(
            s_expander, IO_EXPANDER_PIN_NUM_5, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_QC_EN push-pull mode failed: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_dir(s_expander,
                                      IO_EXPANDER_PIN_NUM_7,  // CHG_EN
                                      IO_EXPANDER_OUTPUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_EN dir failed: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_output_mode(
            s_expander, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_EN push-pull mode failed: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_5, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CHG_QC_EN low write failed; charging remains inhibited: %s",
                     esp_err_to_name(err));
            return err;
        }
    }

    // CHG_EN set_level 1 (enabled) is deliberately the final hardware stage,
    // after QC is asserted above, and it owns its own broker lease.
    err = write_chg_enable(true);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Expander-B initialized: CHG_STAT pin %u (input/pull-up), CHG_QC_EN pin 5 (push-pull, low), CHG_EN pin %u (push-pull, high)",
             IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_PIN_NUM_7);
    s_expander_configured = true;
    return ESP_OK;
}

esp_err_t load_protection_option(bool *out_enabled)
{
    if (out_enabled == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *out_enabled = cyberdeck_battery_protection::default_protection_enabled;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t value = 0;
    err = nvs_get_u8(handle, NVS_KEY_PROTECTION, &value);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_enabled = cyberdeck_battery_protection::default_protection_enabled;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS get failed: %s", esp_err_to_name(err));
        return err;
    }

    *out_enabled = (value != 0);
    return ESP_OK;
}

esp_err_t persist_protection_option(bool enabled)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open for write failed: %s", esp_err_to_name(err));
        return err;
    }

    const uint8_t value = enabled ? 1 : 0;
    err = nvs_set_u8(handle, NVS_KEY_PROTECTION, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS persist failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t apply_charger_state(bool charger_enabled)
{
    const esp_err_t err = write_chg_enable(charger_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply charger state: %s", esp_err_to_name(err));
    }
    return err;
}

void update_snapshot_from_policy(const cyberdeck_battery_protection::snapshot &policy_snap)
{
    std::lock_guard<std::mutex> lock(s_snapshot_mutex);
    s_snapshot.available = policy_snap.available;
    s_snapshot.percentage = policy_snap.percentage;
    s_snapshot.charge = [policy_snap]() {
        switch (policy_snap.state) {
        case cyberdeck_battery_protection::battery_state::charging:
            return cyberdeck_battery::charge_class::charging;
        case cyberdeck_battery_protection::battery_state::battery:
            return cyberdeck_battery::charge_class::discharging;
        case cyberdeck_battery_protection::battery_state::external:
            return cyberdeck_battery::charge_class::neutral;
        case cyberdeck_battery_protection::battery_state::absent:
            return cyberdeck_battery::charge_class::absent;
        case cyberdeck_battery_protection::battery_state::unknown:
        default:
            return cyberdeck_battery::charge_class::unavailable;
        }
    }();
}

/*
 * Single sampling path shared by every reader of the policy.  It reads the
 * INA226 sample and CHG_STAT, feeds the pure policy, republishes the battery
 * projection and applies the resulting charger level.  A failed CHG_STAT read
 * is published as an unknown signal instead of freezing the measurement.
 */
bool sample_and_publish(cyberdeck_battery_protection::observation *out_observation,
                        cyberdeck_battery_protection::snapshot *out_policy)
{
    cyberdeck_battery::sample ina_sample{};
    const bool ina_valid = ina226_reader_get_raw_sample(&ina_sample) &&
                           ina_sample.present && ina_sample.valid;

    bool chg_valid = false;
    bool raw_chg_stat_low = false;
    (void)read_chg_status(&chg_valid, &raw_chg_stat_low);

    cyberdeck_battery_protection::observation obs{};
    if (ina_valid) {
        obs.ina_valid = true;
        obs.bus_voltage_mv = ina_sample.bus_voltage_mv;
        obs.current_ma = ina_sample.current_ma;
        obs.percentage =
            cyberdeck_battery::percentage_from_bus_voltage_mv(ina_sample.bus_voltage_mv);
    }
    if (chg_valid) {
        obs.chg_valid = true;
        obs.raw_chg_stat_low = raw_chg_stat_low;
    }

    cyberdeck_battery_protection::snapshot policy_snap;
    {
        std::lock_guard<std::mutex> lock(s_policy_mutex);
        policy_snap = s_policy.observe(obs);
    }
    update_snapshot_from_policy(policy_snap);
    {
        std::lock_guard<std::mutex> lock(s_snapshot_mutex);
        s_policy_sampled_at_us = esp_timer_get_time();
        s_cached_chg_stat_valid = chg_valid;
        s_cached_chg_stat_raw_low = raw_chg_stat_low;
        s_chg_stat_sampled_at_us = chg_valid ? s_policy_sampled_at_us : 0;
    }

    {
        std::lock_guard<std::mutex> lock(s_policy_mutex);
        const bool charger_now = policy_snap.charger_enabled;
        if (charger_now != s_last_applied_charger_state) {
            // The confirmed-level cache moves only inside write_chg_enable() on
            // success, so a failed write is retried by the next sample.
            if (apply_charger_state(charger_now) != ESP_OK) {
                ESP_LOGE(TAG, "CHG_EN write failed, preserving last confirmed level (%s)",
                         s_last_applied_charger_state ? "enabled" : "disabled");
            }
        }
    }

    if (out_observation != nullptr) {
        *out_observation = obs;
    }
    if (out_policy != nullptr) {
        *out_policy = policy_snap;
    }
    return true;
}

} // namespace

extern "C" esp_err_t battery_protection_init(void)
{
    if (s_started.load(std::memory_order_acquire)) {
        return ESP_OK;
    }

    bool persisted_enabled = cyberdeck_battery_protection::default_protection_enabled;
    esp_err_t err = load_protection_option(&persisted_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load protection option, using default: %s",
                 esp_err_to_name(err));
    }

    s_policy.restore_protection_enabled(persisted_enabled);

    err = init_expander();
    if (err != ESP_OK) {
        return err;
    }

    err = ina226_reader_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 init failed: %s", esp_err_to_name(err));
    }

    s_started.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "Battery protection initialized (protection_enabled=%s)",
             persisted_enabled ? "true" : "false");
    return ESP_OK;
}

extern "C" esp_err_t battery_protection_start(void)
{
    return battery_protection_init();
}

extern "C" bool battery_protection_started(void)
{
    return s_started.load(std::memory_order_acquire);
}

extern "C" bool battery_protection_get_snapshot(cyberdeck_battery::snapshot *out_snapshot)
{
    if (out_snapshot == nullptr) {
        return false;
    }

    if (!battery_protection_started()) {
        return false;
    }

    if (!sample_and_publish(nullptr, nullptr)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshot_mutex);
        *out_snapshot = s_snapshot;
    }
    return true;
}

extern "C" bool battery_protection_get_policy_snapshot(
    cyberdeck_battery_protection::snapshot *out_snapshot)
{
    if (out_snapshot == nullptr) {
        return false;
    }

    if (!battery_protection_started()) {
        return false;
    }

    cyberdeck_battery_protection::snapshot policy_snap{};
    if (!sample_and_publish(nullptr, &policy_snap)) {
        return false;
    }

    *out_snapshot = policy_snap;
    return true;
}

extern "C" bool battery_protection_get_cached_policy_snapshot(
    cyberdeck_battery_protection::snapshot *out_snapshot)
{
    if (out_snapshot == nullptr || !battery_protection_started()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(s_policy_mutex);
    *out_snapshot = s_policy.snapshot();
    return true;
}

extern "C" bool battery_protection_set_enabled(bool enabled)
{
    if (!battery_protection_started()) {
        return false;
    }

    cyberdeck_battery_protection::snapshot policy_snap;
    {
        std::lock_guard<std::mutex> lock(s_policy_mutex);
        const bool changed = s_policy.set_protection_enabled(enabled);
        if (!changed) {
            return false;
        }
        policy_snap = s_policy.snapshot();
    }

    if (policy_snap.charger_enabled != s_last_applied_charger_state) {
        // write_chg_enable() is the only writer of the confirmed-level cache.
        if (apply_charger_state(policy_snap.charger_enabled) != ESP_OK) {
            ESP_LOGE(TAG, "CHG_EN write failed on set_enabled, preserving last confirmed level (%s)",
                     s_last_applied_charger_state ? "enabled" : "disabled");
        }
    }

    esp_err_t err = persist_protection_option(enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist protection option: %s", esp_err_to_name(err));
        return false;
    }

    update_snapshot_from_policy(policy_snap);
    return true;
}

extern "C" bool battery_protection_is_enabled(void)
{
    return s_policy.protection_enabled();
}

extern "C" bool battery_protection_is_active(void)
{
    return s_policy.protection_active();
}

extern "C" bool battery_protection_charger_enabled(void)
{
    return s_policy.charger_enabled();
}

extern "C" bool battery_protection_get_hardware_diagnostics(
    cyberdeck_battery_protection::hardware_diagnostics *out)
{
    if (out == nullptr) return false;
    *out = {};
    if (!battery_protection_started()) return false;
    out->captured_at_us = esp_timer_get_time();
    {
        std::lock_guard<std::mutex> lock(s_snapshot_mutex);
        out->policy_sampled_at_us = s_policy_sampled_at_us;
        out->chg_stat_available = s_cached_chg_stat_valid;
        out->chg_stat_raw_low = s_cached_chg_stat_raw_low;
        out->chg_stat_sampled_at_us = s_chg_stat_sampled_at_us;
    }
    out->expander_available = (s_expander != nullptr);
    if (out->expander_available) {
        cyberdeck_expander_broker::lease lease(std::chrono::milliseconds(250));
        if (lease) {
            // PI4IOE5V6408 (DS40583 Rev3-5): 0x03 = IO_DIR, 0x05 = OUT_SET
            // output latch, 0x07 = OUT_H_IM high-Z. They are configuration and
            // latch state, never pad voltage, so P5/P7 are reported as latch.
            // P6/CHG_STAT is an input sampled through 0x0F (input status), where
            // outputs read LOW by design. 0x13 is clear-on-read and is never
            // touched here, and this path writes no register at all.
            constexpr uint8_t regs[] = {0x03, 0x05, 0x07};
            uint8_t values[3]{};
            bool ok = true;
            const auto device = cyberdeck_expander_raw_handle();
            for (size_t i = 0; i < 3; ++i) {
                if (device == nullptr || i2c_master_transmit_receive(
                        device, &regs[i], 1, &values[i], 1, 100) != ESP_OK) {
                    ok = false;
                    break;
                }
            }
            out->expander_registers_valid = ok;
            if (ok) {
                out->expander_register_origin = 0x44;
                out->expander_direction = values[0];
                out->expander_output_latch = values[1];
                out->expander_hiz = values[2];
                out->chg_qc_read_ok = true;
                out->chg_qc_enabled = (values[1] & (1U << 5)) == 0;
                out->chg_en_read_ok = true;
                out->chg_en_enabled = (values[1] & (1U << 7)) != 0;
            }
        }
    }
    cyberdeck_battery::raw_diagnostics ina{};
    if (ina226_reader_get_raw_diagnostics(&ina)) {
        out->ina_available = true;
        out->ina_bus_raw = ina.bus_voltage_register;
        out->ina_current_raw = ina.current_register;
        out->ina_bus_voltage_mv = ina.converted.bus_voltage_mv;
        out->ina_current_ma = ina.converted.current_ma;
        out->ina_sampled_at_us = ina.sampled_at_us;
    } else {
        out->ina_error = ina226_reader_started();
    }
    return true;
}
