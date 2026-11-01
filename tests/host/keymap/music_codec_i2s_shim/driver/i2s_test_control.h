#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void music_i2s_reset(void);
void music_i2s_clear_trace(void);
void music_i2s_fail_next_disable(int error);
void music_i2s_fail_next_slot(int error);
void music_i2s_fail_next_clock(int error);
void music_i2s_fail_next_enable(int error);
void music_i2s_set_channel_active(int active);
int music_i2s_channel_active(void);
void music_i2s_configure_channel(void *handle, int direction, void *pair, int active);
int music_i2s_channel_active_for(void *handle);
void music_i2s_fail_next_disable_for(void *handle, int error);
void music_i2s_fail_next_slot_for(void *handle, int error);
void music_i2s_fail_next_clock_for(void *handle, int error);
void music_i2s_fail_next_enable_for(void *handle, int error);
void *music_i2s_trace_handle_at(int index);
int music_i2s_trace_size(void);
int music_i2s_trace_at(int index);
void music_i2s_record_call(int call);
int music_i2s_disable_calls(void);
int music_i2s_enable_calls(void);
int music_i2s_slot_calls(void);
int music_i2s_clock_calls(void);
int music_i2s_last_disable_result(void);
enum {
    MUSIC_I2S_CALL_DISABLE = 1,
    MUSIC_I2S_CALL_SLOT = 2,
    MUSIC_I2S_CALL_CLOCK = 3,
    MUSIC_I2S_CALL_ENABLE = 4,
    MUSIC_I2S_CALL_CODEC_SETUP = 5,
    MUSIC_I2S_CALL_CODEC_ENABLE = 6,
    MUSIC_I2S_CALL_CODEC_DISABLE = 7,
};
#ifdef __cplusplus
}
#endif
