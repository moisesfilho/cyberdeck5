/* Streaming ID3v2 reader against real temp files (TEST-MUSIC-META-READ-*).
 *
 * The production reader is linked with `--wrap=pread/open/openat/dup/close`, so
 * this test observes the bytes, offsets and descriptor it uses without touching
 * the implementation. `operator new` is replaced locally to prove that a failed
 * allocation rejects the metadata and that the heap block does not grow with the
 * tag. Fixtures are generated from a structural description: the artwork payload
 * is zeroed and no audio or real cover art is stored in the repository. */

#include "apps/music/cyberdeck_music_metadata.h"
#include "apps/music/cyberdeck_music_model.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
int checks = 0;
int failures = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; \
    } \
} while (false)

/* Allocation probe: the reader must use one fixed window regardless of the tag
 * size, and a failed allocation must leave the output untouched. */
bool fail_allocations = false;
int allocation_calls = 0;
std::size_t largest_allocation = 0;
} // namespace

void *operator new(std::size_t size) {
    ++allocation_calls;
    largest_allocation = std::max(largest_allocation, size);
    if (fail_allocations)
        throw std::bad_alloc();
    void *ptr = std::malloc(size == 0 ? 1 : size);
    if (ptr == nullptr)
        throw std::bad_alloc();
    return ptr;
}
void *operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void *ptr) noexcept {
    std::free(ptr);
}
void operator delete[](void *ptr) noexcept {
    std::free(ptr);
}
void operator delete(void *ptr, std::size_t) noexcept {
    std::free(ptr);
}
void operator delete[](void *ptr, std::size_t) noexcept {
    std::free(ptr);
}

/* --- descriptor probe ------------------------------------------------------- */

extern "C" {
ssize_t __real_pread(int fd, void *buffer, std::size_t count, off_t offset);
int __real_open(const char *path, int flags, ...);
int __real_openat(int dirfd, const char *path, int flags, ...);
int __real_dup(int fd);
int __real_close(int fd);
}

namespace {
struct range {
    std::size_t begin = 0;
    std::size_t end = 0;
};

enum class fault { none, short_read, zero, error };

struct descriptor_probe {
    bool active = false;
    int expected_fd = -1;
    bool wrong_fd = false;
    int calls = 0;
    int open_calls = 0;
    int openat_calls = 0;
    int dup_calls = 0;
    int close_calls = 0;
    std::size_t total = 0;
    std::size_t largest = 0;
    std::size_t highest_end = 0;
    std::size_t last_offset = 0;
    bool decreasing = false;
    int range_count = 0;
    bool range_overflow = false;
    std::array<range, 64> ranges{};
    fault injected = fault::none;
    int fault_after = 0;

    bool overlaps(std::size_t begin, std::size_t end) const {
        for (int i = 0; i < range_count; ++i)
            if (ranges[i].begin < end && begin < ranges[i].end)
                return true;
        return false;
    }
};

descriptor_probe probe;

void reset_probe(int expected_fd) {
    probe = descriptor_probe{};
    probe.active = true;
    probe.expected_fd = expected_fd;
}

void disarm_probe() {
    probe.active = false;
}
} // namespace

extern "C" ssize_t __wrap_pread(int fd, void *buffer, std::size_t count, off_t offset) {
    if (probe.active) {
        ++probe.calls;
        probe.total += count;
        probe.largest = std::max(probe.largest, count);
        if (probe.expected_fd >= 0 && fd != probe.expected_fd)
            probe.wrong_fd = true;
        const auto begin = static_cast<std::size_t>(offset);
        const auto end = begin + count;
        probe.highest_end = std::max(probe.highest_end, end);
        if (probe.calls > 1 && begin < probe.last_offset)
            probe.decreasing = true;
        probe.last_offset = begin;
        if (probe.range_count < static_cast<int>(probe.ranges.size()))
            probe.ranges[static_cast<std::size_t>(probe.range_count++)] = range{begin, end};
        else
            probe.range_overflow = true;
        const bool inject = probe.injected != fault::none && probe.calls > probe.fault_after;
        if (inject) {
            switch (probe.injected) {
            case fault::zero:
                return 0;
            case fault::error:
                errno = EIO;
                return -1;
            case fault::short_read: {
                const ssize_t got = __real_pread(fd, buffer, count, offset);
                return got > 0 ? got - 1 : got;
            }
            case fault::none:
                break;
            }
        }
    }
    return __real_pread(fd, buffer, count, offset);
}

extern "C" int __wrap_open(const char *path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    const mode_t mode = (flags & O_CREAT) != 0 ? static_cast<mode_t>(va_arg(args, int)) : mode_t{0};
    va_end(args);
    if (probe.active)
        ++probe.open_calls;
    return __real_open(path, flags, mode);
}

extern "C" int __wrap_openat(int dirfd, const char *path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    const mode_t mode = (flags & O_CREAT) != 0 ? static_cast<mode_t>(va_arg(args, int)) : mode_t{0};
    va_end(args);
    if (probe.active)
        ++probe.openat_calls;
    return __real_openat(dirfd, path, flags, mode);
}

extern "C" int __wrap_dup(int fd) {
    if (probe.active)
        ++probe.dup_calls;
    return __real_dup(fd);
}

extern "C" int __wrap_close(int fd) {
    if (probe.active)
        ++probe.close_calls;
    return __real_close(fd);
}

namespace {
using cyberdeck_music::k_id3_max_tag_bytes;
using cyberdeck_music::k_id3_read_window_bytes;
using cyberdeck_music::k_id3v2_header_bytes;
using cyberdeck_music::metadata;

/* --- fixture builders ------------------------------------------------------- */

void append_syncsafe(std::string &out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 21) & 0x7f));
    out.push_back(static_cast<char>((value >> 14) & 0x7f));
    out.push_back(static_cast<char>((value >> 7) & 0x7f));
    out.push_back(static_cast<char>(value & 0x7f));
}

std::string wrap_tag(unsigned version, std::string_view frames, unsigned char header_flags = 0) {
    std::string bytes = "ID3";
    bytes.push_back(static_cast<char>(version));
    bytes.push_back('\0');
    bytes.push_back(static_cast<char>(header_flags));
    append_syncsafe(bytes, static_cast<std::uint32_t>(frames.size()));
    bytes += frames;
    return bytes;
}

void append_raw_frame(std::string &tag, unsigned version, std::string_view id, std::string_view payload,
                      unsigned char flag1 = 0, unsigned char flag2 = 0) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    tag.append(id);
    if (version == 2) {
        tag.push_back(static_cast<char>((size >> 16) & 0xff));
        tag.push_back(static_cast<char>((size >> 8) & 0xff));
        tag.push_back(static_cast<char>(size & 0xff));
    } else {
        if (version == 4)
            append_syncsafe(tag, size);
        else {
            tag.push_back(static_cast<char>((size >> 24) & 0xff));
            tag.push_back(static_cast<char>((size >> 16) & 0xff));
            tag.push_back(static_cast<char>((size >> 8) & 0xff));
            tag.push_back(static_cast<char>(size & 0xff));
        }
        tag.push_back(static_cast<char>(flag1));
        tag.push_back(static_cast<char>(flag2));
    }
    tag += payload;
}

/* UTF-16 with a little-endian BOM and the two-byte terminator, the encoding of
 * the reported file. */
std::string utf16_payload(std::string_view text) {
    std::string payload;
    payload.push_back('\x01');
    payload.push_back('\xff');
    payload.push_back('\xfe');
    for (char c : text) {
        payload.push_back(c);
        payload.push_back('\0');
    }
    payload.push_back('\0');
    payload.push_back('\0');
    return payload;
}

std::string utf8_payload(std::string_view text) {
    return std::string(1, '\x03') + std::string(text);
}

/* Artwork frame with zeroed picture bytes of the exact size the metadata must
 * skip. */
std::string artwork_payload(std::size_t size) {
    std::string payload = std::string(1, '\x00') + "image/jpeg" + std::string(1, '\0') + std::string(1, '\x03') +
                          std::string(1, '\0');
    if (payload.size() < size)
        payload.append(size - payload.size(), '\0');
    return payload;
}

std::string pad_to_payload(std::string frames, std::size_t payload_bytes) {
    if (frames.size() < payload_bytes)
        frames.append(payload_bytes - frames.size(), '\0');
    return frames;
}

/* Reported geometries of the file that motivated the fix (provenance, not
 * production constants): payload 254209, frames in this order, artwork 252112,
 * padding 1757. */
constexpr std::size_t k_reported_payload = 254209;
constexpr std::size_t k_reported_artwork = 252112;
constexpr std::size_t k_reported_padding = 1757;

std::string reported_text_frames() {
    std::string frames;
    const std::string album = utf16_payload("Kid Cosmic and the Sonic Courage");
    const std::string artist = utf16_payload("Dr. Fang and The Gang");
    const std::string title = utf16_payload(" The Kid (Kid Cosmic Theme)");
    CHECK(album.size() == 69);
    CHECK(artist.size() == 47);
    CHECK(title.size() == 59);
    append_raw_frame(frames, 3, "TALB", album);
    append_raw_frame(frames, 3, "TPE1", utf16_payload("Dr. Fang and The Gang"));
    append_raw_frame(frames, 3, "TPE2", utf16_payload("Dr. Fang and The Gang"));
    append_raw_frame(frames, 3, "TSSE", utf16_payload("Lavf58.76.10"));
    append_raw_frame(frames, 3, "TIT2", title);
    /* Track and year frames are not decoded: they only have to occupy the three
     * and six payload bytes of the reported geometry. */
    append_raw_frame(frames, 3, "TRCK", std::string("\x01" "1\0", 3));
    append_raw_frame(frames, 3, "TYER", std::string("\x00" "2021\0", 6));
    return frames;
}

std::string artwork_frame(unsigned version) {
    return [&] {
        std::string frame;
        append_raw_frame(frame, version, version == 2 ? "PIC" : "APIC", artwork_payload(k_reported_artwork));
        return frame;
    }();
}

std::string reported_tag() {
    std::string frames = reported_text_frames();
    frames += artwork_frame(3);
    CHECK(frames.size() == k_reported_payload - k_reported_padding);
    frames = pad_to_payload(frames, k_reported_payload);
    const std::string tag = wrap_tag(3, frames);
    CHECK(tag.size() == k_reported_payload + k_id3v2_header_bytes);
    return tag;
}

std::string apic_first_tag() {
    std::string frames = artwork_frame(3);
    frames += reported_text_frames();
    frames = pad_to_payload(frames, k_reported_payload);
    return wrap_tag(3, frames);
}

/* Total tag bytes exactly at the ceiling, texts after a nearly ceiling-sized
 * artwork frame. */
std::string capped_tag() {
    std::string texts;
    append_raw_frame(texts, 3, "TIT2", utf8_payload("Cap title"));
    append_raw_frame(texts, 3, "TPE1", utf8_payload("Cap artist"));
    append_raw_frame(texts, 3, "TALB", utf8_payload("Cap album"));
    const std::size_t artwork = k_id3_max_tag_bytes - k_id3v2_header_bytes - texts.size() - 10;
    std::string frame;
    append_raw_frame(frame, 3, "APIC", artwork_payload(artwork));
    std::string frames = frame + texts;
    CHECK(frames.size() + k_id3v2_header_bytes == k_id3_max_tag_bytes);
    return wrap_tag(3, frames);
}

std::string over_cap_tag() {
    std::string texts;
    append_raw_frame(texts, 3, "TIT2", utf8_payload("Over title"));
    append_raw_frame(texts, 3, "TPE1", utf8_payload("Over artist"));
    append_raw_frame(texts, 3, "TALB", utf8_payload("Over album"));
    /* Complete text frames first, then enough padding for a total of one byte
     * above the ceiling: nothing may be published from them. */
    std::string frames = texts;
    frames.append(k_id3_max_tag_bytes - k_id3v2_header_bytes + 1 - texts.size(), '\0');
    return wrap_tag(3, frames);
}

std::string small_tag() {
    std::string frames;
    append_raw_frame(frames, 3, "TIT2", utf8_payload("Small title"));
    append_raw_frame(frames, 3, "TPE1", utf8_payload("Small artist"));
    append_raw_frame(frames, 3, "TALB", utf8_payload("Small album"));
    return wrap_tag(3, frames);
}

/* --- file helpers ----------------------------------------------------------- */

/* The fixture only needs a writable base: TMPDIR when the harness sets it,
 * otherwise the standard /tmp. No CI-specific directory is required here. */
const char *temp_dir_base() {
    const char *tmpdir = std::getenv("TMPDIR");
    return (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
}

std::string make_temp_dir() {
    std::string pattern = std::string(temp_dir_base()) + "/music-metadata-io-XXXXXX";
    char *created = ::mkdtemp(pattern.data());
    return created != nullptr ? std::string(created) : std::string();
}

bool write_file(const std::string &path, std::string_view bytes) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t written = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (written <= 0) {
            ::close(fd);
            return false;
        }
        done += static_cast<std::size_t>(written);
    }
    return ::close(fd) == 0;
}

void reset_tags(metadata &out) {
    std::strcpy(out.title.data(), "keep-title");
    std::strcpy(out.artist.data(), "keep-artist");
    std::strcpy(out.album.data(), "keep-album");
}

bool preserved(const metadata &out) {
    return std::string_view(out.title.data()) == "keep-title" &&
           std::string_view(out.artist.data()) == "keep-artist" &&
           std::string_view(out.album.data()) == "keep-album";
}

/* Reads `path` through the probe and returns what the reader published. The
 * descriptor stays open for the caller. */
metadata read_probed(const std::string &path, std::size_t size) {
    metadata tags{};
    reset_tags(tags);
    const int fd = ::open(path.c_str(), O_RDONLY);
    CHECK(fd >= 0);
    reset_probe(fd);
    cyberdeck_music::read_metadata(fd, size, tags);
    disarm_probe();
    CHECK(::close(fd) == 0);
    return tags;
}

/* --- tests ------------------------------------------------------------------ */

void test_read_same_fd_keeps_position(const std::string &dir) {
    const std::string path = dir + "/reported.mp3";
    const std::string bytes = reported_tag();
    CHECK(write_file(path, bytes));
    const int fd = ::open(path.c_str(), O_RDONLY);
    CHECK(fd >= 0);
    CHECK(::lseek(fd, 1234, SEEK_SET) == 1234);
    metadata tags{};
    reset_tags(tags);
    reset_probe(fd);
    cyberdeck_music::read_metadata(fd, bytes.size(), tags);
    disarm_probe();
    CHECK(std::string_view(tags.title.data()) == " The Kid (Kid Cosmic Theme)");
    CHECK(std::string_view(tags.artist.data()) == "Dr. Fang and The Gang");
    CHECK(std::string_view(tags.album.data()) == "Kid Cosmic and the Sonic Courage");
    CHECK(!probe.wrong_fd);
    CHECK(probe.open_calls == 0);
    CHECK(probe.openat_calls == 0);
    CHECK(probe.dup_calls == 0);
    CHECK(probe.close_calls == 0);
    CHECK(probe.calls > 0);
    CHECK(::lseek(fd, 0, SEEK_CUR) == 1234);

    /* Repeated calls on the same descriptor are idempotent and still never
     * reopen it. */
    for (int repeat = 0; repeat < 2; ++repeat) {
        metadata again{};
        reset_tags(again);
        reset_probe(fd);
        cyberdeck_music::read_metadata(fd, bytes.size(), again);
        disarm_probe();
        CHECK(std::string_view(again.title.data()) == " The Kid (Kid Cosmic Theme)");
        CHECK(std::string_view(again.artist.data()) == "Dr. Fang and The Gang");
        CHECK(std::string_view(again.album.data()) == "Kid Cosmic and the Sonic Courage");
        CHECK(probe.open_calls == 0 && probe.dup_calls == 0);
        CHECK(!probe.wrong_fd);
    }
    CHECK(::lseek(fd, 0, SEEK_CUR) == 1234);
    CHECK(::close(fd) == 0);
}

void test_read_skips_artwork(const std::string &dir) {
    const std::string bytes = reported_tag();
    const std::string path = dir + "/reported-bounded.mp3";
    CHECK(write_file(path, bytes));
    const metadata tags = read_probed(path, bytes.size());
    CHECK(std::string_view(tags.title.data()) == " The Kid (Kid Cosmic Theme)");
    /* No frame beyond three target payloads is loaded: the artwork payload is
     * never read, and no single read exceeds the declared window. */
    const std::size_t artwork_begin = k_id3v2_header_bytes + 330 + 10;
    const std::size_t artwork_end = artwork_begin + k_reported_artwork;
    CHECK(probe.largest <= k_id3_read_window_bytes);
    CHECK(probe.total < k_reported_artwork);
    CHECK(probe.total <= k_id3_read_window_bytes * 16);
    CHECK(probe.highest_end <= std::min(bytes.size(), k_id3_max_tag_bytes));
    CHECK(!probe.decreasing);
    CHECK(!probe.range_overflow);
    CHECK(!probe.overlaps(artwork_begin, artwork_end));

    /* Artwork first: the bound does not depend on the frame order. */
    const std::string first_bytes = apic_first_tag();
    const std::string first_path = dir + "/apic-first.mp3";
    CHECK(write_file(first_path, first_bytes));
    const metadata first = read_probed(first_path, first_bytes.size());
    CHECK(std::string_view(first.title.data()) == " The Kid (Kid Cosmic Theme)");
    CHECK(std::string_view(first.artist.data()) == "Dr. Fang and The Gang");
    CHECK(std::string_view(first.album.data()) == "Kid Cosmic and the Sonic Courage");
    CHECK(probe.largest <= k_id3_read_window_bytes);
    CHECK(probe.total < k_reported_artwork);
    /* Skipping the artwork from the first frame must not read into it. */
    CHECK(!probe.overlaps(k_id3v2_header_bytes + 10, k_id3v2_header_bytes + 10 + k_reported_artwork));

    /* A huge target frame is read only up to the window and the text is
     * truncated at the output capacity. */
    std::string huge_frames;
    std::string huge_text = utf8_payload(std::string(300 * 1024, 'T'));
    append_raw_frame(huge_frames, 3, "TIT2", huge_text);
    append_raw_frame(huge_frames, 3, "APIC", artwork_payload(k_reported_artwork));
    const std::string huge = wrap_tag(3, huge_frames);
    const std::string huge_path = dir + "/huge-title.mp3";
    CHECK(write_file(huge_path, huge));
    const metadata huge_tags = read_probed(huge_path, huge.size());
    CHECK(std::strlen(huge_tags.title.data()) == cyberdeck_music::k_max_title_bytes);
    CHECK(probe.largest <= k_id3_read_window_bytes);
    CHECK(probe.total < huge_text.size());
}

void test_read_cap_boundaries(const std::string &dir) {
    const std::string capped = capped_tag();
    CHECK(capped.size() == k_id3_max_tag_bytes);
    const std::string capped_path = dir + "/cap.mp3";
    CHECK(write_file(capped_path, capped));
    const metadata at_cap = read_probed(capped_path, capped.size());
    CHECK(std::string_view(at_cap.title.data()) == "Cap title");
    CHECK(std::string_view(at_cap.artist.data()) == "Cap artist");
    CHECK(std::string_view(at_cap.album.data()) == "Cap album");
    CHECK(probe.largest <= k_id3_read_window_bytes);
    CHECK(probe.total < k_id3_max_tag_bytes / 2);

    const std::string over = over_cap_tag();
    CHECK(over.size() == k_id3_max_tag_bytes + 1);
    const std::string over_path = dir + "/cap-over.mp3";
    CHECK(write_file(over_path, over));
    const metadata rejected = read_probed(over_path, over.size());
    CHECK(preserved(rejected));
    CHECK(probe.calls <= 1);
}

void test_read_short_io(const std::string &dir) {
    const std::string bytes = reported_tag();
    const std::string path = dir + "/short-io.mp3";
    CHECK(write_file(path, bytes));
    const int fd = ::open(path.c_str(), O_RDONLY);
    CHECK(fd >= 0);

    const std::pair<fault, int> faults[] = {
        {fault::short_read, 0}, /* the tag header itself */
        {fault::short_read, 1}, /* first frame header */
        {fault::short_read, 4}, /* a text payload */
        {fault::zero, 1},
        {fault::error, 2},
    };
    for (const auto &[kind, after] : faults) {
        metadata tags{};
        reset_tags(tags);
        reset_probe(fd);
        probe.injected = kind;
        probe.fault_after = after;
        cyberdeck_music::read_metadata(fd, bytes.size(), tags);
        disarm_probe();
        CHECK(preserved(tags));
    }

    /* The descriptor reports the size from before a truncation: the reader must
     * notice the missing bytes instead of parsing a partial frame. The full
     * fixture is not read here, so the sentinels stay untouched. */
    const std::string short_path = dir + "/truncated.mp3";
    CHECK(write_file(short_path, std::string_view(bytes).substr(0, 2048)));
    const int short_fd = ::open(short_path.c_str(), O_RDONLY);
    CHECK(short_fd >= 0);
    metadata truncated_tags{};
    reset_tags(truncated_tags);
    reset_probe(short_fd);
    cyberdeck_music::read_metadata(short_fd, bytes.size(), truncated_tags);
    disarm_probe();
    CHECK(preserved(truncated_tags));
    CHECK(::close(short_fd) == 0);
    CHECK(::close(fd) == 0);
}

void test_read_allocation_failure(const std::string &dir) {
    const std::string small = small_tag();
    const std::string capped = capped_tag();
    const std::string small_path = dir + "/alloc-small.mp3";
    const std::string capped_path = dir + "/alloc-cap.mp3";
    CHECK(write_file(small_path, small));
    CHECK(write_file(capped_path, capped));

    /* A failed allocation rejects the metadata instead of publishing it. */
    metadata tags{};
    reset_tags(tags);
    const int fd = ::open(small_path.c_str(), O_RDONLY);
    CHECK(fd >= 0);
    fail_allocations = true;
    cyberdeck_music::read_metadata(fd, small.size(), tags);
    fail_allocations = false;
    CHECK(preserved(tags));
    CHECK(::close(fd) == 0);

    /* The heap block is the same for a compact tag and for a tag at the
     * ceiling: it never grows with the tag. */
    metadata compact{};
    reset_tags(compact);
    const int compact_fd = ::open(small_path.c_str(), O_RDONLY);
    CHECK(compact_fd >= 0);
    allocation_calls = 0;
    largest_allocation = 0;
    cyberdeck_music::read_metadata(compact_fd, small.size(), compact);
    const int compact_calls = allocation_calls;
    const std::size_t compact_largest = largest_allocation;
    CHECK(::close(compact_fd) == 0);
    CHECK(std::string_view(compact.title.data()) == "Small title");

    metadata wide{};
    reset_tags(wide);
    const int wide_fd = ::open(capped_path.c_str(), O_RDONLY);
    CHECK(wide_fd >= 0);
    allocation_calls = 0;
    largest_allocation = 0;
    cyberdeck_music::read_metadata(wide_fd, capped.size(), wide);
    const int wide_calls = allocation_calls;
    const std::size_t wide_largest = largest_allocation;
    CHECK(::close(wide_fd) == 0);
    CHECK(std::string_view(wide.title.data()) == "Cap title");
    CHECK(compact_calls == wide_calls);
    CHECK(compact_largest == wide_largest);
    CHECK(wide_largest <= k_id3_read_window_bytes);
}

void test_read_versions_and_flags(const std::string &dir) {
    /* ID3v2.2 (three-character ids, six-byte frame headers) and ID3v2.4
     * (syncsafe frame sizes), both with artwork before the texts. */
    for (unsigned version : {2U, 4U}) {
        const std::string_view title_id = version == 2 ? "TT2" : "TIT2";
        const std::string_view artist_id = version == 2 ? "TP1" : "TPE1";
        const std::string_view album_id = version == 2 ? "TAL" : "TALB";
        std::string frames;
        append_raw_frame(frames, version, version == 2 ? "PIC" : "APIC", artwork_payload(k_reported_artwork));
        append_raw_frame(frames, version, title_id, utf8_payload("Version title"));
        append_raw_frame(frames, version, artist_id, utf8_payload("Version artist"));
        append_raw_frame(frames, version, album_id, utf8_payload("Version album"));
        const std::string bytes = wrap_tag(version, frames);
        const std::string path = dir + "/version-" + std::to_string(version) + ".mp3";
        CHECK(write_file(path, bytes));
        const metadata tags = read_probed(path, bytes.size());
        CHECK(std::string_view(tags.title.data()) == "Version title");
        CHECK(std::string_view(tags.artist.data()) == "Version artist");
        CHECK(std::string_view(tags.album.data()) == "Version album");
        CHECK(probe.largest <= k_id3_read_window_bytes);
        CHECK(!probe.overlaps(k_id3v2_header_bytes + (version == 2 ? 6 : 10),
                              k_id3v2_header_bytes + (version == 2 ? 6 : 10) + k_reported_artwork));
    }

    /* Unsynchronisation and the extended header move the payload: fail closed. */
    std::string frames;
    append_raw_frame(frames, 3, "TIT2", utf8_payload("Flagged title"));
    for (unsigned char flag : {0x80, 0x40, 0x01}) {
        const std::string bytes = wrap_tag(3, frames, flag);
        const std::string path = dir + "/header-flag-" + std::to_string(flag) + ".mp3";
        CHECK(write_file(path, bytes));
        const metadata tags = read_probed(path, bytes.size());
        CHECK(preserved(tags));
    }

    /* Experimental (2.3) and footer (2.4) do not move the payload. */
    const std::string experimental = wrap_tag(3, frames, 0x20);
    const std::string experimental_path = dir + "/experimental.mp3";
    CHECK(write_file(experimental_path, experimental));
    CHECK(std::string_view(read_probed(experimental_path, experimental.size()).title.data()) == "Flagged title");
    const std::string footer = wrap_tag(4, frames, 0x10);
    const std::string footer_path = dir + "/footer.mp3";
    CHECK(write_file(footer_path, footer));
    CHECK(std::string_view(read_probed(footer_path, footer.size()).title.data()) == "Flagged title");

    /* A target frame carrying any flag cannot be decoded at its offset. */
    std::string flagged;
    append_raw_frame(flagged, 3, "TIT2", utf8_payload("Grouped"), 0x00, 0x20);
    const std::string flagged_bytes = wrap_tag(3, flagged);
    const std::string flagged_path = dir + "/flagged-target.mp3";
    CHECK(write_file(flagged_path, flagged_bytes));
    CHECK(preserved(read_probed(flagged_path, flagged_bytes.size())));

    /* A skipped frame with flags is dropped by size and the texts after it are
     * still decoded. */
    std::string skipped;
    append_raw_frame(skipped, 3, "APIC", artwork_payload(k_reported_artwork), 0x00, 0x20);
    append_raw_frame(skipped, 3, "TIT2", utf8_payload("After artwork"));
    const std::string skipped_bytes = wrap_tag(3, skipped);
    const std::string skipped_path = dir + "/flagged-artwork.mp3";
    CHECK(write_file(skipped_path, skipped_bytes));
    CHECK(std::string_view(read_probed(skipped_path, skipped_bytes.size()).title.data()) == "After artwork");
}

void test_read_padding_and_absent_tag(const std::string &dir) {
    std::string frames;
    append_raw_frame(frames, 3, "TIT2", utf8_payload("Padded title"));
    frames.append(4, '\0');
    const std::string padded = wrap_tag(3, frames);
    const std::string padded_path = dir + "/trailing-padding.mp3";
    CHECK(write_file(padded_path, padded));
    const metadata padded_tags = read_probed(padded_path, padded.size());
    CHECK(std::string_view(padded_tags.title.data()) == "Padded title");

    /* A non-zero remainder shorter than a frame header is a truncated frame. */
    std::string broken = padded;
    broken.replace(broken.size() - 4, 4, "xxxx");
    const std::string broken_path = dir + "/trailing-garbage.mp3";
    CHECK(write_file(broken_path, broken));
    CHECK(preserved(read_probed(broken_path, broken.size())));

    /* A zero byte only opens the padding area: every remaining byte of the
     * declared area must be zero. A valid text frame followed by one zero and
     * non-zero garbage that still fits a frame header must not publish the
     * frame (AC-MUSIC-META-27, all-or-nothing). */
    std::string leaky_frames;
    append_raw_frame(leaky_frames, 3, "TIT2", utf8_payload("Leaked title"));
    leaky_frames.append(1, '\0');
    leaky_frames.append(k_id3v2_header_bytes + 6, 'x');
    const std::string leaky = wrap_tag(3, leaky_frames);
    const std::string leaky_path = dir + "/zero-then-garbage.mp3";
    CHECK(write_file(leaky_path, leaky));
    CHECK(preserved(read_probed(leaky_path, leaky.size())));

    /* The non-zero byte may sit in a later read of the padding area, which is
     * validated in bounded chunks. */
    std::string deep_frames;
    append_raw_frame(deep_frames, 3, "TIT2", utf8_payload("Leaked title"));
    deep_frames.append(2 * k_id3_read_window_bytes + 3, '\0');
    deep_frames.back() = 'x';
    const std::string deep = wrap_tag(3, deep_frames);
    const std::string deep_path = dir + "/deep-garbage.mp3";
    CHECK(write_file(deep_path, deep));
    CHECK(preserved(read_probed(deep_path, deep.size())));

    /* Zero padding of the same size is padding: the frame is published and the
     * area is still read through bounded chunks. */
    std::string full_frames;
    append_raw_frame(full_frames, 3, "TIT2", utf8_payload("Padded title"));
    full_frames.append(2 * k_id3_read_window_bytes + 3, '\0');
    const std::string full = wrap_tag(3, full_frames);
    const std::string full_path = dir + "/multi-chunk-padding.mp3";
    CHECK(write_file(full_path, full));
    CHECK(std::string_view(read_probed(full_path, full.size()).title.data()) == "Padded title");
    CHECK(probe.largest <= k_id3_read_window_bytes);
    CHECK(probe.total <= k_id3_read_window_bytes * 16);
    /* The padding larger than one window is fully validated: the reads reach
     * the declared tag end, in more than one chunk and none above the window. */
    CHECK(probe.highest_end == full.size());

    /* No ID3v2 prologue and a file shorter than a header: nothing published. */
    const std::string plain_path = dir + "/plain.mp3";
    CHECK(write_file(plain_path, "not-an-id3-file"));
    CHECK(preserved(read_probed(plain_path, 15)));
    const std::string tiny_path = dir + "/tiny.mp3";
    CHECK(write_file(tiny_path, "ID3"));
    CHECK(preserved(read_probed(tiny_path, 3)));

    /* A declared tag larger than the verified size is rejected. */
    const std::string reported = reported_tag();
    const std::string reported_path = dir + "/size-short.mp3";
    CHECK(write_file(reported_path, reported));
    CHECK(preserved(read_probed(reported_path, reported.size() - 1)));
}
} // namespace

/* The approved ceiling and the read-window invariant are requirements, not
 * implementation details: both are asserted against expressions derived from
 * the model caps, so changing either constant breaks the test instead of
 * silently shrinking the supported envelope. */
void test_read_requirements_and_text_window(const std::string &dir) {
    CHECK(k_id3_max_tag_bytes == 1024 * 1024);
    CHECK(k_id3v2_header_bytes == 10);
    CHECK(k_id3_read_window_bytes >= 2 * cyberdeck_music::k_max_title_bytes + 3);

    /* UTF-16 title that needs the whole window: encoding byte + BOM +
     * k_max_title_bytes ASCII units = 1 + 2 + 2 * 96 = 195 bytes. The output is
     * filled exactly, so a window below 195 bytes would truncate it. */
    const std::string text(cyberdeck_music::k_max_title_bytes, 'T');
    std::string frames;
    append_raw_frame(frames, 3, "TIT2", utf16_payload(text));
    const std::string bytes = wrap_tag(3, frames);
    const std::string path = dir + "/utf16-window.mp3";
    CHECK(write_file(path, bytes));
    const metadata tags = read_probed(path, bytes.size());
    CHECK(std::strlen(tags.title.data()) == cyberdeck_music::k_max_title_bytes);
    CHECK(std::string_view(tags.title.data()) == text);
    CHECK(probe.largest <= k_id3_read_window_bytes);
}

int main() {
    const std::string dir = make_temp_dir();
    if (dir.empty()) {
        std::cerr << "FAIL: cannot create the temp directory\n";
        ++failures;
    } else {
        test_read_same_fd_keeps_position(dir);
        test_read_skips_artwork(dir);
        test_read_cap_boundaries(dir);
        test_read_short_io(dir);
        test_read_allocation_failure(dir);
        test_read_versions_and_flags(dir);
        test_read_padding_and_absent_tag(dir);
        test_read_requirements_and_text_window(dir);
        std::string cleanup = "rm -rf '" + dir + "'";
        if (std::system(cleanup.c_str()) != 0)
            std::cerr << "warning: could not remove " << dir << '\n';
    }
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
