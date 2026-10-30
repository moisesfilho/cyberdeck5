#include "apps/music/cyberdeck_music_duration.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
/* A whole MP3 file cannot legitimately report more than a day of audio; a
 * larger value means the header (or the Xing frame count) is corrupt. */
constexpr std::uint64_t k_max_duration_ms = 24ULL * 60ULL * 60ULL * 1000ULL;
constexpr std::uint32_t k_max_frames = 0xFFFFFFU;
constexpr std::size_t k_probe_bytes = 4096;
/* VBR sampling: windows of this size, spread over the whole audio payload.  A
 * single window at the head of the file is not representative of a VBR stream
 * (measured -9 % to -16 % on 20 s LAME VBR files without Xing), so the average
 * is taken from `k_window_count` windows distributed evenly over the payload.
 * 32 * 2048 + 4096 = 69632 bytes of I/O per probe (under the 72 KiB budget);
 * 32 windows keep the worst measured VBR error under 1 % (vs ~2 % with 20). */
constexpr std::size_t k_window_bytes = 2048;
constexpr std::size_t k_window_count = 32;
/* The average estimator stops after this many frames: enough for the frame
 * ratio to stabilize on a VBR stream without reading the whole file. */
constexpr std::size_t k_max_scanned_frames = 48;
/* Largest Layer III frame: 1440 payload bytes plus one padding byte. */
constexpr std::uint32_t k_max_frame_bytes = 1441;

constexpr std::uint32_t k_bitrate_mpeg1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
constexpr std::uint32_t k_bitrate_mpeg2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
constexpr std::uint32_t k_hz_mpeg1[3] = {44100, 48000, 32000};
constexpr std::uint32_t k_hz_mpeg2[3] = {22050, 24000, 16000};
constexpr std::uint32_t k_hz_mpeg25[3] = {11025, 12000, 8000};

struct frame_header {
    bool valid = false;
    std::uint32_t hz = 0;
    std::uint32_t frame_bytes = 0;
    std::uint32_t samples_per_frame = 0;
    std::size_t side_info = 0;
};

std::uint32_t read_be32(const std::uint8_t *data) {
    return (std::uint32_t(data[0]) << 24) | (std::uint32_t(data[1]) << 16) | (std::uint32_t(data[2]) << 8) |
           std::uint32_t(data[3]);
}

/* Header-only validation: sync, Layer III, a valid version, a real bitrate and
 * sample-rate index, and a frame size that fits the Layer III range. */
frame_header parse_header(const std::uint8_t *data, std::size_t size) {
    frame_header out{};
    if (data == nullptr || size < 4)
        return out;
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0)
        return out;
    const std::uint8_t version = static_cast<std::uint8_t>((data[1] >> 3) & 0x3);
    const std::uint8_t layer = static_cast<std::uint8_t>((data[1] >> 1) & 0x3);
    if (version == 1 || layer != 1)
        return out;
    const std::uint8_t bitrate_index = static_cast<std::uint8_t>((data[2] >> 4) & 0xF);
    const std::uint8_t hz_index = static_cast<std::uint8_t>((data[2] >> 2) & 0x3);
    if (bitrate_index == 0 || bitrate_index == 15 || hz_index == 3)
        return out;
    const bool mpeg1 = version == 3;
    const std::uint32_t bitrate = (mpeg1 ? k_bitrate_mpeg1 : k_bitrate_mpeg2)[bitrate_index];
    const std::uint32_t hz = (mpeg1 ? k_hz_mpeg1 : version == 2 ? k_hz_mpeg2 : k_hz_mpeg25)[hz_index];
    if (bitrate == 0 || hz == 0)
        return out;
    const std::uint32_t padding = static_cast<std::uint32_t>((data[2] >> 1) & 0x1);
    const std::uint32_t frame_bytes = (mpeg1 ? 144U : 72U) * bitrate * 1000U / hz + padding;
    if (frame_bytes <= 4 || frame_bytes > k_max_frame_bytes)
        return out;
    const bool mono = ((data[3] >> 6) & 0x3) == 3;
    out.valid = true;
    out.hz = hz;
    out.frame_bytes = frame_bytes;
    out.samples_per_frame = mpeg1 ? 1152U : 576U;
    out.side_info = mpeg1 ? (mono ? 17U : 32U) : (mono ? 9U : 17U);
    return out;
}

std::uint32_t frame_duration_ms(std::uint32_t frames, const frame_header &header) {
    if (frames == 0 || frames > k_max_frames)
        return 0;
    const std::uint64_t duration = std::uint64_t(frames) * header.samples_per_frame * 1000ULL / header.hz;
    if (duration == 0 || duration > k_max_duration_ms)
        return 0;
    return static_cast<std::uint32_t>(duration);
}

/* Xing/Info layout: "Xing"|"Info", big-endian flags, then the optional fields
 * in flag order (frames 0x1, bytes 0x2, TOC 0x4, quality 0x8).  When the tag is
 * present it is authoritative: an unusable frame count fails closed instead of
 * falling back to an estimate from the few frames that happen to be buffered. */
bool probe_xing(const std::uint8_t *data, std::size_t size, const frame_header &header, mp3_probe &out) {
    const std::size_t tag = 4 + header.side_info;
    if (size < tag + 8)
        return false;
    if (std::memcmp(data + tag, "Xing", 4) != 0 && std::memcmp(data + tag, "Info", 4) != 0)
        return false;
    const std::uint32_t flags = read_be32(data + tag + 4);
    std::size_t cursor = tag + 8;
    std::uint32_t frames = 0;
    if ((flags & 0x1) != 0 && size >= cursor + 4) {
        frames = read_be32(data + cursor);
        cursor += 4;
    }
    if ((flags & 0x2) != 0 && size >= cursor + 4)
        cursor += 4; /* stream size in bytes; not needed for the duration */
    if ((flags & 0x4) != 0 && size >= cursor + 100) {
        std::copy_n(data + cursor, out.toc.size(), out.toc.begin());
        out.has_toc = true;
    }
    out.duration_ms = frame_duration_ms(frames, header);
    return true;
}

/* Extrapolate the payload from the measured bytes/samples ratio:
 * duration_ms = audio_bytes * samples * 1000 / (hz * bytes).  The rational form
 * keeps CBR exact and avoids the truncation of a per-frame millisecond value. */
std::uint32_t duration_from_ratio(std::uint64_t samples, std::uint64_t bytes, std::uint64_t audio_bytes,
                                  std::uint32_t hz) {
    if (samples == 0 || bytes == 0 || audio_bytes == 0 || hz == 0)
        return 0;
    const std::uint64_t duration = samples * 1000ULL * audio_bytes / (std::uint64_t(hz) * bytes);
    if (duration == 0 || duration > k_max_duration_ms)
        return 0;
    return static_cast<std::uint32_t>(duration);
}

/* Average bitrate of the buffered frames, extrapolated to the whole audio
 * payload.  Every scanned frame must share the sample rate, so the rational sum
 * stays exact: duration = sum(spf * 1000) * audio_bytes / (hz * scanned_bytes). */
std::uint32_t estimate_from_frames(const std::uint8_t *data, std::size_t size, std::uint64_t audio_bytes,
                                   const frame_header &first) {
    std::uint64_t scanned_bytes = 0;
    std::uint64_t scanned_samples = 0;
    std::size_t cursor = 0;
    std::size_t scanned = 0;
    while (scanned < k_max_scanned_frames) {
        const frame_header current = parse_header(data + cursor, size - cursor);
        if (!current.valid || current.hz != first.hz || current.samples_per_frame != first.samples_per_frame)
            break;
        if (std::size_t(current.frame_bytes) > size - cursor)
            break; /* truncated tail frame: extrapolate from the complete ones */
        scanned_bytes += current.frame_bytes;
        scanned_samples += current.samples_per_frame;
        cursor += current.frame_bytes;
        ++scanned;
    }
    return duration_from_ratio(scanned_samples, scanned_bytes, audio_bytes, first.hz);
}
} // namespace

bool mp3_frame_window(const std::uint8_t *data, std::size_t size, mp3_window &out) {
    out = mp3_window{};
    if (data == nullptr || size < 8)
        return false;
    /* The window can start in the middle of a frame, so the first header is
     * only trusted when the header at its own frame size agrees with it: a
     * stray 0xFF byte can fake a sync word. */
    frame_header first{};
    std::size_t start = 0;
    while (start + 8 <= size) {
        const frame_header candidate = parse_header(data + start, size - start);
        if (candidate.valid && std::size_t(candidate.frame_bytes) + 4 <= size - start) {
            const std::size_t next = start + candidate.frame_bytes;
            const frame_header following = parse_header(data + next, size - next);
            if (following.valid && following.hz == candidate.hz &&
                following.samples_per_frame == candidate.samples_per_frame) {
                first = candidate;
                break;
            }
        }
        ++start;
    }
    if (!first.valid)
        return false;
    std::uint64_t bytes = 0;
    std::uint64_t samples = 0;
    for (std::size_t cursor = start; cursor + 4 <= size;) {
        const frame_header current = parse_header(data + cursor, size - cursor);
        if (!current.valid || current.hz != first.hz || current.samples_per_frame != first.samples_per_frame)
            break;
        if (std::size_t(current.frame_bytes) > size - cursor)
            break; /* truncated tail frame: keep the complete ones */
        bytes += current.frame_bytes;
        samples += current.samples_per_frame;
        cursor += current.frame_bytes;
    }
    if (bytes == 0 || samples == 0)
        return false;
    out.bytes = bytes;
    out.samples = samples;
    return true;
}

mp3_probe mp3_probe_bytes(const std::uint8_t *data, std::size_t size, std::uint64_t audio_bytes) {
    mp3_probe out{};
    const frame_header header = parse_header(data, size);
    if (!header.valid)
        return out;
    if (probe_xing(data, size, header, out))
        return out;
    out.duration_ms = estimate_from_frames(data, size, audio_bytes, header);
    return out;
}

mp3_seek_point mp3_seek_target(const mp3_probe &info, std::uint64_t offset, std::uint64_t audio_bytes,
                               std::uint32_t target_ms) {
    mp3_seek_point point{};
    point.byte = offset;
    if (info.duration_ms == 0)
        return point;
    if (!info.has_toc) {
        /* CBR: the byte offset is proportional to the requested time, so the
         * position needs no correction.  The byte is clamped to the payload end
         * exactly like the TOC branch, so a target past `duration_ms` can never
         * seek outside `offset + audio_bytes`. */
        point.position_ms = target_ms;
        const std::uint64_t end = offset + audio_bytes;
        const std::uint64_t byte =
            target_ms >= info.duration_ms ? end : offset + audio_bytes * target_ms / info.duration_ms;
        point.byte = std::min(byte, end);
        return point;
    }
    /* The TOC has 100 entries; the last one is the end of the stream and is
     * never a valid start, so the percent is clamped to 99. */
    const std::uint64_t percent = std::min<std::uint64_t>(99, std::uint64_t(target_ms) * 100 / info.duration_ms);
    const std::uint64_t entry = std::uint64_t(info.toc[static_cast<std::size_t>(percent)]);
    const std::uint64_t byte = offset + entry * audio_bytes / 256;
    const std::uint64_t end = offset + audio_bytes;
    point.byte = std::min(std::max(byte, offset), end);
    point.position_ms = static_cast<std::uint32_t>(percent * info.duration_ms / 100);
    return point;
}

bool mp3_audio_offset(int fd, std::uint64_t file_size, std::uint64_t &offset) {
    offset = 0;
    if (fd < 0)
        return false;
    unsigned char header[10]{};
    if (pread(fd, header, sizeof(header), 0) != static_cast<ssize_t>(sizeof(header)) ||
        std::memcmp(header, "ID3", 3) != 0)
        return true;
    if (header[3] < 2 || header[3] > 4 || ((header[6] | header[7] | header[8] | header[9]) & 0x80) != 0)
        return false;
    const std::uint64_t bytes = (std::uint64_t(header[6]) << 21) | (std::uint64_t(header[7]) << 14) |
                                (std::uint64_t(header[8]) << 7) | std::uint64_t(header[9]);
    const std::uint64_t tag = 10 + bytes + ((header[5] & 0x10) != 0 ? 10 : 0);
    if (bytes > file_size || tag > file_size)
        return false;
    offset = tag;
    return true;
}

mp3_probe mp3_probe_fd(int fd, std::uint64_t file_size, std::uint64_t offset) {
    mp3_probe out{};
    if (fd < 0 || offset >= file_size)
        return out;
    const std::uint64_t available = file_size - offset;
    const std::size_t read_size = static_cast<std::size_t>(std::min<std::uint64_t>(available, k_probe_bytes));
    /* Heap-backed on purpose: the probe is also called from the 8192-byte
     * playback worker, which cannot afford a 4 KiB stack buffer. */
    std::unique_ptr<unsigned char[]> head(new (std::nothrow) unsigned char[read_size]);
    if (!head)
        return out;
    const ssize_t got = pread(fd, head.get(), read_size, static_cast<off_t>(offset));
    if (got <= 0)
        return out;
    const frame_header first = parse_header(head.get(), static_cast<std::size_t>(got));
    if (!first.valid)
        return out; /* the audio must start with a frame: fail closed */
    /* Xing/Info is authoritative and wins over any estimate. */
    if (probe_xing(head.get(), static_cast<std::size_t>(got), first, out))
        return out;
    /* No Xing: the frames at the head of the file are not representative of a
     * VBR stream, so the average is taken from windows spread over the whole
     * payload.  A single 2048-byte buffer is reused for every window (heap:
     * the playback worker only owns an 8192-byte stack) and released on every
     * path, including the error ones. */
    std::unique_ptr<unsigned char[]> window(new (std::nothrow) unsigned char[k_window_bytes]);
    if (!window)
        return out;
    std::uint64_t bytes = 0;
    std::uint64_t samples = 0;
    for (std::size_t index = 0; index < k_window_count; ++index) {
        const std::uint64_t start = offset + available * index / k_window_count;
        const std::size_t length =
            static_cast<std::size_t>(std::min<std::uint64_t>(k_window_bytes, offset + available - start));
        if (length < 8)
            continue;
        const ssize_t read = pread(fd, window.get(), length, static_cast<off_t>(start));
        if (read <= 0)
            continue;
        mp3_window found{};
        if (!mp3_frame_window(window.get(), static_cast<std::size_t>(read), found))
            continue;
        bytes += found.bytes;
        samples += found.samples;
    }
    /* No window carried a frame: the duration stays unknown (0) and the caller
     * keeps its fallback. */
    out.duration_ms = duration_from_ratio(samples, bytes, available, first.hz);
    return out;
}
} // namespace cyberdeck_music
