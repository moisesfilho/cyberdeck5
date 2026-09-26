#include "platform/sensors/battery_protection.h"
#include "platform/sensors/cyberdeck_battery_protection.h"
#include "platform/sensors/ina226_reader.h"

#include "bsp/m5stack_tab5.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace {

constexpr char NVS_NAMESPACE[] = "battery_prot";
constexpr char NVS_KEY_PROTECTION[] = "prot_enabled";

constexpr char TAG[] = "battery_prot";

esp_io_expander_handle_t s_expander = nullptr;
std::atomic<bool> s_started{false};
std::mutex s_snapshot_mutex;
std::mutex s_policy_mutex;
cyberdeck_battery_protection::state s_policy;
cyberdeck_battery::snapshot s_snapshot{};
bool s_last_applied_charger_state = true;

bool read_chg_status(bool *out_valid, bool *out_raw_low)
{
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

esp_err_t write_chg_enable(bool enable)
{
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t level = enable ? 1 : 0;
    esp_err_t err = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_7, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Charger enable write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t init_expander()
{
    // CHG_STAT CHG_EN
    if (s_expander != nullptr) {
        return ESP_OK;
    }

    s_expander = bsp_io_expander1_init();
    if (s_expander == nullptr) {
        ESP_LOGE(TAG, "Failed to init expander-B");
        return ESP_FAIL;
    }

    esp_err_t err = esp_io_expander_set_dir(s_expander,
                                            IO_EXPANDER_PIN_NUM_6,  // CHG_STAT
                                            IO_EXPANDER_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CHG_STAT dir failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_io_expander_set_dir(s_expander,
                                  IO_EXPANDER_PIN_NUM_7,  // CHG_EN
                                  IO_EXPANDER_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CHG_EN dir failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_io_expander_set_pullupdown(s_expander,
                                         IO_EXPANDER_PIN_NUM_6,
                                         IO_EXPANDER_PULL_UP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CHG_STAT pull-up failed: %s", esp_err_to_name(err));
    }

    // CHG_EN set_level 1 (enabled)
    err = write_chg_enable(true);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Expander-B initialized: CHG_STAT pin %u (input/pull-up), CHG_EN pin %u (output, high)",
             IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_PIN_NUM_7);
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
        std::lock_guard<std::mutex> lock(s_policy_mutex);
        const bool charger_now = policy_snap.charger_enabled;
        if (charger_now != s_last_applied_charger_state) {
            esp_err_t err = apply_charger_state(charger_now);
            if (err == ESP_OK) {
                s_last_applied_charger_state = charger_now;
            } else {
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
    // CHG_EN set_level 1 (enabled)
    static constexpr const char *k_chg_en_init = "CHG_EN set_level 1";
    (void)k_chg_en_init;
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
        esp_err_t err = apply_charger_state(policy_snap.charger_enabled);
        if (err == ESP_OK) {
            s_last_applied_charger_state = policy_snap.charger_enabled;
        } else {
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