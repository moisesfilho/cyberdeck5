/*
 * Testes host-side para a API pura de geracao de BMP 24-bit top-down.
 * Cobre components/cyberdeck/include/screenshot_bmp.h.
 *
 * Plano aprovado (recorte sob teste):
 *   - cabecalho BMP 24-bit top-down e tamanhos;
 *   - padding/stride de linhas;
 *   - conversao RGB565 para BGR888 (preto, branco, primarias);
 *   - calculo de tamanho e chunking sem gaps/sobreposicao;
 *   - validacoes de dimensoes/capacidade.
 *
 * A API e pura (sem LVGL/ESP-IDF): o teste inclui apenas
 * screenshot_bmp.h e compila contra a implementacao
 * (components/cyberdeck/src/core/screenshot_bmp.cpp) que o coder
 * deve criar conforme o contrato abaixo.
 *
 * Build: make test  (veja Makefile; sem dependencias alem de g++/make).
 */
#include "screenshot_bmp.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <climits>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected %zu, actual %zu\n", \
                        __FILE__, __LINE__, (size_t)(expected), (size_t)(actual)); \
        }                                                                              \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                                   \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected %u, actual %u\n", \
                        __FILE__, __LINE__, (unsigned)(expected), (unsigned)(actual)); \
        }                                                                              \
    } while (0)

#define CHECK_EQ_BGR(bgr, expected_b, expected_g, expected_r)                          \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((bgr)[0] != (expected_b) || (bgr)[1] != (expected_g) || (bgr)[2] != (expected_r)) { \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected BGR(%u,%u,%u) actual BGR(%u,%u,%u)\n", \
                        __FILE__, __LINE__, (unsigned)(expected_b), (unsigned)(expected_g), \
                        (unsigned)(expected_r), (unsigned)(bgr)[0], (unsigned)(bgr)[1], \
                        (unsigned)(bgr)[2]);                                           \
        }                                                                              \
    } while (0)

/* ============================================================== ouro para tamanho do BMP */
size_t oracle_bmp_size(int width, int height)
{
    size_t stride = ((size_t)width * 3 + 3) & ~(size_t)3;
    size_t pixel_bytes = stride * (size_t)height;
    return SCREENSHOT_BMP_HEADER_SIZE + pixel_bytes;
}

/* ============================================================== testes de stride */

void test_stride_basic()
{
    /* Largura que ja e multiplo de 4: sem padding adicional. */
    CHECK_EQ(screenshot_bmp_calc_stride(32), 96);
    CHECK_EQ(screenshot_bmp_calc_stride(64), 192);
    CHECK_EQ(screenshot_bmp_calc_stride(128), 384);

    /* Largura que NAO e multiplo de 4: padding ate o multiplo de 4 seguinte. */
    CHECK_EQ(screenshot_bmp_calc_stride(1), 4);
    CHECK_EQ(screenshot_bmp_calc_stride(2), 8);
    CHECK_EQ(screenshot_bmp_calc_stride(3), 12);
    CHECK_EQ(screenshot_bmp_calc_stride(5), 16);
    CHECK_EQ(screenshot_bmp_calc_stride(7), 24);
    CHECK_EQ(screenshot_bmp_calc_stride(10), 32);
    CHECK_EQ(screenshot_bmp_calc_stride(15), 48);
    CHECK_EQ(screenshot_bmp_calc_stride(17), 52);
    CHECK_EQ(screenshot_bmp_calc_stride(100), 300);
    CHECK_EQ(screenshot_bmp_calc_stride(101), 304);
    CHECK_EQ(screenshot_bmp_calc_stride(200), 600);
    CHECK_EQ(screenshot_bmp_calc_stride(201), 604);
}

void test_stride_formula()
{
    /* Verifica que stride = ((width * 3) + 3) & ~3 para uma variedade de larguras. */
    for (int w = 1; w <= 256; ++w) {
        size_t stride = screenshot_bmp_calc_stride(w);
        size_t expected = ((size_t)w * 3 + 3) & ~(size_t)3;
        CHECK_EQ(stride, expected);
        /* Stride deve ser multiplo de 4. */
        CHECK(stride % 4 == 0);
        /* Stride deve ser >= width * 3. */
        CHECK(stride >= (size_t)w * 3);
        /* Stride deve ser < width * 3 + 4. */
        CHECK(stride < (size_t)w * 3 + 4);
    }
}

/* ============================================================== testes de tamanho do BMP */

void test_bmp_size_basic()
{
    /* 1x1: stride=4, pixel_bytes=4, total=58. */
    CHECK_EQ(screenshot_bmp_calc_size(1, 1), 58);

    /* 32x32: stride=96, pixel_bytes=3072, total=3126. */
    CHECK_EQ(screenshot_bmp_calc_size(32, 32), 3126);

    /* 640x480 (VGA): stride=1920, pixel_bytes=921600, total=921654. */
    CHECK_EQ(screenshot_bmp_calc_size(640, 480), 921654);

    /* 800x600: stride=2400, pixel_bytes=1440000, total=1440054. */
    CHECK_EQ(screenshot_bmp_calc_size(800, 600), 1440054);

    /* 1920x1080: stride=5760, pixel_bytes=6220800, total=6220854. */
    CHECK_EQ(screenshot_bmp_calc_size(1920, 1080), 6220854);
}

void test_bmp_size_vs_oracle()
{
    /* Compara contra oracle independente para diversas dimensoes. */
    struct TestCase { int w, h; };
    const TestCase cases[] = {
        {1, 1}, {1, 2}, {2, 1}, {3, 3}, {4, 4},
        {5, 5}, {7, 13}, {10, 20}, {31, 31}, {32, 32},
        {33, 33}, {100, 100}, {640, 480}, {800, 600},
        {1920, 1080}, {1366, 768},
    };
    for (const auto &tc : cases) {
        CHECK_EQ(screenshot_bmp_calc_size(tc.w, tc.h), oracle_bmp_size(tc.w, tc.h));
    }
}

void test_bmp_size_monotonic()
{
    /* Aumentar largura ou altura nunca diminui o tamanho. */
    CHECK(screenshot_bmp_calc_size(10, 10) <= screenshot_bmp_calc_size(11, 10));
    CHECK(screenshot_bmp_calc_size(10, 10) <= screenshot_bmp_calc_size(10, 11));
    CHECK(screenshot_bmp_calc_size(1, 1) <= screenshot_bmp_calc_size(100, 100));
    CHECK(screenshot_bmp_calc_size(100, 100) <= screenshot_bmp_calc_size(1000, 1000));
}

/* ============================================================== testes de RGB565 -> BGR888 */

void test_rgb565_black_white()
{
    uint8_t bgr[3];

    /* Preto: RGB565 = 0x0000 -> BGR = (0, 0, 0). */
    screenshot_bmp_rgb565_to_bgr888(0x0000, bgr);
    CHECK_EQ_BGR(bgr, 0, 0, 0);

    /* Branco: RGB565 = 0xFFFF -> BGR = (255, 255, 255). */
    screenshot_bmp_rgb565_to_bgr888(0xFFFF, bgr);
    CHECK_EQ_BGR(bgr, 255, 255, 255);
}

void test_rgb565_primary_colors()
{
    uint8_t bgr[3];

    /* Vermelho puro: RGB565 = 0xF800 (1111100000000000) -> R=31, G=0, B=0
     * Em BGR888: B=0, G=0, R=255. */
    screenshot_bmp_rgb565_to_bgr888(0xF800, bgr);
    CHECK_EQ_BGR(bgr, 0, 0, 255);

    /* Verde puro: RGB565 = 0x07E0 (0000011111100000) -> R=0, G=63, B=0
     * Em BGR888: B=0, G=255, R=0. */
    screenshot_bmp_rgb565_to_bgr888(0x07E0, bgr);
    CHECK_EQ_BGR(bgr, 0, 255, 0);

    /* Azul puro: RGB565 = 0x001F (0000000000011111) -> R=0, G=0, B=31
     * Em BGR888: B=255, G=0, R=0. */
    screenshot_bmp_rgb565_to_bgr888(0x001F, bgr);
    CHECK_EQ_BGR(bgr, 255, 0, 0);
}

void test_rgb565_intermediate_colors()
{
    uint8_t bgr[3];

    /* Amarelo: RGB565 = 0xFFE0 -> R=31, G=63, B=0 -> BGR(0, 255, 255) */
    screenshot_bmp_rgb565_to_bgr888(0xFFE0, bgr);
    CHECK_EQ_BGR(bgr, 0, 255, 255);

    /* Cyan: RGB565 = 0x07FF -> R=0, G=63, B=31 -> BGR(255, 255, 0) */
    screenshot_bmp_rgb565_to_bgr888(0x07FF, bgr);
    CHECK_EQ_BGR(bgr, 255, 255, 0);

    /* Magenta: RGB565 = 0xF81F -> R=31, G=0, B=31 -> BGR(255, 0, 255) */
    screenshot_bmp_rgb565_to_bgr888(0xF81F, bgr);
    CHECK_EQ_BGR(bgr, 255, 0, 255);

/* Cinza intermediario: RGB565 = 0x8410 -> R=16, G=32, B=16
 * Conversao documentada: R8=(R<<3)|(R>>2), G8=(G<<2)|(G>>4), B8=(B<<3)|(B>>2)
 * R8=(16<<3)|(16>>2)=132, G8=(32<<2)|(32>>4)=130, B8=(16<<3)|(16>>2)=132
 * Em BGR888: B=132, G=130, R=132. */
    screenshot_bmp_rgb565_to_bgr888(0x8410, bgr);
    CHECK_EQ_BGR(bgr, 132, 130, 132);
}

void test_rgb565_bit_extraction()
{
    uint8_t bgr[3];

    /* Teste de extracao de bits: cada componente deve ser deslocado corretamente.
     * RGB565 = (R << 11) | (G << 5) | B
     * R (5 bits) -> 8 bits: (R << 3) | (R >> 2)
     * G (6 bits) -> 8 bits: (G << 2) | (G >> 4)
     * B (5 bits) -> 8 bits: (B << 3) | (B >> 2) */

    /* Apenas canal R no bit 11: 0x0800 */
    screenshot_bmp_rgb565_to_bgr888(0x0800, bgr);
    CHECK(bgr[2] > 0);  /* componente vermelho nao-zero */
    CHECK(bgr[0] == 0 && bgr[1] == 0); /* B e G devem ser zero */

    /* Apenas canal G no bit 5: 0x0020 */
    screenshot_bmp_rgb565_to_bgr888(0x0020, bgr);
    CHECK(bgr[1] > 0);  /* componente verde nao-zero */
    CHECK(bgr[0] == 0 && bgr[2] == 0); /* B e R devem ser zero */

    /* Apenas canal B no bit 0: 0x0001 */
    screenshot_bmp_rgb565_to_bgr888(0x0001, bgr);
    CHECK(bgr[0] > 0);  /* componente azul nao-zero */
    CHECK(bgr[1] == 0 && bgr[2] == 0); /* G e R devem ser zero */
}

/* ============================================================== testes de header */

void test_header_basic_fields()
{
    screenshot_bmp_header_t hdr = {};
    screenshot_bmp_fill_header(&hdr, 320, 240);

    CHECK_EQ_U32(hdr.bfType, SCREENSHOT_BMP_MAGIC);
    CHECK_EQ_U32(hdr.biSize, SCREENSHOT_BMP_INFOHEADER_SIZE);
    CHECK_EQ_U32(hdr.biWidth, 320);
    CHECK_EQ_U32(hdr.biHeight, 240);
    CHECK_EQ_U32(hdr.biPlanes, SCREENSHOT_BMP_PLANES);
    CHECK_EQ_U32(hdr.biBitCount, SCREENSHOT_BMP_BITS_PER_PIXEL);
    CHECK_EQ_U32(hdr.biCompression, SCREENSHOT_BMP_COMPRESSION);
    CHECK_EQ_U32(hdr.biClrUsed, 0);
    CHECK_EQ_U32(hdr.biClrImportant, 0);
    CHECK_EQ_U32(hdr.bfReserved1, 0);
    CHECK_EQ_U32(hdr.bfReserved2, 0);
    CHECK_EQ_U32(hdr.bfOffBits, SCREENSHOT_BMP_HEADER_SIZE);
}

void test_header_size_fields()
{
    /* bfSize deve ser igual ao calculado por screenshot_bmp_calc_size. */
    screenshot_bmp_header_t hdr = {};
    screenshot_bmp_fill_header(&hdr, 640, 480);
    CHECK_EQ_U32(hdr.bfSize, screenshot_bmp_calc_size(640, 480));

    hdr = {};
    screenshot_bmp_fill_header(&hdr, 1, 1);
    CHECK_EQ_U32(hdr.bfSize, screenshot_bmp_calc_size(1, 1));

    hdr = {};
    screenshot_bmp_fill_header(&hdr, 1920, 1080);
    CHECK_EQ_U32(hdr.bfSize, screenshot_bmp_calc_size(1920, 1080));
}

void test_header_biSizeImage()
{
    /* biSizeImage deve ser igual ao tamanho dos dados de pixel (stride * height). */
    screenshot_bmp_header_t hdr = {};
    screenshot_bmp_fill_header(&hdr, 32, 32);
    size_t expected_pixel_bytes = screenshot_bmp_calc_stride(32) * 32;
    CHECK_EQ_U32(hdr.biSizeImage, (uint32_t)expected_pixel_bytes);

    hdr = {};
    screenshot_bmp_fill_header(&hdr, 100, 50);
    expected_pixel_bytes = screenshot_bmp_calc_stride(100) * 50;
    CHECK_EQ_U32(hdr.biSizeImage, (uint32_t)expected_pixel_bytes);
}

void test_header_top_down_positive_height()
{
    /* Top-down: biHeight deve ser positivo. */
    screenshot_bmp_header_t hdr = {};
    screenshot_bmp_fill_header(&hdr, 640, 480);
    CHECK(hdr.biHeight > 0);
    CHECK(hdr.biWidth > 0);
}

void test_header_size_consistency()
{
    /* Preencher header duas vezes com mesma dimensao deve dar mesmo resultado. */
    screenshot_bmp_header_t hdr1 = {};
    screenshot_bmp_header_t hdr2 = {};
    screenshot_bmp_fill_header(&hdr1, 800, 600);
    screenshot_bmp_fill_header(&hdr2, 800, 600);
    CHECK(std::memcmp(&hdr1, &hdr2, sizeof(hdr1)) == 0);
}

void test_header_null_pointer()
{
    /* Se hdr == NULL, a funcao nao deve crashar nem modificar memoria.
     * Comportamento documentado: operacao segura sem efeito. */
    screenshot_bmp_fill_header(nullptr, 100, 100);
    /* Nenhum crash = comportamento correto. */
    CHECK(true);
}

/* ============================================================== testes de chunking */

void test_chunk_bounds_basic()
{
    int start, end;

    /* Imagem de 100 linhas, chunk_size=25: 4 chunks exatos. */
    CHECK(screenshot_bmp_chunk_bounds(100, 0, 25, &start, &end));
    CHECK(start == 0 && end == 25);

    CHECK(screenshot_bmp_chunk_bounds(100, 1, 25, &start, &end));
    CHECK(start == 25 && end == 50);

    CHECK(screenshot_bmp_chunk_bounds(100, 2, 25, &start, &end));
    CHECK(start == 50 && end == 75);

    CHECK(screenshot_bmp_chunk_bounds(100, 3, 25, &start, &end));
    CHECK(start == 75 && end == 100);
}

void test_chunk_bounds_last_chunk_smaller()
{
    /* Chunk final menor que chunk_size. */
    int start, end;
    CHECK(screenshot_bmp_chunk_bounds(100, 3, 30, &start, &end));
    CHECK(start == 90 && end == 100);
}

void test_chunk_bounds_out_of_range()
{
    /* Chunk index fora do intervalo retorna false. */
    int start = -1, end = -1;
    CHECK(!screenshot_bmp_chunk_bounds(100, 4, 25, &start, &end));
    CHECK(start == -1 && end == -1);

    CHECK(!screenshot_bmp_chunk_bounds(100, 5, 25, &start, &end));
    CHECK(start == -1 && end == -1);

    /* chunk_size > height: existe apenas 1 chunk (index 0). */
    CHECK(screenshot_bmp_chunk_bounds(100, 0, 200, &start, &end));
    CHECK(start == 0 && end == 100);
}

void test_chunk_no_gaps_or_overlaps()
{
    /* Verifica que chunks consecutivos nao possuem gaps nem sobreposicoes. */
    const int height = 1000;
    const int chunk_size = 137; /* nao divide height exatamente */
    int prev_end = 0;
    int num_chunks = screenshot_bmp_calc_num_chunks(height, chunk_size);

    for (int i = 0; i < num_chunks; ++i) {
        int start, end;
        CHECK(screenshot_bmp_chunk_bounds(height, i, chunk_size, &start, &end));
        CHECK(start == prev_end); /* sem gaps */
        if (i > 0) {
            CHECK(start < end); /* chunk nao vazio */
        }
        prev_end = end;
    }
    CHECK(prev_end == height); /* ultimo chunk cobre ate o final */
}

void test_chunk_covers_all_rows()
{
    /* Para diversas combinacoes, o chunking cobre todas as linhas. */
    struct TestCase { int height, chunk_size; };
    const TestCase cases[] = {
        {100, 10}, {100, 30}, {100, 33}, {100, 100}, {100, 101},
        {1, 1}, {1, 2}, {50, 7}, {50, 8}, {50, 16}, {50, 17},
        {1024, 256}, {1024, 100}, {1024, 1023}, {1024, 1025},
        {1366, 768}, {1366, 500},
    };
    for (const auto &tc : cases) {
        int num_chunks = screenshot_bmp_calc_num_chunks(tc.height, tc.chunk_size);
        int prev_end = 0;
        for (int i = 0; i < num_chunks; ++i) {
            int start, end;
            CHECK(screenshot_bmp_chunk_bounds(tc.height, i, tc.chunk_size, &start, &end));
            CHECK(start == prev_end);
            prev_end = end;
        }
        CHECK(prev_end == tc.height);
    }
}

void test_num_chunks()
{
    /* Divisao exata. */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 25), 4);
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 50), 2);
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 100), 1);

    /* Divisao com resto: arredonda para cima. */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 30), 4);  /* 30*3=90, resto 10 */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 33), 4);  /* 33*3=99, resto 1 */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(100, 99), 2);  /* 99*1=99, resto 1 */

    /* Chunk_size > height: um unico chunk. */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(50, 100), 1);
    CHECK_EQ(screenshot_bmp_calc_num_chunks(1, 100), 1);

    /* Height 1. */
    CHECK_EQ(screenshot_bmp_calc_num_chunks(1, 1), 1);
}

/* ============================================================== testes de validacao */

void test_validate_dimensions_positive()
{
    CHECK(screenshot_bmp_validate_dimensions(1, 1));
    CHECK(screenshot_bmp_validate_dimensions(640, 480));
    CHECK(screenshot_bmp_validate_dimensions(1920, 1080));
    CHECK(screenshot_bmp_validate_dimensions(1, 10000));
    CHECK(screenshot_bmp_validate_dimensions(10000, 1));
}

void test_validate_dimensions_zero_and_negative()
{
    CHECK(!screenshot_bmp_validate_dimensions(0, 1));
    CHECK(!screenshot_bmp_validate_dimensions(1, 0));
    CHECK(!screenshot_bmp_validate_dimensions(0, 0));
    CHECK(!screenshot_bmp_validate_dimensions(-1, 1));
    CHECK(!screenshot_bmp_validate_dimensions(1, -1));
    CHECK(!screenshot_bmp_validate_dimensions(-100, -100));
}

void test_validate_overflow_safety()
{
    /* Dimensoes que poderiam causar overflow na plataforma */
    CHECK(!screenshot_bmp_validate_dimensions(1431655765, 1));
    CHECK(!screenshot_bmp_validate_dimensions(1000000, 1000000));
    /* Overflow de stride deve ser independente da largura de size_t:
     * usar o limite derivado de SIZE_MAX em vez de assumir 32 bits. */
    const size_t max_safe_width = (SIZE_MAX - 3u) / 3u;
    if (max_safe_width <= (size_t)INT_MAX) {
        CHECK_EQ(screenshot_bmp_calc_stride((int)max_safe_width + 1), 0);
    } else {
        /* Em plataformas com size_t de 64 bits, nenhum int estoura o stride. */
        CHECK(screenshot_bmp_calc_stride(INT_MAX) != 0);
    }
    CHECK_EQ(screenshot_bmp_calc_size(1000000, 1000000), 0);
}

void test_validate_capacity()
{
    /* Capacidade exata deve ser valida. */
    CHECK(screenshot_bmp_validate_capacity(640, 480, screenshot_bmp_calc_size(640, 480)));
    CHECK(screenshot_bmp_validate_capacity(1, 1, screenshot_bmp_calc_size(1, 1)));

    /* Capacidade maior deve ser valida. */
    CHECK(screenshot_bmp_validate_capacity(640, 480, screenshot_bmp_calc_size(640, 480) + 1));
    CHECK(screenshot_bmp_validate_capacity(320, 240, 1000000));

    /* Capacidade menor deve ser invalida. */
    CHECK(!screenshot_bmp_validate_capacity(640, 480, screenshot_bmp_calc_size(640, 480) - 1));
    CHECK(!screenshot_bmp_validate_capacity(1, 1, screenshot_bmp_calc_size(1, 1) - 1));
    CHECK(!screenshot_bmp_validate_capacity(320, 240, 0));

    /* Dimensoes invalidas sempre invalidas. */
    CHECK(!screenshot_bmp_validate_capacity(0, 1, 1000));
    CHECK(!screenshot_bmp_validate_capacity(1, 0, 1000));
    CHECK(!screenshot_bmp_validate_capacity(-1, 1, 1000));
}

void test_validate_capacity_vs_oracle()
{
    /* Verifica que validate_capacity e consistente com o calculo de tamanho. */
    for (int w = 1; w <= 200; w += 7) {
        for (int h = 1; h <= 200; h += 13) {
            size_t sz = screenshot_bmp_calc_size(w, h);
            CHECK(screenshot_bmp_validate_capacity(w, h, sz));
            if (sz > 0) {
                CHECK(!screenshot_bmp_validate_capacity(w, h, sz - 1));
            }
        }
    }
}

/* ============================================================== testes de integracao */

void test_full_pipeline()
{
    /* Pipeline completo: header -> stride -> tamanho -> validacao. */
    const int width = 640;
    const int height = 480;

    screenshot_bmp_header_t hdr = {};
    screenshot_bmp_fill_header(&hdr, width, height);

    CHECK_EQ(screenshot_bmp_calc_stride(width), 1920);
    CHECK_EQ(screenshot_bmp_calc_size(width, height), hdr.bfSize);
    CHECK(screenshot_bmp_validate_dimensions(width, height));
    CHECK(screenshot_bmp_validate_capacity(width, height, hdr.bfSize));

    /* Chunking cobre todas as linhas sem gaps. */
    int num_chunks = screenshot_bmp_calc_num_chunks(height, 64);
    int prev_end = 0;
    for (int i = 0; i < num_chunks; ++i) {
        int start, end;
        CHECK(screenshot_bmp_chunk_bounds(height, i, 64, &start, &end));
        CHECK(start == prev_end);
        prev_end = end;
    }
    CHECK(prev_end == height);
}

void test_rgb565_to_bgr888_deterministic()
{
    /* Mesma entrada produz mesma saida em chamadas repetidas. */
    uint8_t bgr1[3], bgr2[3];
    screenshot_bmp_rgb565_to_bgr888(0xDEAD, bgr1);
    screenshot_bmp_rgb565_to_bgr888(0xDEAD, bgr2);
    CHECK(std::memcmp(bgr1, bgr2, 3) == 0);
}

} // namespace

int main()
{
    /* Stride */
    test_stride_basic();
    test_stride_formula();

    /* Tamanho do BMP */
    test_bmp_size_basic();
    test_bmp_size_vs_oracle();
    test_bmp_size_monotonic();

    /* RGB565 -> BGR888 */
    test_rgb565_black_white();
    test_rgb565_primary_colors();
    test_rgb565_intermediate_colors();
    test_rgb565_bit_extraction();
    test_rgb565_to_bgr888_deterministic();

    /* Header */
    test_header_basic_fields();
    test_header_size_fields();
    test_header_biSizeImage();
    test_header_top_down_positive_height();
    test_header_size_consistency();
    test_header_null_pointer();

    /* Chunking */
    test_chunk_bounds_basic();
    test_chunk_bounds_last_chunk_smaller();
    test_chunk_bounds_out_of_range();
    test_chunk_no_gaps_or_overlaps();
    test_chunk_covers_all_rows();
    test_num_chunks();

    /* Validacoes */
    test_validate_dimensions_positive();
    test_validate_dimensions_zero_and_negative();
    test_validate_overflow_safety();
    test_validate_capacity();
    test_validate_capacity_vs_oracle();

    /* Integracao */
    test_full_pipeline();

    if (s_failures == 0) {
        std::printf("PASS: screenshot_bmp (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
