#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

int main() {
    constexpr std::size_t frame_size = 417;
    std::array<std::uint8_t, frame_size * 3> frames{};
    for (std::size_t frame = 0; frame < 3; ++frame) {
        auto *header = frames.data() + frame * frame_size;
        header[0] = 0xff;
        header[1] = 0xfb;
        header[2] = 0x90;
        header[3] = 0x64;
    }
    auto original = std::make_unique<mp3dec_t>();
    auto external = std::make_unique<mp3dec_t>();
    auto scratch = std::make_unique<mp3dec_scratch_t>();
    auto pcm_original = std::make_unique<mp3d_sample_t[]>(MINIMP3_MAX_SAMPLES_PER_FRAME);
    auto pcm_external = std::make_unique<mp3d_sample_t[]>(MINIMP3_MAX_SAMPLES_PER_FRAME);
    mp3dec_init(original.get());
    mp3dec_init(external.get());

    for (std::size_t frame = 0; frame < 2; ++frame) {
        mp3dec_frame_info_t info_original{}, info_external{};
        auto *bytes = frames.data() + frame * frame_size;
        const int size = static_cast<int>(frames.size() - frame * frame_size);
        const int samples_original = mp3dec_decode_frame(original.get(), bytes, size,
                                                        pcm_original.get(), &info_original);
        const int samples_external = mp3dec_decode_frame_with_scratch(external.get(), bytes, size,
                                                                      pcm_external.get(), &info_external,
                                                                      scratch.get());
        assert(samples_original > 0);
        assert(samples_external == samples_original);
        assert(info_original.frame_bytes == info_external.frame_bytes);
        assert(info_original.channels == info_external.channels);
        assert(info_original.hz == info_external.hz);
        assert(std::memcmp(pcm_original.get(), pcm_external.get(),
                           samples_original * info_original.channels * sizeof(mp3d_sample_t)) == 0);
    }
}
