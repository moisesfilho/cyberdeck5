#include "features/screenshot/screenshot_bmp.h"

#include <limits.h>
#include <string.h>

#if defined(__cplusplus)
static_assert(sizeof(screenshot_bmp_header_t) == SCREENSHOT_BMP_HEADER_SIZE,
              "BMP header must have its on-wire size");
#endif

size_t screenshot_bmp_calc_stride(int width)
{
    if (width <= 0 || (size_t)width > (SIZE_MAX - 3u) / 3u) return 0;
    const size_t bytes = (size_t)width * 3u;
    return (bytes + 3u) & ~(size_t)3u;
}

size_t screenshot_bmp_calc_size(int width, int height)
{
    if (!screenshot_bmp_validate_dimensions(width, height)) return 0;
    const size_t stride = screenshot_bmp_calc_stride(width);
    const size_t pixels = stride * (size_t)height;
    if (pixels > SIZE_MAX - SCREENSHOT_BMP_HEADER_SIZE) return 0;
    return SCREENSHOT_BMP_HEADER_SIZE + pixels;
}

void screenshot_bmp_rgb565_to_bgr888(uint16_t rgb565, uint8_t bgr[3])
{
    if (bgr == NULL) return;
    const uint8_t r = (uint8_t)((rgb565 >> 11) & 0x1f);
    const uint8_t g = (uint8_t)((rgb565 >> 5) & 0x3f);
    const uint8_t b = (uint8_t)(rgb565 & 0x1f);
    bgr[0] = (uint8_t)((b << 3) | (b >> 2));
    bgr[1] = (uint8_t)((g << 2) | (g >> 4));
    bgr[2] = (uint8_t)((r << 3) | (r >> 2));
}

void screenshot_bmp_fill_header(screenshot_bmp_header_t *hdr, int width, int height)
{
    if (hdr == NULL) return;
    memset(hdr, 0, sizeof(*hdr));
    if (!screenshot_bmp_validate_dimensions(width, height)) return;
    hdr->bfType = SCREENSHOT_BMP_MAGIC;
    hdr->bfSize = (uint32_t)screenshot_bmp_calc_size(width, height);
    hdr->bfOffBits = SCREENSHOT_BMP_HEADER_SIZE;
    hdr->biSize = SCREENSHOT_BMP_INFOHEADER_SIZE;
    hdr->biWidth = (uint32_t)width;
    hdr->biHeight = (uint32_t)height;
    hdr->biPlanes = SCREENSHOT_BMP_PLANES;
    hdr->biBitCount = SCREENSHOT_BMP_BITS_PER_PIXEL;
    hdr->biCompression = SCREENSHOT_BMP_COMPRESSION;
    hdr->biSizeImage = (uint32_t)(screenshot_bmp_calc_stride(width) * (size_t)height);
}

int screenshot_bmp_calc_num_chunks(int height, int chunk_size)
{
    if (height <= 0 || chunk_size <= 0) return 0;
    return (int)(((size_t)height + (size_t)chunk_size - 1u) / (size_t)chunk_size);
}

bool screenshot_bmp_chunk_bounds(int height, int chunk_index, int chunk_size,
                                 int *out_start, int *out_end)
{
    if (out_start == NULL || out_end == NULL || height <= 0 || chunk_index < 0 || chunk_size <= 0) {
        return false;
    }
    const int count = screenshot_bmp_calc_num_chunks(height, chunk_size);
    if (chunk_index >= count) return false;
    const int start = chunk_index * chunk_size;
    *out_start = start;
    *out_end = start + ((start + chunk_size < height) ? chunk_size : height - start);
    return true;
}

bool screenshot_bmp_validate_dimensions(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    const size_t stride = screenshot_bmp_calc_stride(width);
    if (stride == 0 || (size_t)height > (SIZE_MAX - SCREENSHOT_BMP_HEADER_SIZE) / stride) return false;
    return SCREENSHOT_BMP_HEADER_SIZE + stride * (size_t)height <= UINT32_MAX;
}

bool screenshot_bmp_validate_capacity(int width, int height, size_t buffer_capacity)
{
    const size_t required = screenshot_bmp_calc_size(width, height);
    return required != 0 && buffer_capacity >= required;
}
