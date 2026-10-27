#pragma once
#define ESP_OK 0
void *bsp_audio_codec_speaker_init(void);
bool bsp_headphone_is_connected(void);
int bsp_feature_enable(int feature, bool enabled);
#define BSP_FEATURE_SPEAKER 1
