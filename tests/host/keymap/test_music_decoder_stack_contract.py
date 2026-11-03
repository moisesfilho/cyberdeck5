#!/usr/bin/env python3
"""Keep the decoder's large scratch buffer off the FreeRTOS worker stack."""

import re
from pathlib import Path

source = (Path(__file__).resolve().parents[3] /
          "components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp").read_text()
decoder = (Path(__file__).resolve().parents[3] /
           "components/cyberdeck/third_party/minimp3/minimp3.h").read_text()

stack = re.search(r"k_worker_stack\s*=\s*(\d+)\s*;", source)
assert stack, "music worker stack budget must be explicit"
assert int(stack.group(1)) == 8192, "decoder scratch must not require a larger internal-RAM task stack"
assert "mp3dec_scratch_t scratch;" in decoder, "upstream stack-based decoder changed; inspect wrapper"
assert "static int mp3dec_decode_frame_with_scratch(" in decoder
assert "mp3dec_scratch_t *scratch)" in decoder
assert "std::unique_ptr<mp3dec_scratch_t> scratch(new (std::nothrow) mp3dec_scratch_t)" in source
assert "if (!decoder || !scratch || !input || !pcm)" in source
assert "pcm.get(), &info, scratch.get()" in source
assert "mp3dec_decode_frame(decoder.get()" not in source
print("PASS: music worker owns decoder scratch on heap; allocation failure is handled")
