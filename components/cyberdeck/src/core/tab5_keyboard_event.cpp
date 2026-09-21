#include "tab5_keyboard_event.h"

#include <string.h>

bool tab5_char_event_parse(const uint8_t *raw, size_t raw_len,
                           tab5_char_event_t *out)
{
    if (raw == NULL || out == NULL || raw_len <= 1) {
        return false;
    }

    size_t text_length = raw_len - 1;
    if (text_length > TAB5_CHAR_EVENT_MAX_TEXT) {
        text_length = TAB5_CHAR_EVENT_MAX_TEXT;
    }

    out->modifier = raw[0];
    out->length = static_cast<uint8_t>(text_length);
    memcpy(out->text, raw + 1, text_length);
    out->text[text_length] = '\0';
    return true;
}
