# Third-party code

## minimp3

- Upstream: https://github.com/lieff/minimp3
- Revision: `ea99364f61c14656440e8d77e9c233ccf3124633`
- Files: `third_party/minimp3/minimp3.h`, `third_party/minimp3/LICENSE`
- License: CC0 1.0 Universal. The upstream license text is included alongside
  the source. The decoder is compiled into the firmware player; no runtime or
  network dependency is added.
- Local adaptation: `mp3dec_decode_frame_with_scratch` accepts caller-owned
  scratch storage. The original `mp3dec_decode_frame` entry point remains
  available; the firmware player keeps scratch on heap to avoid a 17 KB decoder
  frame on its 8 KB FreeRTOS stack.
