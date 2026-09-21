#ifndef SCREENSHOT_BMP_H
#define SCREENSHOT_BMP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * API pura para geracao de imagens BMP 24-bit.
 *
 * Nao depende de LVGL, ESP-IDF ou qualquer abstracao de hardware.
 * Toda a logica e computada a partir de dimensoes e pixels.
 *
 * Contrato:
 *   - BMP 24-bit, bottom-up (biHeight > 0), compressao BI_RGB (0).
 *   - Cada linha e alinhada a 4 bytes (padding incluido).
 *   - Pixel em RGB565 (5-6-5 bits) e convertido para BGR888 no BMP.
 *   - O arquivo BMP completo comeca com o header de 54 bytes
 *     (BITMAPFILEHEADER + BITMAPINFOHEADER).
 */

/* Constantes do formato BMP 24-bit. */
#define SCREENSHOT_BMP_MAGIC           0x4D42  /* "BM" em little-endian */
#define SCREENSHOT_BMP_HEADER_SIZE     54      /* sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) */
#define SCREENSHOT_BMP_INFOHEADER_SIZE 40      /* sizeof(BITMAPINFOHEADER) */
#define SCREENSHOT_BMP_BITS_PER_PIXEL  24
#define SCREENSHOT_BMP_PLANES          1
#define SCREENSHOT_BMP_COMPRESSION     0       /* BI_RGB */

/* Header BMP completo (layout em bytes no arquivo). */
#if defined(__GNUC__)
#pragma pack(push, 1)
#endif
typedef struct {
    uint16_t bfType;        /* SCREENSHOT_BMP_MAGIC = 0x4D42 ("BM") */
    uint32_t bfSize;        /* tamanho total do arquivo em bytes */
    uint16_t bfReserved1;   /* 0 */
    uint16_t bfReserved2;   /* 0 */
    uint32_t bfOffBits;     /* offset para os dados de pixel (54) */
    uint32_t biSize;        /* tamanho do info header (40) */
    uint32_t biWidth;       /* largura em pixels */
    uint32_t biHeight;      /* altura em pixels (positivo = bottom-up) */
    uint16_t biPlanes;      /* 1 */
    uint16_t biBitCount;    /* 24 */
    uint32_t biCompression; /* 0 (BI_RGB) */
    uint32_t biSizeImage;   /* tamanho da imagem em bytes (com padding) */
    uint32_t biXPelsPerMeter;
    uint32_t biYPelsPerMeter;
    uint32_t biClrUsed;     /* 0 (todas as cores) */
    uint32_t biClrImportant;/* 0 (todas importantes) */
} screenshot_bmp_header_t;
#if defined(__GNUC__)
#pragma pack(pop)
#endif

/* Calcula o stride (bytes por linha) com padding para multo de 4.
 * Formula: ((width * 3) + 3) & ~3  — cada pixel tem 3 bytes (BGR). */
size_t screenshot_bmp_calc_stride(int width);

/* Calcula o tamanho total do arquivo BMP em bytes para as dimensoes dadas.
 * Inclui o header de 54 bytes mais os dados de pixel com padding. */
size_t screenshot_bmp_calc_size(int width, int height);

/* Converte um pixel RGB565 (5-6-5 bits) para BGR888 (3 bytes).
 * rgb565: byte alto bits 15-11 = R (5 bits), bits 10-5 = G (6 bits), bits 4-0 = B (5 bits).
 * bgr[0] = azul, bgr[1] = verde, bgr[2] = vermelho (ordem BMP). */
void screenshot_bmp_rgb565_to_bgr888(uint16_t rgb565, uint8_t bgr[3]);

/* Preenche o header BMP para uma imagem 24-bit top-down.
 * Preenche todos os campos do header com base em width/height.
 * biSizeImage eh calculado automaticamente.
 * Se hdr == NULL, a funcao nao modifica memoria e nao causa crash. */
void screenshot_bmp_fill_header(screenshot_bmp_header_t *hdr, int width, int height);

/* Calcula os limites (linha inicial e final exclusivo) de um chunk.
 * height: altura total da imagem.
 * chunk_index: indice do chunk (0-based).
 * chunk_size: numero de linhas por chunk.
 * out_start: recebe a linha inicial do chunk (inclusivo).
 * out_end: recebe a linha final do chunk (exclusivo).
 * Retorna true se o chunk existe, false se chunk_index esta fora do intervalo. */
bool screenshot_bmp_chunk_bounds(int height, int chunk_index, int chunk_size,
                                  int *out_start, int *out_end);

/* Calcula o numero total de chunks necessarios para cobrir toda a imagem. */
int screenshot_bmp_calc_num_chunks(int height, int chunk_size);

/* Validacao de dimensoes: width e height devem ser estritamente positivos. */
bool screenshot_bmp_validate_dimensions(int width, int height);

/* Validacao de capacidade: verifica se buffer_capacity bytes sao suficientes
 * para armazenar o BMP completo com as dimensoes dadas. */
bool screenshot_bmp_validate_capacity(int width, int height, size_t buffer_capacity);

#ifdef __cplusplus
}
#endif

#endif /* SCREENSHOT_BMP_H */
