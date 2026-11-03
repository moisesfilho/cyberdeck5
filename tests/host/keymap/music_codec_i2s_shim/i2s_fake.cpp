#include "driver/i2s_std.h"
#include "driver/i2s_test_control.h"
#include "esp_err.h"
#include <array>
#include <cstring>

namespace {
int disables;
int enables;
int slots;
int clocks;
int next_disable_error;
int last_disable_result;
int next_slot_error, next_clock_error, next_enable_error;
struct Channel { void *handle{}; void *pair{}; int direction{I2S_DIR_TX}; bool active{}; int disable_error{}, slot_error{}, clock_error{}, enable_error{}; };
std::array<Channel, 64> channels{};
std::array<int, 64> trace{};
std::array<void *, 64> trace_handles{};
int trace_size;
Channel *find(void *handle) { for (auto &item : channels) if (item.handle == handle) return &item; return nullptr; }
void record(int call, void *handle) { if (trace_size < static_cast<int>(trace.size())) { trace[trace_size] = call; trace_handles[trace_size++] = handle; } }
}

extern "C" {
void music_i2s_reset(void) { disables = enables = slots = clocks = next_disable_error = last_disable_result = 0; next_slot_error = next_clock_error = next_enable_error = 0; for (auto &item : channels) { item.active = false; item.disable_error = item.slot_error = item.clock_error = item.enable_error = 0; } trace_size = 0; }
void music_i2s_clear_trace(void) { trace_size = 0; }
void music_i2s_fail_next_disable(int error) { next_disable_error = error; }
void music_i2s_fail_next_slot(int error) { next_slot_error = error; }
void music_i2s_fail_next_clock(int error) { next_clock_error = error; }
void music_i2s_fail_next_enable(int error) { next_enable_error = error; }
void music_i2s_set_channel_active(int active) { channels[0].handle = reinterpret_cast<void *>(1); channels[0].active = active != 0; }
int music_i2s_channel_active(void) { return channels[0].active; }
void music_i2s_configure_channel(void *handle, int direction, void *pair, int active) { auto *item = find(handle); if (!item) for (auto &candidate : channels) if (!candidate.handle) { item = &candidate; break; } if (item) *item = Channel{handle, pair, direction, active != 0}; }
int music_i2s_channel_active_for(void *handle) { auto *item = find(handle); return item && item->active; }
void music_i2s_fail_next_disable_for(void *handle, int error) { if (auto *item = find(handle)) item->disable_error = error; }
void music_i2s_fail_next_slot_for(void *handle, int error) { if (auto *item = find(handle)) item->slot_error = error; }
void music_i2s_fail_next_clock_for(void *handle, int error) { if (auto *item = find(handle)) item->clock_error = error; }
void music_i2s_fail_next_enable_for(void *handle, int error) { if (auto *item = find(handle)) item->enable_error = error; }
int music_i2s_trace_size(void) { return trace_size; }
int music_i2s_trace_at(int index) { return index >= 0 && index < trace_size ? trace[index] : 0; }
void *music_i2s_trace_handle_at(int index) { return index >= 0 && index < trace_size ? trace_handles[index] : nullptr; }
void music_i2s_record_call(int call) { record(call, nullptr); }
int music_i2s_disable_calls(void) { return disables; }
int music_i2s_enable_calls(void) { return enables; }
int music_i2s_slot_calls(void) { return slots; }
int music_i2s_clock_calls(void) { return clocks; }
int music_i2s_last_disable_result(void) { return last_disable_result; }
int i2s_channel_disable(i2s_chan_handle_t handle) {
    record(MUSIC_I2S_CALL_DISABLE, handle);
    ++disables;
    auto *channel = find(handle);
    const int injected = channel && channel->disable_error ? channel->disable_error : next_disable_error;
    if (channel) channel->disable_error = 0;
    const int result = injected != ESP_OK ? injected : (channel && channel->active ? ESP_OK : ESP_ERR_INVALID_STATE);
    next_disable_error = 0;
    last_disable_result = result;
    if (result == 0 && channel) channel->active = false;
    return result;
}
int i2s_channel_enable(i2s_chan_handle_t handle) { record(MUSIC_I2S_CALL_ENABLE, handle); ++enables; auto *channel = find(handle); const int result = channel && channel->enable_error ? channel->enable_error : next_enable_error; if (channel) channel->enable_error = 0; next_enable_error = 0; if (!result && channel) channel->active = true; return result; }
int i2s_channel_get_info(i2s_chan_handle_t handle, i2s_chan_info_t *info) {
    auto *channel = find(handle);
    info->mode = I2S_COMM_MODE_STD;
    info->dir = channel ? channel->direction : I2S_DIR_TX;
    info->pair_chan = channel ? channel->pair : nullptr;
    info->is_enabled = channel && channel->active;
    return 0;
}
int i2s_channel_reconfig_std_slot(i2s_chan_handle_t handle, const i2s_std_slot_config_t *) { record(MUSIC_I2S_CALL_SLOT, handle); ++slots; auto *channel = find(handle); const int result = channel && channel->slot_error ? channel->slot_error : next_slot_error; if (channel) channel->slot_error = 0; next_slot_error = 0; return result; }
int i2s_channel_reconfig_std_clock(i2s_chan_handle_t handle, const i2s_std_clk_config_t *) { record(MUSIC_I2S_CALL_CLOCK, handle); ++clocks; auto *channel = find(handle); const int result = channel && channel->clock_error ? channel->clock_error : next_clock_error; if (channel) channel->clock_error = 0; next_clock_error = 0; return result; }
int i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *read, uint32_t) { *read = 0; return 0; }
int i2s_channel_write(i2s_chan_handle_t, const void *, size_t n, size_t *written, uint32_t) { *written = n; return 0; }
}
