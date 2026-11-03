#include "apps/music/cyberdeck_music_duration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

constexpr unsigned k_mpeg1 = 3;
constexpr unsigned k_mpeg2 = 2;
constexpr unsigned k_mpeg25 = 0;
constexpr unsigned k_layer3 = 1;
constexpr unsigned k_joint_stereo = 1;
constexpr unsigned k_mono = 3;

constexpr unsigned k_bitrate_mpeg1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
constexpr unsigned k_bitrate_mpeg2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
constexpr unsigned k_hz_mpeg1[3] = {44100, 48000, 32000};
constexpr unsigned k_hz_mpeg2[3] = {22050, 24000, 16000};
constexpr unsigned k_hz_mpeg25[3] = {11025, 12000, 8000};

std::size_t frame_size(unsigned version, unsigned bitrate_index, unsigned hz_index, bool padding) {
    const bool mpeg1 = version == k_mpeg1;
    const unsigned bitrate = (mpeg1 ? k_bitrate_mpeg1 : k_bitrate_mpeg2)[bitrate_index];
    const unsigned hz = (mpeg1 ? k_hz_mpeg1 : version == k_mpeg2 ? k_hz_mpeg2 : k_hz_mpeg25)[hz_index];
    return (mpeg1 ? 144U : 72U) * bitrate * 1000U / hz + (padding ? 1U : 0U);
}

void write_header(std::uint8_t *frame, unsigned version, unsigned layer, unsigned bitrate_index,
                  unsigned hz_index, unsigned channel_mode, bool padding) {
    frame[0] = 0xFF;
    frame[1] = static_cast<std::uint8_t>(0xE0 | (version << 3) | (layer << 1) | 0x1);
    frame[2] = static_cast<std::uint8_t>((bitrate_index << 4) | (hz_index << 2) | (padding ? 0x2 : 0));
    frame[3] = static_cast<std::uint8_t>(channel_mode << 6);
}

void write_be32(std::uint8_t *target, std::uint32_t value) {
    target[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    target[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    target[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    target[3] = static_cast<std::uint8_t>(value & 0xFF);
}

std::size_t side_info_bytes(unsigned version, unsigned channel_mode) {
    const bool mpeg1 = version == k_mpeg1;
    const bool mono = channel_mode == k_mono;
    return mpeg1 ? (mono ? 17U : 32U) : (mono ? 9U : 17U);
}

/* Stream of `frames` identical MPEG1 128 kbps / 44.1 kHz frames (417 bytes). */
std::vector<std::uint8_t> make_cbr(unsigned frames) {
    constexpr std::size_t k_frame_bytes = 417;
    std::vector<std::uint8_t> data(frames * k_frame_bytes, 0);
    for (unsigned frame = 0; frame < frames; ++frame)
        write_header(data.data() + frame * k_frame_bytes, k_mpeg1, k_layer3, 9, 0, k_joint_stereo, false);
    return data;
}

/* Stream of `frames` identical frames of any version/bitrate/rate/channels. */
std::vector<std::uint8_t> make_stream(unsigned version, unsigned bitrate_index, unsigned hz_index,
                                      unsigned channel_mode, unsigned frames) {
    const std::size_t bytes = frame_size(version, bitrate_index, hz_index, false);
    std::vector<std::uint8_t> data(bytes * frames, 0);
    for (unsigned frame = 0; frame < frames; ++frame)
        write_header(data.data() + frame * bytes, version, k_layer3, bitrate_index, hz_index,
                     channel_mode, false);
    return data;
}

/* VBR stream alternating 417-byte (128 kbps) and 1044-byte (320 kbps) frames. */
std::vector<std::uint8_t> make_vbr(unsigned frames) {
    std::vector<std::uint8_t> data;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const bool high = (frame % 2) == 1;
        const std::size_t bytes = high ? 1044 : 417;
        const std::size_t offset = data.size();
        data.resize(offset + bytes, 0);
        write_header(data.data() + offset, k_mpeg1, k_layer3, high ? 14 : 9, 0, k_joint_stereo, false);
    }
    return data;
}

std::vector<std::uint8_t> make_xing_frame(unsigned version, unsigned bitrate_index, unsigned hz_index,
                                          unsigned channel_mode, std::uint32_t frames, bool toc,
                                          const char *tag = "Xing") {
    const std::size_t bytes = frame_size(version, bitrate_index, hz_index, false);
    std::vector<std::uint8_t> frame(bytes, 0);
    write_header(frame.data(), version, k_layer3, bitrate_index, hz_index, channel_mode, false);
    const std::size_t tag_offset = 4 + side_info_bytes(version, channel_mode);
    std::memcpy(frame.data() + tag_offset, tag, 4);
    std::uint32_t flags = 0x1 | 0x2;
    if (toc)
        flags |= 0x4;
    write_be32(frame.data() + tag_offset + 4, flags);
    std::size_t cursor = tag_offset + 8;
    write_be32(frame.data() + cursor, frames);
    cursor += 4;
    write_be32(frame.data() + cursor, static_cast<std::uint32_t>(bytes));
    cursor += 4;
    if (toc) {
        for (std::size_t index = 0; index < 100; ++index)
            frame[cursor + index] = static_cast<std::uint8_t>(index);
    }
    return frame;
}

std::uint32_t probe_duration(const std::vector<std::uint8_t> &data, std::uint64_t audio_bytes) {
    return cyberdeck_music::mp3_probe_bytes(data.data(), data.size(), audio_bytes).duration_ms;
}

/* Probe a real file descriptor (the path the player uses). */
std::uint32_t probe_fd_duration(const std::vector<std::uint8_t> &bytes, std::uint64_t offset) {
    char path[] = "/tmp/music-duration-window-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0)
        return 0;
    std::uint32_t duration = 0;
    if (::write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())) {
        const int read_fd = ::open(path, O_RDONLY);
        if (read_fd >= 0) {
            duration = cyberdeck_music::mp3_probe_fd(read_fd, bytes.size(), offset).duration_ms;
            CHECK(::close(read_fd) == 0);
        }
    }
    CHECK(::close(fd) == 0);
    CHECK(::unlink(path) == 0);
    return duration;
}

std::vector<std::uint8_t> id3_header(std::uint32_t payload) {
    std::vector<std::uint8_t> header = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0};
    header[6] = static_cast<std::uint8_t>((payload >> 21) & 0x7F);
    header[7] = static_cast<std::uint8_t>((payload >> 14) & 0x7F);
    header[8] = static_cast<std::uint8_t>((payload >> 7) & 0x7F);
    header[9] = static_cast<std::uint8_t>(payload & 0x7F);
    return header;
}

bool write_file(const std::vector<std::uint8_t> &bytes, const char *path) {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return false;
    const bool written = ::write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
    return ::close(fd) == 0 && written;
}

void test_cbr_average() {
    const auto data = make_cbr(100);
    CHECK(data.size() == 41700);
    CHECK(probe_duration(data, 41700) == 2612);
    /* The estimator is proportional to the audio payload it was told about. */
    CHECK(probe_duration(data, 41700 / 2) == 1306);
}

void test_vbr_average() {
    const auto data = make_vbr(60);
    CHECK(data.size() == 43830);
    const std::uint32_t duration = probe_duration(data, data.size());
    /* 60 frames * 1152 samples / 44100 Hz = 1567.35 ms. */
    CHECK(std::abs(static_cast<long>(duration) - 1567L) <= 1L);
}

void test_xing_durations() {
    const auto mpeg1 = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false);
    CHECK(probe_duration(mpeg1, mpeg1.size()) == 26122);
    const auto mpeg2 = make_xing_frame(k_mpeg2, 8, 0, k_mono, 1000, false);
    CHECK(probe_duration(mpeg2, mpeg2.size()) == 26122);
    const auto mpeg25 = make_xing_frame(k_mpeg25, 8, 0, k_mono, 1000, false);
    CHECK(probe_duration(mpeg25, mpeg25.size()) == 52244);
    const auto info = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false, "Info");
    CHECK(probe_duration(info, info.size()) == 26122);
}

void test_xing_toc() {
    const auto data = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, true);
    const auto probe = cyberdeck_music::mp3_probe_bytes(data.data(), data.size(), data.size());
    CHECK(probe.has_toc);
    CHECK(probe.duration_ms == 26122);
    bool toc_matches = true;
    for (std::size_t index = 0; index < probe.toc.size(); ++index)
        toc_matches = toc_matches && probe.toc[index] == static_cast<std::uint8_t>(index);
    CHECK(toc_matches);

    const auto without_toc = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false);
    const auto plain = cyberdeck_music::mp3_probe_bytes(without_toc.data(), without_toc.size(), without_toc.size());
    CHECK(!plain.has_toc);
    CHECK(std::all_of(plain.toc.begin(), plain.toc.end(), [](std::uint8_t value) { return value == 0; }));
}

/* Windows of the distributed VBR sampling (`mp3_frame_window`): resync on the
 * first confirmed header pair, complete frames only, exact bytes/samples sum and
 * fail-closed when the window carries no confirmed frame. */
void test_frame_window() {
    using cyberdeck_music::mp3_window;
    mp3_window window{};
    CHECK(!cyberdeck_music::mp3_frame_window(nullptr, 0, window));
    CHECK(window.bytes == 0 && window.samples == 0);
    const std::vector<std::uint8_t> zeros(4096, 0);
    CHECK(!cyberdeck_music::mp3_frame_window(zeros.data(), zeros.size(), window));
    CHECK(!cyberdeck_music::mp3_frame_window(zeros.data(), 3, window));
    CHECK(window.bytes == 0 && window.samples == 0);

    /* Exact sum of three complete MPEG-1 128 kbps frames (417 B, 1152 samples). */
    const auto cbr = make_cbr(3);
    CHECK(cyberdeck_music::mp3_frame_window(cbr.data(), cbr.size(), window));
    CHECK(window.bytes == 3 * 417);
    CHECK(window.samples == 3 * 1152);

    /* Resync: a window that starts in the middle of a frame measures the three
     * complete frames that follow the first confirmed header pair. */
    const auto shifted = make_cbr(4);
    constexpr std::size_t k_shift = 100;
    CHECK(cyberdeck_music::mp3_frame_window(shifted.data() + k_shift, shifted.size() - k_shift, window));
    CHECK(window.bytes == 3 * 417);
    CHECK(window.samples == 3 * 1152);

    /* A single header is not trusted: without the following header the window
     * fails closed instead of measuring a truncated frame. */
    const auto single = make_cbr(1);
    CHECK(!cyberdeck_music::mp3_frame_window(single.data(), single.size(), window));
    CHECK(window.bytes == 0 && window.samples == 0);

    /* A truncated tail frame is skipped; the complete ones are still summed. */
    auto truncated = make_cbr(3);
    truncated.resize(truncated.size() + 100, 0);
    CHECK(cyberdeck_music::mp3_frame_window(truncated.data(), truncated.size(), window));
    CHECK(window.bytes == 3 * 417);
    CHECK(window.samples == 3 * 1152);

    /* A window with one complete frame plus the header of the next one is
     * enough: the 320 kbps frame is 1044 B, so 2048 B cannot hold two. */
    const auto high = make_stream(k_mpeg1, 14, 0, k_joint_stereo, 2);
    CHECK(cyberdeck_music::mp3_frame_window(high.data(), 2048, window));
    CHECK(window.bytes == 1044);
    CHECK(window.samples == 1152);
}

/* The head of a VBR file is not representative: 40 frames at 128 kbps followed
 * by 200 frames at 64 kbps.  The head-only estimate of `mp3_probe_bytes` is more
 * than 30 % low; the windows spread by `mp3_probe_fd` must land within 3 %. */
void test_distributed_vbr_sampling() {
    std::vector<std::uint8_t> stream;
    const auto append = [&stream](unsigned bitrate_index, unsigned frames) {
        const auto block = make_stream(k_mpeg1, bitrate_index, 0, k_joint_stereo, frames);
        stream.insert(stream.end(), block.begin(), block.end());
    };
    append(9, 40);   /* 128 kbps, 417-byte frames */
    append(5, 200);  /* 64 kbps, 208-byte frames */
    CHECK(stream.size() == 40 * 417 + 200 * 208);
    const std::uint64_t audio_bytes = stream.size();
    const std::uint64_t expected = 240 * 1152 * 1000 / 44100; /* 6269 ms */
    const std::uint32_t head_only = probe_duration(stream, audio_bytes);
    CHECK(head_only < expected * 70 / 100);
    const std::uint32_t distributed = probe_fd_duration(stream, 0);
    CHECK(distributed > 0);
    const long long error = static_cast<long long>(distributed) - static_cast<long long>(expected);
    const long long tolerance = static_cast<long long>(expected * 3 / 100);
    CHECK(error >= -tolerance && error <= tolerance);

    /* A file whose head is not a frame fails closed (the caller keeps its
     * fallback), and so does a payload with no valid frame at all. */
    std::vector<std::uint8_t> garbage;
    garbage.reserve(3 + stream.size());
    garbage.push_back(0x12);
    garbage.push_back(0x34);
    garbage.push_back(0x56);
    garbage.insert(garbage.end(), stream.begin(), stream.end());
    CHECK(probe_fd_duration(garbage, 0) == 0);
    const std::vector<std::uint8_t> zeros(8192, 0);
    CHECK(probe_fd_duration(zeros, 0) == 0);
}

/* Seek start: the Xing TOC when the probe found one, else the linear mapping.
 * `toc[i] = i` keeps the entries monotonic; `toc[50] = 128` gives the known
 * half-track byte used by the exact-value assertions. */
void test_seek_target() {
    using cyberdeck_music::mp3_seek_point;
    cyberdeck_music::mp3_probe probe{};
    probe.duration_ms = 200000;
    probe.has_toc = true;
    for (std::size_t index = 0; index < probe.toc.size(); ++index)
        probe.toc[index] = static_cast<std::uint8_t>(index);
    probe.toc[50] = 128;

    /* 50% of a 200 s track with a 100000-byte payload: toc[50] = 128 maps to
     * 128 * 100000 / 256 = 50000. */
    const mp3_seek_point half = cyberdeck_music::mp3_seek_target(probe, 0, 100000, 100000);
    CHECK(half.byte == 50000);
    CHECK(half.position_ms == 100000);

    /* The offset is the ID3-skipped start of the audio and is always added. */
    const mp3_seek_point offset_half = cyberdeck_music::mp3_seek_target(probe, 44, 100000, 100000);
    CHECK(offset_half.byte == 50044);
    CHECK(offset_half.position_ms == 100000);

    /* Target 0 starts at the first TOC entry (toc[0] == 0 here). */
    const mp3_seek_point start = cyberdeck_music::mp3_seek_target(probe, 44, 100000, 0);
    CHECK(start.byte == 44);
    CHECK(start.position_ms == 0);

    /* Target == duration and anything beyond it clamp the percent to 99. */
    const mp3_seek_point end = cyberdeck_music::mp3_seek_target(probe, 44, 100000, 200000);
    CHECK(end.byte == 44 + std::uint64_t(99) * 100000 / 256);
    CHECK(end.position_ms == 198000);
    const mp3_seek_point beyond = cyberdeck_music::mp3_seek_target(probe, 44, 100000, 500000);
    CHECK(beyond.byte == end.byte);
    CHECK(beyond.position_ms == end.position_ms);
    CHECK(beyond.position_ms <= 500000);

    /* Every percent stays inside the audio payload, grows monotonically and
     * never claims a position ahead of the requested time. */
    cyberdeck_music::mp3_probe increasing{};
    increasing.duration_ms = 200000;
    increasing.has_toc = true;
    for (std::size_t index = 0; index < increasing.toc.size(); ++index)
        increasing.toc[index] = static_cast<std::uint8_t>(index * 2);
    bool inside = true;
    bool monotonic = true;
    std::uint64_t previous_byte = 0;
    std::uint32_t previous_position = 0;
    for (std::uint32_t target = 0; target <= 200000; target += 2000) {
        const mp3_seek_point point =
            cyberdeck_music::mp3_seek_target(increasing, 44, 100000, target);
        inside = inside && point.byte >= 44 && point.byte <= 44 + 100000;
        monotonic = monotonic && point.byte >= previous_byte && point.position_ms >= previous_position &&
                    point.position_ms <= target;
        previous_byte = point.byte;
        previous_position = point.position_ms;
    }
    CHECK(inside);
    CHECK(monotonic);

    /* Without a TOC the mapping stays linear (exact for CBR) and the position
     * needs no correction. */
    cyberdeck_music::mp3_probe cbr{};
    cbr.duration_ms = 200000;
    const mp3_seek_point linear = cyberdeck_music::mp3_seek_target(cbr, 44, 100000, 100000);
    CHECK(linear.byte == 44 + 50000);
    CHECK(linear.position_ms == 100000);
    const mp3_seek_point linear_zero = cyberdeck_music::mp3_seek_target(cbr, 44, 100000, 0);
    CHECK(linear_zero.byte == 44);
    CHECK(linear_zero.position_ms == 0);
    const mp3_seek_point linear_end = cyberdeck_music::mp3_seek_target(cbr, 44, 100000, 200000);
    CHECK(linear_end.byte == 44 + 100000);
    CHECK(linear_end.position_ms == 200000);

    /* A target past the total is clamped to the payload end exactly like the TOC
     * branch, so the byte can never run past `offset + audio_bytes`; the largest
     * possible target cannot overflow the proportional product. */
    const mp3_seek_point linear_beyond = cyberdeck_music::mp3_seek_target(cbr, 44, 100000, 500000);
    CHECK(linear_beyond.byte == 44 + 100000);
    CHECK(linear_beyond.position_ms == 500000);
    const mp3_seek_point linear_max =
        cyberdeck_music::mp3_seek_target(cbr, 44, 100000, UINT32_MAX);
    CHECK(linear_max.byte == 44 + 100000);
    CHECK(linear_max.byte >= 44);

    /* Unknown duration fails closed: no byte and no position are guessed. */
    cyberdeck_music::mp3_probe unknown{};
    unknown.has_toc = true;
    unknown.toc = probe.toc;
    const mp3_seek_point unknown_point = cyberdeck_music::mp3_seek_target(unknown, 44, 100000, 100000);
    CHECK(unknown_point.byte == 44);
    CHECK(unknown_point.position_ms == 0);
    const mp3_seek_point unknown_linear =
        cyberdeck_music::mp3_seek_target(cyberdeck_music::mp3_probe{}, 7, 0, 5000);
    CHECK(unknown_linear.byte == 7);
    CHECK(unknown_linear.position_ms == 0);
}

void test_fail_closed() {
    const auto valid = make_cbr(4);
    CHECK(cyberdeck_music::mp3_probe_bytes(nullptr, 0, 1000).duration_ms == 0);
    CHECK(cyberdeck_music::mp3_probe_bytes(valid.data(), 3, 1000).duration_ms == 0);
    const std::vector<std::uint8_t> zeros(417, 0);
    CHECK(probe_duration(zeros, zeros.size()) == 0);
    CHECK(probe_duration(valid, 0) == 0);

    const auto header_with = [](unsigned version, unsigned layer, unsigned bitrate_index,
                                unsigned hz_index) {
        std::vector<std::uint8_t> data(417, 0);
        write_header(data.data(), version, layer, bitrate_index, hz_index, k_joint_stereo, false);
        return data;
    };
    CHECK(probe_duration(header_with(k_mpeg1, k_layer3, 0, 0), 417) == 0);   /* free bitrate */
    CHECK(probe_duration(header_with(k_mpeg1, k_layer3, 15, 0), 417) == 0);  /* invalid bitrate */
    CHECK(probe_duration(header_with(k_mpeg1, 3, 9, 0), 417) == 0);          /* Layer I */
    CHECK(probe_duration(header_with(k_mpeg1, 2, 9, 0), 417) == 0);          /* Layer II */
    CHECK(probe_duration(header_with(k_mpeg1, k_layer3, 9, 3), 417) == 0);   /* reserved rate */
    CHECK(probe_duration(header_with(1, k_layer3, 9, 0), 417) == 0);         /* reserved version */

    CHECK(probe_duration(make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 0, false), 417) == 0);
    CHECK(probe_duration(make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 0xFFFFFFFFU, false), 417) == 0);
    CHECK(probe_duration(make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 0x1000000U, false), 417) == 0);
}

void test_xing_without_frames_flag() {
    auto data = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false);
    const std::size_t tag_offset = 4 + side_info_bytes(k_mpeg1, k_joint_stereo);
    write_be32(data.data() + tag_offset + 4, 0x2); /* only the stream size is present */
    CHECK(probe_duration(data, data.size()) == 0);
}

void test_truncated_tail_frame() {
    auto data = make_cbr(5);
    data.resize(data.size() + 100, 0); /* incomplete sixth frame header area */
    const std::uint32_t duration = probe_duration(data, data.size());
    CHECK(duration > 0);
    /* 5 complete frames (2085 bytes, 5760000 sample-ms) over 2185 audio bytes. */
    CHECK(duration == 136);
}

/* The average estimator must use the samples-per-frame of the stream version:
 * MPEG-2/2.5 carry 576 samples per frame, half of MPEG-1. */
void test_average_other_versions() {
    /* MPEG-2 22.05 kHz / 64 kbps: 208-byte frames, 100 frames = 2612 ms. */
    const auto mpeg2 = make_stream(k_mpeg2, 8, 0, k_mono, 100);
    CHECK(mpeg2.size() == 20800);
    CHECK(probe_duration(mpeg2, mpeg2.size()) == 2612);
    /* MPEG-2.5 8 kHz / 16 kbps: 144-byte frames (72 ms), 100 frames = 7200 ms. */
    const auto mpeg25 = make_stream(k_mpeg25, 8, 2, k_mono, 100);
    CHECK(mpeg25.size() == 57600);
    CHECK(probe_duration(mpeg25, mpeg25.size()) == 7200);
    /* MPEG-1 48 kHz / 320 kbps: 960-byte frames, 50 frames = 1200 ms. */
    const auto mpeg1_48k = make_stream(k_mpeg1, 14, 1, k_joint_stereo, 50);
    CHECK(mpeg1_48k.size() == 48000);
    CHECK(probe_duration(mpeg1_48k, mpeg1_48k.size()) == 1200);
}

/* A Xing/Info tag that carries only the TOC is authoritative: without a frame
 * count the duration fails closed, so the seek cannot guess a byte. */
void test_xing_toc_only() {
    auto data = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false);
    const std::size_t tag_offset = 4 + side_info_bytes(k_mpeg1, k_joint_stereo);
    write_be32(data.data() + tag_offset + 4, 0x4); /* only the TOC flag */
    for (std::size_t index = 0; index < 100; ++index)
        data[tag_offset + 8 + index] = static_cast<std::uint8_t>(index);
    const auto probe = cyberdeck_music::mp3_probe_bytes(data.data(), data.size(), data.size());
    CHECK(probe.has_toc);
    CHECK(probe.duration_ms == 0);
    const auto point = cyberdeck_music::mp3_seek_target(probe, 44, 10000, 5000);
    CHECK(point.byte == 44);
    CHECK(point.position_ms == 0);
}

/* A file that holds only the Xing frame: the tag claims 1000 frames, so the
 * reported total is the header value (it cannot be verified from the payload)
 * and every seek still stays inside the audio bytes instead of running past
 * the end of the file. */
void test_xing_only_frame() {
    auto data = make_xing_frame(k_mpeg1, 9, 0, k_joint_stereo, 1000, false);
    const auto probe = cyberdeck_music::mp3_probe_bytes(data.data(), data.size(), data.size());
    CHECK(probe.duration_ms == 26122);
    CHECK(!probe.has_toc);
    const auto end = cyberdeck_music::mp3_seek_target(probe, 0, data.size(), 26122);
    CHECK(end.byte == data.size());
    CHECK(end.position_ms == 26122);
    const auto start = cyberdeck_music::mp3_seek_target(probe, 0, data.size(), 0);
    CHECK(start.byte == 0);
    CHECK(start.position_ms == 0);
}

/* A corrupt or hand-made TOC is not monotonic.  Whatever it says, the read must
 * stay inside `offset .. offset + audio_bytes`. */
void test_non_monotonic_toc() {
    cyberdeck_music::mp3_probe probe{};
    probe.duration_ms = 100000;
    probe.has_toc = true;
    for (std::size_t index = 0; index < probe.toc.size(); ++index)
        probe.toc[index] = static_cast<std::uint8_t>(99 - index); /* descending */
    bool inside = true;
    for (std::uint32_t target = 0; target <= 100000; target += 5000) {
        const auto point = cyberdeck_music::mp3_seek_target(probe, 7, 10000, target);
        inside = inside && point.byte >= 7 && point.byte <= 10007;
        inside = inside && point.position_ms <= target;
    }
    CHECK(inside);

    /* Every entry at 255: target 0 jumps to the last entry, still clamped. */
    probe.toc.fill(255);
    const auto high = cyberdeck_music::mp3_seek_target(probe, 7, 10000, 0);
    CHECK(high.byte == 7 + std::uint64_t(255) * 10000 / 256);
    CHECK(high.byte <= 10007);
    CHECK(high.position_ms == 0);

    /* Every entry at 0: the end target restarts at the first byte. */
    probe.toc.fill(0);
    const auto low = cyberdeck_music::mp3_seek_target(probe, 7, 10000, 100000);
    CHECK(low.byte == 7);
    CHECK(low.position_ms == 99000);

    /* Empty payload: the byte stays at the audio offset. */
    const auto empty = cyberdeck_music::mp3_seek_target(probe, 7, 0, 100000);
    CHECK(empty.byte == 7);
}

/* Linear (no TOC) mapping stays inside the payload for every target the player
 * can request, i.e. `target_ms <= duration_ms` (`seek` clamps it). */
void test_linear_seek_inside_payload() {
    cyberdeck_music::mp3_probe probe{};
    probe.duration_ms = 1000;
    bool inside = true;
    for (std::uint32_t target = 0; target <= probe.duration_ms; target += 25) {
        const auto point = cyberdeck_music::mp3_seek_target(probe, 5, 100, target);
        inside = inside && point.byte >= 5 && point.byte <= 105;
        inside = inside && point.position_ms == target;
    }
    CHECK(inside);
    const auto end = cyberdeck_music::mp3_seek_target(probe, 5, 100, 1000);
    CHECK(end.byte == 105);
    CHECK(end.position_ms == 1000);
}

/* ID3v2 footer flag (bit 0x10 of the flags byte) adds ten bytes after the
 * payload, and a negative descriptor or an offset at EOF is rejected. */
void test_id3_footer_flag() {
    char path[] = "/tmp/music-duration-footer-XXXXXX";
    const int fd = ::mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    const std::uint32_t tag_payload = 5;
    std::vector<std::uint8_t> bytes = id3_header(tag_payload);
    bytes[5] = 0x10; /* footer present */
    /* header + payload + the ten-byte footer, then the audio starts. */
    bytes.resize(10 + tag_payload + 10, 0);
    const auto audio = make_cbr(2);
    bytes.insert(bytes.end(), audio.begin(), audio.end());
    CHECK(::close(fd) == 0);
    CHECK(write_file(bytes, path));

    const int read_fd = ::open(path, O_RDONLY);
    CHECK(read_fd >= 0);
    if (read_fd >= 0) {
        std::uint64_t offset = 0;
        CHECK(cyberdeck_music::mp3_audio_offset(read_fd, bytes.size(), offset));
        CHECK(offset == 10 + tag_payload + 10);
        CHECK(cyberdeck_music::mp3_probe_fd(read_fd, bytes.size(), offset).duration_ms == 52);
        CHECK(cyberdeck_music::mp3_probe_fd(read_fd, bytes.size(), bytes.size()).duration_ms == 0);
        CHECK(::close(read_fd) == 0);
    }
    std::uint64_t offset = 0;
    CHECK(!cyberdeck_music::mp3_audio_offset(-1, 100, offset));
    CHECK(offset == 0);
    CHECK(cyberdeck_music::mp3_probe_fd(-1, 100, 0).duration_ms == 0);
    CHECK(::unlink(path) == 0);
}

void test_id3_offset_and_fd_probe() {
    char path[] = "/tmp/music-duration-test-XXXXXX";
    const int fd = ::mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    const std::uint32_t tag_payload = 20;
    std::vector<std::uint8_t> bytes = id3_header(tag_payload);
    bytes.resize(bytes.size() + tag_payload, 0);
    const auto audio = make_cbr(100);
    bytes.insert(bytes.end(), audio.begin(), audio.end());
    CHECK(::close(fd) == 0);
    CHECK(write_file(bytes, path));

    const int read_fd = ::open(path, O_RDONLY);
    CHECK(read_fd >= 0);
    if (read_fd >= 0) {
        std::uint64_t offset = 999;
        CHECK(cyberdeck_music::mp3_audio_offset(read_fd, bytes.size(), offset));
        CHECK(offset == 10 + tag_payload);
        const off_t position_before = ::lseek(read_fd, 0, SEEK_CUR);
        const auto probe = cyberdeck_music::mp3_probe_fd(read_fd, bytes.size(), offset);
        CHECK(probe.duration_ms == 2612);
        CHECK(::lseek(read_fd, 0, SEEK_CUR) == position_before);
        CHECK(::close(read_fd) == 0);
    }

    /* Invalid ID3v2 version fails closed instead of guessing an offset. */
    auto malformed = id3_header(0);
    malformed[3] = 1;
    malformed.resize(64, 0);
    CHECK(write_file(malformed, path));
    const int bad_fd = ::open(path, O_RDONLY);
    CHECK(bad_fd >= 0);
    if (bad_fd >= 0) {
        std::uint64_t offset = 7;
        CHECK(!cyberdeck_music::mp3_audio_offset(bad_fd, malformed.size(), offset));
        CHECK(offset == 0);
        CHECK(::close(bad_fd) == 0);
    }

    /* Tag larger than the file is rejected. */
    auto oversized = id3_header(4096);
    oversized.resize(10, 0);
    CHECK(write_file(oversized, path));
    const int big_fd = ::open(path, O_RDONLY);
    CHECK(big_fd >= 0);
    if (big_fd >= 0) {
        std::uint64_t offset = 0;
        CHECK(!cyberdeck_music::mp3_audio_offset(big_fd, 10, offset));
        CHECK(::close(big_fd) == 0);
    }

    /* No ID3 tag: the audio starts at zero and the probe still resolves it. */
    CHECK(write_file(audio, path));
    const int plain_fd = ::open(path, O_RDONLY);
    CHECK(plain_fd >= 0);
    if (plain_fd >= 0) {
        std::uint64_t offset = 5;
        CHECK(cyberdeck_music::mp3_audio_offset(plain_fd, audio.size(), offset));
        CHECK(offset == 0);
        CHECK(cyberdeck_music::mp3_probe_fd(plain_fd, audio.size(), 0).duration_ms == 2612);
        CHECK(cyberdeck_music::mp3_probe_fd(plain_fd, 0, 0).duration_ms == 0);
        CHECK(::close(plain_fd) == 0);
    }
    CHECK(::unlink(path) == 0);
}
} // namespace

int main() {
    test_cbr_average();
    test_vbr_average();
    test_xing_durations();
    test_xing_toc();
    test_seek_target();
    test_fail_closed();
    test_xing_without_frames_flag();
    test_truncated_tail_frame();
    test_average_other_versions();
    test_xing_toc_only();
    test_xing_only_frame();
    test_non_monotonic_toc();
    test_linear_seek_inside_payload();
    test_frame_window();
    test_distributed_vbr_sampling();
    test_id3_offset_and_fd_probe();
    test_id3_footer_flag();
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
