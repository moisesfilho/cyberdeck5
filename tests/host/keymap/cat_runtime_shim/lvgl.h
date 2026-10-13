#pragma once
enum lv_result_t { LV_RESULT_OK, LV_RESULT_INVALID };
inline lv_result_t lv_async_call(void (*)(void *), void *) { return LV_RESULT_INVALID; }
