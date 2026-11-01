#pragma once
#include <stddef.h>
#include "hal/i2s_types.h"
typedef void *i2s_chan_handle_t;
typedef int i2s_std_slot_mask_t;
typedef struct { i2s_comm_mode_t mode; i2s_dir_t dir; i2s_chan_handle_t pair_chan; bool is_enabled; } i2s_chan_info_t;
typedef struct { int data_bit_width, slot_bit_width, ws_width, slot_mode, slot_mask; } i2s_std_slot_config_t;
typedef struct { int sample_rate_hz, mclk_multiple, clk_src; } i2s_std_clk_config_t;
#define I2S_STD_SLOT_BOTH 3
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) ((i2s_std_slot_config_t){(bits),(bits),0,(mode),I2S_STD_SLOT_BOTH})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) ((i2s_std_clk_config_t){(rate),256,0})
#ifdef __cplusplus
extern "C" {
#endif
int i2s_channel_enable(i2s_chan_handle_t);
int i2s_channel_disable(i2s_chan_handle_t);
int i2s_channel_get_info(i2s_chan_handle_t, i2s_chan_info_t *);
int i2s_channel_reconfig_std_slot(i2s_chan_handle_t, const i2s_std_slot_config_t *);
int i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t *);
int i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, uint32_t);
int i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, uint32_t);
#ifdef __cplusplus
}
#endif
