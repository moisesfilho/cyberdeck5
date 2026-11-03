#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cyberdeck_music {
/* Session duration probe for an MPEG-1/2/2.5 Layer III stream.
 *
 * The duration is resolved once per track from the first frame (Xing/Info frame
 * count) or, when there is no Xing header, from the average frame duration of
 * the frames present in `data`.  It is deliberately independent of
 * `mp3dec_frame_info_t::bitrate_kbps`: that field belongs to the frame just
 * decoded, so using it per frame makes a VBR track report a different total on
 * every frame and the progress bar jumps back and forth.
 *
 * `audio_bytes` is `file_size - audio_offset`, i.e. every byte after the ID3v2
 * tag.  Trailing tags (ID3v1, APE, Lyrics3) are therefore counted as audio and
 * slightly inflate the estimate; the probe never scans the tail to find them.
 * `duration_ms == 0` means "unknown" (fail-closed): the caller must keep the
 * previous value instead of publishing a guess. */
struct mp3_probe {
    std::uint32_t duration_ms = 0; /* 0 = desconhecida (fail-closed) */
    bool has_toc = false;
    std::array<std::uint8_t, 100> toc{}; /* só válido quando has_toc */
};

/* Frames measured inside one buffered window: their payload bytes and their
 * sample count, summed over the complete frames of the window. */
struct mp3_window {
    std::uint64_t bytes = 0;
    std::uint64_t samples = 0;
};

/* Starting point of a seek, resolved from the probe of the current track. */
struct mp3_seek_point {
    std::uint64_t byte = 0;
    std::uint32_t position_ms = 0;
};

/* Seek start: the Xing TOC when the probe found one, else the linear mapping.
 *
 * A seek used to map the requested time to a byte offset proportionally, which
 * on a VBR file lands in the middle of a frame: minimp3 then reports a
 * plausible header with zero samples and playback used to die with a decode
 * error.  The TOC gives the byte where the encoder says that time starts; the
 * caller still discards the frames before `target_ms` because a TOC entry is
 * only accurate to ~1% of the track.
 *
 * With a TOC: `percent = min(99, target_ms * 100 / duration_ms)`,
 * `byte = offset + toc[percent] * audio_bytes / 256` clamped to
 * `offset .. offset + audio_bytes` and `position_ms = percent * duration_ms / 100`.
 * Without a TOC the mapping stays linear (`position_ms = target_ms`), which is
 * exact for CBR, and the byte is clamped to the payload end as well, so a
 * target past `duration_ms` can never seek outside `offset + audio_bytes`.
 * `duration_ms == 0` (unknown) fails closed at `offset`/0. */
mp3_seek_point mp3_seek_target(const mp3_probe &info, std::uint64_t offset, std::uint64_t audio_bytes,
                               std::uint32_t target_ms);

/* `data` = primeiros bytes do áudio a partir do offset do primeiro frame. */
mp3_probe mp3_probe_bytes(const std::uint8_t *data, std::size_t size, std::uint64_t audio_bytes);

/* Frames of one buffered window of the audio payload.
 *
 * The window may start in the middle of a frame, so it resynchronizes on the
 * first header confirmed by a consistent following header and then walks the
 * complete frames, summing their bytes and samples.  Every frame of the window
 * must share the sample rate and the samples-per-frame of the first one, so the
 * window has a single bytes/samples ratio.  A window without that header pair
 * (or without one complete frame) fails closed and leaves `out` zeroed. */
bool mp3_frame_window(const std::uint8_t *data, std::size_t size, mp3_window &out);

/* Skip de ID3v2 via pread (não move o fd). false = tag inválida. */
bool mp3_audio_offset(int fd, std::uint64_t file_size, std::uint64_t &offset);

/* Probe of a whole file: the Xing/Info frame count when the tag is present,
 * else the average frame duration of windows spread over the audio payload, so
 * a VBR file whose head is not representative still resolves.  Reads with
 * `pread` and never moves the descriptor. */
mp3_probe mp3_probe_fd(int fd, std::uint64_t file_size, std::uint64_t offset);
} // namespace cyberdeck_music
