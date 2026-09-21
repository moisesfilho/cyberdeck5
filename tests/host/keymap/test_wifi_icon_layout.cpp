/*
 * Testes unitarios host-side para o layout geometrico do icono
 * Wi-Fi (contrato em cyberdeck_wifi_icon.h).
 *
 * Plano aprovado (recorte sob teste):
 *   - Layout geometrico para caixa variavel: tres arcos + ponto;
 *   - Raios, espessura e angulos validos;
 *   - Rejeicao de dimensoes invalidas;
 *   - Determinismo;
 *   - Estado lit separado (reutiliza cyberdeck_wifi_indicator_is_lit).
 *
 * Contrato sob teste:
 *   - cyberdeck_wifi_icon_validate(const cyberdeck_wifi_icon_layout_t *);
 *   - cyberdeck_wifi_icon_path_count(const cyberdeck_wifi_icon_layout_t *);
 *   - cyberdeck_wifi_icon_visual_arc_bottom_y(const cyberdeck_wifi_icon_layout_t *);
 *   - cyberdeck_wifi_icon_is_lit(bool, bool, bool) -> reutiliza o
 *     predicado existente;
 *   - Funcao pura e deterministica;
 *   - Centro visual vertical em ~29,5 px na caixa de 42 px, independente da
 *     geometria horizontal, do gap e do estado;
 *   - O limite inferior visual dos arcos abertos para cima e
 *     center_y - outer_radius * (1 - sin(45 graus));
 *   - Dimensoes invalidas rejeitadas.
 *
 * Build: make test_wifi_icon_layout (veja Makefile;
 * sem dependencias alem de g++).
 */

#include "cyberdeck_wifi_icon.h"
#include "cyberdeck_wifi_indicator.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

/* Contrato puro a ser fornecido pelo modulo de geometria.  A declaracao fica
 * explicita nesta suite para que o teste possa ser criado antes da
 * implementacao, sem tocar nos fontes de producao neste handoff. */
extern "C" float cyberdeck_wifi_icon_visual_arc_bottom_y(
    const cyberdeck_wifi_icon_layout_t *layout);

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++s_checks;                                                          \
        if (!(cond)) {                                                       \
            ++s_failures;                                                    \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected %zu, actual %zu\n", \
                        __FILE__, __LINE__, (size_t)(expected), (size_t)(actual)); \
        }                                                                              \
    } while (0)

#define CHECK_EQ_SIZE(actual, expected)                                                \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected %zu, actual %zu\n", \
                        __FILE__, __LINE__, (size_t)(expected), (size_t)(actual)); \
        }                                                                              \
    } while (0)

#define CHECK_EQ_BOOL(actual, expected)                                               \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK_EQ_BOOL(%s): expected %s, actual %s\n", \
                        __FILE__, __LINE__, #actual, #expected,               \
                        (actual) ? "true" : "false");                   \
        }                                                                              \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                                \
    do {                                                                               \
        ++s_checks;                                                                    \
        if ((actual) != (expected)) {                                                  \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  expected %u, actual %u\n", \
                        __FILE__, __LINE__, (unsigned)(expected), (unsigned)(actual)); \
        }                                                                              \
    } while (0)

/* Comparacao numerica com tolerancia (segura para NaN: NaN nunca esta
 * dentro do intervalo e portanto falha). */
#define CHECK_NEAR(actual, expected, tolerance)                                      \
    do {                                                                             \
        ++s_checks;                                                                  \
        const double _near_a = static_cast<double>(actual);                          \
        const double _near_e = static_cast<double>(expected);                        \
        const double _near_t = static_cast<double>(tolerance);                       \
        if (!(_near_a >= _near_e - _near_t && _near_a <= _near_e + _near_t)) {       \
            ++s_failures;                                                            \
            std::printf("FAIL %s:%d  expected %.6f +/- %.6f, actual %.6f\n",         \
                        __FILE__, __LINE__, _near_e, _near_t, _near_a);              \
        }                                                                            \
    } while (0)

/* Compara a representacao do layout, preservando igualdade para NaN. */
bool layout_bitwise_equal(const cyberdeck_wifi_icon_layout_t &a,
                          const cyberdeck_wifi_icon_layout_t &b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

/* ======================================================= construtor de layout */

cyberdeck_wifi_icon_layout_t make_valid_layout()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.center_x = 60.0f;
    layout.center_y = 21.0f;
    layout.radii[0] = 8.0f;
    layout.radii[1] = 14.0f;
    layout.radii[2] = 20.0f;
    layout.thickness = 3.0f;
    layout.start_angles[0] = 180.0f;
    layout.start_angles[1] = 180.0f;
    layout.start_angles[2] = 180.0f;
    layout.sweep_angles[0] = 90.0f;
    layout.sweep_angles[1] = 90.0f;
    layout.sweep_angles[2] = 90.0f;
    layout.point_radius = 2.0f;
    layout.box_width = 40.0f;
    layout.box_height = 24.0f;
    return layout;
}

/* ======================================================= validacao de dimensoes */

void test_valid_layout()
{
    /* Layout com tres arcos e ponto, todos os parametros dentro dos limites. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    CHECK(cyberdeck_wifi_icon_validate(&layout));
}

void test_null_layout_rejected()
{
    /* NULL deve ser rejeitado. */
    CHECK(!cyberdeck_wifi_icon_validate(nullptr));
}

void test_zero_box_dimensions_rejected()
{
    /* Caixa com dimensoes zero deve ser rejeitada. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.box_width = 40.0f;
    layout.box_height = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.box_height = 24.0f;
    layout.box_width = 0.0f;
    layout.box_height = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_negative_box_dimensions_rejected()
{
    /* Caixa com dimensoes negativas deve ser rejeitada. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = -10.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.box_width = 40.0f;
    layout.box_height = -10.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_non_finite_box_dimensions_rejected()
{
    /* NaN/+Inf/-Inf nao sao dimensoes positivas validas. */
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float invalid[] = {nan, inf, -inf};

    for (const float value : invalid) {
        cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
        layout.box_width = value;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.box_height = value;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));
    }
}

void test_radius_out_of_range_rejected()
{
    /* Raios fora dos limites validos devem ser rejeitados. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.radii[0] = 8.0f;
    layout.radii[1] = 51.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.radii[1] = 14.0f;
    layout.radii[2] = -1.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_thickness_out_of_range_rejected()
{
    /* Espessura fora dos limites deve ser rejeitada. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.thickness = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.thickness = 3.0f;
    layout.thickness = 51.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_angle_out_of_range_rejected()
{
    /* Angulos fora dos limites devem ser rejeitados. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.start_angles[0] = 361.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.start_angles[0] = 180.0f;
    layout.sweep_angles[0] = -361.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_point_radius_out_of_range_rejected()
{
    /* Ponto com raio fora dos limites deve ser rejeitado. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.point_radius = 0.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    layout.point_radius = 2.0f;
    layout.point_radius = 51.0f;
    CHECK(!cyberdeck_wifi_icon_validate(&layout));
}

void test_non_finite_geometry_rejected()
{
    /* Cada parametro geometrico deve rejeitar NaN/+Inf/-Inf. */
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float invalid[] = {nan, inf, -inf};

    for (const float value : invalid) {
        for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
            cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
            layout.radii[i] = value;
            CHECK(!cyberdeck_wifi_icon_validate(&layout));

            layout = make_valid_layout();
            layout.start_angles[i] = value;
            CHECK(!cyberdeck_wifi_icon_validate(&layout));

            layout = make_valid_layout();
            layout.sweep_angles[i] = value;
            CHECK(!cyberdeck_wifi_icon_validate(&layout));
        }

        cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
        layout.thickness = value;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.point_radius = value;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));
    }
}

void test_all_boundaries_valid()
{
    /* Valores nos limites extremos devem ser aceitos. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.center_x = 0.0f;
    layout.center_y = 0.0f;
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        layout.radii[i] = CYBERDECK_WIFI_ICON_MIN_RADIUS;
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE;
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE;
    }
    layout.thickness = CYBERDECK_WIFI_ICON_MIN_THICKNESS;
    layout.point_radius = CYBERDECK_WIFI_ICON_MIN_POINT_RADIUS;
    layout.box_width = 0.001f;
    layout.box_height = 0.001f;
    CHECK(cyberdeck_wifi_icon_validate(&layout));

    /* Limites superiores. */
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        layout.radii[i] = CYBERDECK_WIFI_ICON_MAX_RADIUS;
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE;
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE;
    }
    layout.thickness = CYBERDECK_WIFI_ICON_MAX_THICKNESS;
    layout.point_radius = CYBERDECK_WIFI_ICON_MAX_POINT_RADIUS;
    layout.box_width = 1000.0f;
    layout.box_height = 1000.0f;
    CHECK(cyberdeck_wifi_icon_validate(&layout));
}

/* ======================================================= tres arcos + ponto */

void test_three_arcs_and_point()
{
    /* O layout deve ter exatamente tres arcos e um ponto. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    CHECK_EQ(cyberdeck_wifi_icon_path_count(&layout), 4); /* 3 arcs + 1 point */
}

void test_path_count_null_rejected()
{
    /* NULL deve retornar 0. */
    CHECK_EQ(cyberdeck_wifi_icon_path_count(nullptr), 0);
}

void test_path_count_invalid_rejected()
{
    /* Layout invalido deve retornar 0. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.box_width = 0.0f;
    CHECK_EQ(cyberdeck_wifi_icon_path_count(&layout), 0);
}

void test_arc_count_constant()
{
    /* O numero de arcos e sempre 3 (definido pela constante). */
    CHECK_EQ(CYBERDECK_WIFI_ICON_ARC_COUNT, 3);
}

/* ======================================================= determinismo */

void test_deterministic_validate()
{
    /* Validacao e determinista: mesma entrada -> mesmo resultado. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    bool r1 = cyberdeck_wifi_icon_validate(&layout);
    bool r2 = cyberdeck_wifi_icon_validate(&layout);
    CHECK_EQ_BOOL(r1, r2);
    CHECK(r1 == true);
}

void test_deterministic_path_count()
{
    /* path_count e determinista. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    size_t r1 = cyberdeck_wifi_icon_path_count(&layout);
    size_t r2 = cyberdeck_wifi_icon_path_count(&layout);
    CHECK_EQ(r1, r2);
    CHECK(r1 == 4);
}

void test_deterministic_is_lit()
{
    /* cyberdeck_wifi_icon_is_lit e pura e deterministica. */
    bool r1 = cyberdeck_wifi_icon_is_lit(true, true, true);
    bool r2 = cyberdeck_wifi_icon_is_lit(true, true, true);
    CHECK_EQ_BOOL(r1, r2);
    CHECK(r1 == true);

    bool r3 = cyberdeck_wifi_icon_is_lit(false, true, true);
    bool r4 = cyberdeck_wifi_icon_is_lit(false, true, true);
    CHECK_EQ_BOOL(r3, r4);
    CHECK(r3 == false);
}

void test_deterministic_with_different_inputs()
{
    /* Entradas diferentes produzem saidas diferentes quando aplicavel. */
    bool lit_on = cyberdeck_wifi_icon_is_lit(true, true, true);
    bool lit_off = cyberdeck_wifi_icon_is_lit(false, true, true);
    CHECK(lit_on != lit_off);
}

/* ======================================================= estado lit separado */

void test_lit_delegates_to_existing_predicate()
{
    /* cyberdeck_wifi_icon_is_lit deve reutilizar
     * cyberdeck_wifi_indicator_is_lit: mesmo comportamento. */
    /* Apenas (true, true, true) produz LIT. */
    for (int e = 0; e <= 1; ++e) {
        for (int c = 0; c <= 1; ++c) {
            for (int h = 0; h <= 1; ++h) {
                bool icon_lit = cyberdeck_wifi_icon_is_lit(static_cast<bool>(e), static_cast<bool>(c), static_cast<bool>(h));
                bool indicator_lit = cyberdeck_wifi_indicator_is_lit(static_cast<bool>(e), static_cast<bool>(c), static_cast<bool>(h));
                CHECK_EQ_BOOL(icon_lit, indicator_lit);
            }
        }
    }
}

void test_lit_only_when_all_three_true()
{
    /* LIT somente quando enabled && connected && has_ip. */
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(true, true, true), true);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, true, true), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(true, false, true), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(true, true, false), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, false, false), false);
}

void test_lit_disabled_dominates()
{
    /* disabled domina qualquer combinacao de connected e has_ip. */
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, true, true), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, false, true), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, true, false), false);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, false, false), false);
}

/* ======================================================= geometria do layout */

void test_layout_fields_default_to_zero()
{
    /* Um layout inicializado com {} deve ter todos os campos em zero. */
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK_EQ(layout.center_x, 0.0f);
    CHECK_EQ(layout.center_y, 0.0f);
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        CHECK_EQ(layout.radii[i], 0.0f);
        CHECK_EQ(layout.start_angles[i], 0.0f);
        CHECK_EQ(layout.sweep_angles[i], 0.0f);
    }
    CHECK_EQ(layout.thickness, 0.0f);
    CHECK_EQ(layout.point_radius, 0.0f);
    CHECK_EQ(layout.box_width, 0.0f);
    CHECK_EQ(layout.box_height, 0.0f);
}

void test_layout_center_position()
{
    /* O centro do layout deve ser configuravel. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.center_x = 100.5f;
    layout.center_y = 200.25f;
    CHECK_EQ(layout.center_x, 100.5f);
    CHECK_EQ(layout.center_y, 200.25f);
}

void test_layout_radii_independent()
{
    /* Cada arco pode ter raio independente. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.radii[0] = 5.0f;
    layout.radii[1] = 10.0f;
    layout.radii[2] = 15.0f;
    CHECK_EQ(layout.radii[0], 5.0f);
    CHECK_EQ(layout.radii[1], 10.0f);
    CHECK_EQ(layout.radii[2], 15.0f);
}

void test_layout_box_dimensions()
{
    /* A caixa variavel deve ter dimensoes configuraveis. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = 80.0f;
    layout.box_height = 48.0f;
    CHECK_EQ(layout.box_width, 80.0f);
    CHECK_EQ(layout.box_height, 48.0f);
    /* Caixa valida. */
    CHECK(cyberdeck_wifi_icon_validate(&layout));
}

/* ======================================================= integracao */

void test_full_pipeline_valid_layout()
{
    /* Pipeline completo: criar layout -> validar -> contar caminhos -> verificar estado lit. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();

    /* Validacao. */
    CHECK(cyberdeck_wifi_icon_validate(&layout));

    /* Contagem de caminhos. */
    CHECK_EQ(cyberdeck_wifi_icon_path_count(&layout), 4);

    /* Estado lit. */
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(true, true, true), true);
    CHECK_EQ_BOOL(cyberdeck_wifi_icon_is_lit(false, true, true), false);
}

void test_full_pipeline_invalid_layout()
{
    /* Pipeline completo com layout invalido: validacao falha, path_count retorna 0. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.box_width = 0.0f;

    CHECK(!cyberdeck_wifi_icon_validate(&layout));
    CHECK_EQ(cyberdeck_wifi_icon_path_count(&layout), 0);
}

void test_layout_with_all_arc_angles()
{
    /* Todos os arcos com angulos diferentes. */
    cyberdeck_wifi_icon_layout_t layout = {};
    layout.radii[0] = 5.0f;
    layout.radii[1] = 10.0f;
    layout.radii[2] = 15.0f;
    layout.thickness = 2.0f;
    layout.start_angles[0] = 0.0f;
    layout.start_angles[1] = 90.0f;
    layout.start_angles[2] = 180.0f;
    layout.sweep_angles[0] = 90.0f;
    layout.sweep_angles[1] = 90.0f;
    layout.sweep_angles[2] = 90.0f;
    layout.point_radius = 2.0f;
    layout.box_width = 40.0f;
    layout.box_height = 24.0f;

    CHECK(cyberdeck_wifi_icon_validate(&layout));
    CHECK_EQ(cyberdeck_wifi_icon_path_count(&layout), 4);
}

/* ======================================================= calculo de layout e margem ~2px */

void test_determinant_outer_radius()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK_EQ(cyberdeck_wifi_icon_determinant_outer_radius(nullptr), 0.0f);
    CHECK_EQ(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 0.0f);

    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT, CYBERDECK_WIFI_ICON_MARGIN_RIGHT, &layout));
    /* radius 2 is 14.0f, thickness is 3.0f => determinant outer radius is 17.0f */
    const float det_r = cyberdeck_wifi_icon_determinant_outer_radius(&layout);
    CHECK(det_r >= 16.99f && det_r <= 17.01f);
}

void test_visible_half_width()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK_EQ(cyberdeck_wifi_icon_visible_half_width(nullptr), 0.0f);

    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT, CYBERDECK_WIFI_ICON_MARGIN_RIGHT, &layout));
    /* 17.0f * sin(45 deg) = 17.0f * 0.70710678f = ~12.0208f */
    const float vis_half = cyberdeck_wifi_icon_visible_half_width(&layout);
    CHECK(vis_half >= 12.01f && vis_half <= 12.03f);
}

void test_margin_2px_positioning()
{
    cyberdeck_wifi_icon_layout_t layout = {};

    /* Container 216 px com margem 2 px: center_x = 216 - 2 - 12.0208 = ~201.979f */
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));
    CHECK(layout.center_x >= 201.9f && layout.center_x <= 202.1f);
    CHECK_NEAR(layout.center_y, layout.box_height - 12.5f, 0.0f);

    /* Container 100 px com margem 2 px: center_x = 100 - 2 - 12.0208 = ~85.979f */
    CHECK(cyberdeck_wifi_icon_calculate_layout(100.0f, 42.0f, 2.0f, &layout));
    CHECK(layout.center_x >= 85.9f && layout.center_x <= 86.1f);

    /* Container 720 px com margem 2 px: center_x = 720 - 2 - 12.0208 = ~705.979f */
    CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 42.0f, 2.0f, &layout));
    CHECK(layout.center_x >= 705.9f && layout.center_x <= 706.1f);
}

void test_calculate_layout_invalid_args()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(!cyberdeck_wifi_icon_calculate_layout(0.0f, 42.0f, 2.0f, &layout));
    CHECK(!cyberdeck_wifi_icon_calculate_layout(-10.0f, 42.0f, 2.0f, &layout));
    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 0.0f, 2.0f, &layout));
    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, -42.0f, 2.0f, &layout));
    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, -1.0f, &layout));
    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, nullptr));
}

void test_header_42px_invariants()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));

    /* Top apex of outer arc: center_y - (r_outer + thickness) = 18.0 - 17.0 = 1.0f */
    const float det_r = cyberdeck_wifi_icon_determinant_outer_radius(&layout);
    const float y_top = layout.center_y - det_r;
    CHECK(y_top >= 0.0f);
    CHECK(y_top <= 42.0f);

    /* Bottom of point dot: y = 34.0f + point_radius = 34.0 + 2.0 = 36.0f <= 42.0f */
    const float y_point_bottom = 34.0f + layout.point_radius;
    CHECK(y_point_bottom <= 42.0f);
}

/* ======================================================= selecao do arco determinante */

void test_determinant_selects_dominant_arc()
{
    /* make_valid_layout usa radii 8/14/20 e thickness 3: o arco 2 (20)
     * domina e define o raio externo 23. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 23.0f, 0.0005f);

    /* Arco 0 passa a dominar (30) => 33, independentemente da ordem. */
    layout.radii[0] = 30.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 33.0f, 0.0005f);

    /* Arco 1 passa a dominar (25) => 28. */
    layout.radii[0] = 8.0f;
    layout.radii[1] = 25.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 28.0f, 0.0005f);
}

void test_determinant_handles_tie()
{
    /* Empate entre os arcos 0 e 2: o raio externo e unico (18 + 3 = 21). */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 18.0f;
    layout.radii[1] = 12.0f;
    layout.radii[2] = 18.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 21.0f, 0.0005f);

    /* Empate nos tres arcos. */
    layout.radii[0] = 9.0f;
    layout.radii[1] = 9.0f;
    layout.radii[2] = 9.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 12.0f, 0.0005f);
}

void test_determinant_dominates_every_arc()
{
    /* O raio determinante nunca e menor que o raio externo de qualquer arco. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 6.0f;
    layout.radii[1] = 30.0f;
    layout.radii[2] = 14.0f;
    CHECK(cyberdeck_wifi_icon_validate(&layout));

    const float det = cyberdeck_wifi_icon_determinant_outer_radius(&layout);
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        CHECK(det >= layout.radii[i] + layout.thickness);
    }
    /* Igualdade exata para o arco de maior raio externo (arco 1). */
    CHECK_NEAR(det, layout.radii[1] + layout.thickness, 0.0005f);
}

void test_determinant_invalid_layout_zero()
{
    /* Layout invalido por dimensao de caixa. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = 0.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 0.0f, 0.0f);

    /* Layout invalido por raio fora do limite. */
    layout = make_valid_layout();
    layout.radii[2] = 60.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 0.0f, 0.0f);

    /* NULL ja coberto em test_determinant_outer_radius, reforcado aqui. */
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(nullptr), 0.0f, 0.0f);
}

/* ======================================================= extensao visivel */

void test_visible_extent_default_layout()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT,
                                               CYBERDECK_WIFI_ICON_MARGIN_RIGHT, &layout));

    const float det = cyberdeck_wifi_icon_determinant_outer_radius(&layout); /* 17 */
    const float half = cyberdeck_wifi_icon_visible_half_width(&layout);
    CHECK_NEAR(det, 17.0f, 0.0005f);
    CHECK_NEAR(half, 12.0208153f, 0.0005f);
    /* Extensao visivel total = 2 * meia-largura. */
    CHECK_NEAR(2.0f * half, 24.0416306f, 0.0005f);
    /* Relacao direta com o arco determinante. */
    CHECK_NEAR(half, det * 0.7071067811865475f, 0.0005f);
}

void test_visible_extent_from_dominant_arc()
{
    /* Radii 10/20/30 com thickness 5: arco 2 domina => raio externo 35. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 10.0f;
    layout.radii[1] = 20.0f;
    layout.radii[2] = 30.0f;
    layout.thickness = 5.0f;
    CHECK(cyberdeck_wifi_icon_validate(&layout));

    /* 35 * sin(45) = 24.74874; extensao total = 49.49747. */
    CHECK_NEAR(cyberdeck_wifi_icon_visible_half_width(&layout), 24.7487373f, 0.001f);
    CHECK_NEAR(2.0f * cyberdeck_wifi_icon_visible_half_width(&layout), 49.4974747f, 0.002f);
}

void test_visible_half_width_invalid_layout_zero()
{
    CHECK_NEAR(cyberdeck_wifi_icon_visible_half_width(nullptr), 0.0f, 0.0f);

    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = -1.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_visible_half_width(&layout), 0.0f, 0.0f);
}

void test_visible_half_width_invariant_to_container_width()
{
    /* Os raios sao fixos no calculate_layout: a extensao visivel nao depende
     * da largura real do container; ela apenas desloca center_x. */
    cyberdeck_wifi_icon_layout_t a = {};
    cyberdeck_wifi_icon_layout_t b = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(100.0f, 42.0f, 2.0f, &a));
    CHECK(cyberdeck_wifi_icon_calculate_layout(1280.0f, 42.0f, 2.0f, &b));

    CHECK_NEAR(cyberdeck_wifi_icon_visible_half_width(&a),
               cyberdeck_wifi_icon_visible_half_width(&b), 0.0f);
    CHECK_NEAR(a.center_x + cyberdeck_wifi_icon_visible_half_width(&a), 98.0f, 0.01f);
    CHECK_NEAR(b.center_x + cyberdeck_wifi_icon_visible_half_width(&b), 1278.0f, 0.01f);
}

/* ======================================================= larguras validas/invalidas */

void test_calculate_layout_valid_widths()
{
    const float widths[] = {0.5f, 1.0f, 12.0f, 40.0f, 100.0f, 216.0f, 320.0f, 720.0f, 1280.0f};
    const size_t count = sizeof(widths) / sizeof(widths[0]);

    for (size_t i = 0; i < count; ++i) {
        const float w = widths[i];
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(w, 42.0f, 2.0f, &layout));
        CHECK(cyberdeck_wifi_icon_validate(&layout));
        CHECK_NEAR(layout.box_width, w, 0.0f);
        CHECK_NEAR(layout.box_height, 42.0f, 0.0f);
        CHECK_NEAR(layout.center_y, layout.box_height - 12.5f, 0.0f);
        CHECK_NEAR(layout.center_x, w - 2.0f - 12.0208153f, 0.01f);
        CHECK_NEAR(layout.center_x + cyberdeck_wifi_icon_visible_half_width(&layout),
                   w - 2.0f, 0.01f);
    }
}

void test_calculate_layout_invalid_widths()
{
    const float bad_widths[] = {0.0f, -0.001f, -1.0f, -100.0f, -1280.0f};
    const size_t width_count = sizeof(bad_widths) / sizeof(bad_widths[0]);
    for (size_t i = 0; i < width_count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(!cyberdeck_wifi_icon_calculate_layout(bad_widths[i], 42.0f, 2.0f, &layout));
    }

    const float bad_heights[] = {0.0f, -0.001f, -42.0f, -1000.0f};
    const size_t height_count = sizeof(bad_heights) / sizeof(bad_heights[0]);
    for (size_t i = 0; i < height_count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, bad_heights[i], 2.0f, &layout));
    }

    const float bad_margins[] = {-0.001f, -1.0f, -2.0f, -100.0f};
    const size_t margin_count = sizeof(bad_margins) / sizeof(bad_margins[0]);
    for (size_t i = 0; i < margin_count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, bad_margins[i], &layout));
    }

    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, nullptr));
}

void test_calculate_layout_rejects_non_finite_args()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float invalid[] = {nan, inf, -inf};

    for (const float value : invalid) {
        cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
        const cyberdeck_wifi_icon_layout_t before = layout;

        CHECK(!cyberdeck_wifi_icon_calculate_layout(value, 42.0f, 2.0f, &layout));
        CHECK(layout_bitwise_equal(layout, before));

        CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, value, 2.0f, &layout));
        CHECK(layout_bitwise_equal(layout, before));

        CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, value, &layout));
        CHECK(layout_bitwise_equal(layout, before));
    }
}

void test_calculate_layout_preserves_out_on_failure()
{
    /* Falha nao deve sobrescrever o layout de saida com valores parciais. */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    const cyberdeck_wifi_icon_layout_t before = layout;

    CHECK(!cyberdeck_wifi_icon_calculate_layout(0.0f, 42.0f, 2.0f, &layout));
    CHECK(layout_bitwise_equal(layout, before));

    CHECK(!cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, -1.0f, &layout));
    CHECK(layout_bitwise_equal(layout, before));
}

/* ======================================================= determinismo do calculo */

void test_calculate_layout_deterministic()
{
    cyberdeck_wifi_icon_layout_t a = {};
    cyberdeck_wifi_icon_layout_t b = {};

    for (int rep = 0; rep < 4; ++rep) {
        CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &a));
        CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &b));
        CHECK(layout_bitwise_equal(a, b));
    }

    /* Entradas diferentes produzem layouts diferentes. */
    cyberdeck_wifi_icon_layout_t c = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(320.0f, 42.0f, 2.0f, &c));
    CHECK(!layout_bitwise_equal(a, c));
}

void test_calculate_layout_deterministic_non_default_margin()
{
    cyberdeck_wifi_icon_layout_t a = {};
    cyberdeck_wifi_icon_layout_t b = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 21.5f, 0.0f, &a));
    CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 21.5f, 0.0f, &b));
    CHECK(layout_bitwise_equal(a, b));
}

/* ======================================================= monotonicidade */

void test_calculate_layout_monotonic_in_width()
{
    /* center_x cresce estritamente com a largura do container. */
    float prev_center = -1.0e9f;
    for (float w = 20.0f; w <= 1280.0f; w += 20.0f) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(w, 42.0f, 2.0f, &layout));
        CHECK(layout.center_x > prev_center);
        prev_center = layout.center_x;
    }
}

void test_calculate_layout_monotonic_in_margin()
{
    /* center_x decresce estritamente com a margem solicitada. */
    const float margins[] = {0.0f, 1.0f, 2.0f, 5.0f, 20.0f};
    const size_t count = sizeof(margins) / sizeof(margins[0]);
    float prev_center = 1.0e9f;
    for (size_t i = 0; i < count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 42.0f, margins[i], &layout));
        CHECK(layout.center_x < prev_center);
        prev_center = layout.center_x;
    }
}

/* ======================================================= margem final ~2px */

void test_final_margin_tracks_container_width()
{
    const float widths[] = {20.0f, 100.0f, 216.0f, 480.0f, 720.0f, 1280.0f};
    const size_t count = sizeof(widths) / sizeof(widths[0]);
    const float margin = CYBERDECK_WIFI_ICON_MARGIN_RIGHT; /* 2.0f */

    for (size_t i = 0; i < count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(widths[i], 42.0f, margin, &layout));
        const float final_margin =
            layout.box_width - (layout.center_x + cyberdeck_wifi_icon_visible_half_width(&layout));
        CHECK_NEAR(final_margin, margin, 0.01f); /* ~2px */
    }
}

void test_final_margin_zero_requested()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 0.0f, &layout));
    const float final_margin =
        216.0f - (layout.center_x + cyberdeck_wifi_icon_visible_half_width(&layout));
    CHECK_NEAR(final_margin, 0.0f, 0.01f);
}

void test_final_margin_custom_requested()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 42.0f, 7.5f, &layout));
    const float final_margin =
        720.0f - (layout.center_x + cyberdeck_wifi_icon_visible_half_width(&layout));
    CHECK_NEAR(final_margin, 7.5f, 0.01f);
}

/* ======================================================= casos angulares */

void test_calculate_layout_sets_default_angles()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));

    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        CHECK_NEAR(layout.start_angles[i], CYBERDECK_WIFI_ICON_START_ANGLE, 0.0f);
        CHECK_NEAR(layout.sweep_angles[i], CYBERDECK_WIFI_ICON_SWEEP_ANGLE, 0.0f);
        const float expected_radius = (i == 0) ? CYBERDECK_WIFI_ICON_RADIUS_0
                                     : (i == 1) ? CYBERDECK_WIFI_ICON_RADIUS_1
                                                : CYBERDECK_WIFI_ICON_RADIUS_2;
        CHECK_NEAR(layout.radii[i], expected_radius, 0.0f);
    }
    CHECK_NEAR(layout.thickness, CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS, 0.0f);
    CHECK_NEAR(layout.point_radius, CYBERDECK_WIFI_ICON_POINT_RADIUS, 0.0f);
}

void test_validate_angular_boundaries_per_arc()
{
    /* Os limites extremos +/-360 devem ser aceitos em qualquer arco. */
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE;
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE;
        CHECK(cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE;
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE;
        CHECK(cyberdeck_wifi_icon_validate(&layout));
    }
}

void test_validate_rejects_angle_out_of_range_per_arc()
{
    /* Qualquer arco com angulo fora do intervalo invalida o layout inteiro. */
    for (int i = 0; i < CYBERDECK_WIFI_ICON_ARC_COUNT; ++i) {
        cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE + 1.0f;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.start_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE - 1.0f;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MAX_ANGLE + 1.0f;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));

        layout = make_valid_layout();
        layout.sweep_angles[i] = CYBERDECK_WIFI_ICON_MIN_ANGLE - 1.0f;
        CHECK(!cyberdeck_wifi_icon_validate(&layout));
    }
}

void test_visible_half_width_uses_45_degree_projection()
{
    /* O vao 225..315 graus projeta os extremos horizontais a 45 graus da
     * vertical; logo a meia-largura visivel e outer * sin(45). */
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 6.0f;
    layout.radii[1] = 10.0f;
    layout.radii[2] = 14.0f;
    layout.thickness = 3.0f;
    const float outer = 17.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), outer, 0.0005f);
    CHECK_NEAR(cyberdeck_wifi_icon_visible_half_width(&layout),
               outer * 0.7071067811865475f, 0.0005f);
}

void test_visual_arc_bottom_uses_open_up_geometry()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    const float outer = 23.0f; /* max radius 20 + thickness 3 */
    const float sin_45 = 0.7071067811865475f;
    const float expected = layout.center_y - outer * (1.0f - sin_45);

    CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(&layout), expected,
               0.0005f);
}

void test_visual_arc_bottom_selects_dominant_outer_radius()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.radii[0] = 30.0f;
    layout.radii[1] = 10.0f;
    layout.radii[2] = 14.0f;

    const float expected = layout.center_y - 33.0f *
                           (1.0f - 0.7071067811865475f);
    CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(&layout), expected,
               0.0005f);
}

void test_visual_arc_bottom_invalid_layout_returns_zero()
{
    CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(nullptr), 0.0f, 0.0f);

    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    layout.box_width = 0.0f;
    CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(&layout), 0.0f, 0.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float value : {nan, inf, -inf}) {
        layout = make_valid_layout();
        layout.center_y = value;
        CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(&layout), 0.0f, 0.0f);

        layout = make_valid_layout();
        layout.radii[2] = value;
        CHECK_NEAR(cyberdeck_wifi_icon_visual_arc_bottom_y(&layout), 0.0f, 0.0f);
    }
}

void test_visual_arc_bottom_is_deterministic_and_finite()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    const float first = cyberdeck_wifi_icon_visual_arc_bottom_y(&layout);
    const float second = cyberdeck_wifi_icon_visual_arc_bottom_y(&layout);

    CHECK_NEAR(first, second, 0.0f);
    CHECK(first == first);
    CHECK(first != std::numeric_limits<float>::infinity());
    CHECK(first != -std::numeric_limits<float>::infinity());
}

/* ======================================================= integracao de geometria */

void test_full_geometry_pipeline_various_widths()
{
    const float widths[] = {20.0f, 100.0f, 216.0f, 720.0f, 1280.0f};
    const size_t count = sizeof(widths) / sizeof(widths[0]);

    for (size_t i = 0; i < count; ++i) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(widths[i], CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT,
                                                   CYBERDECK_WIFI_ICON_MARGIN_RIGHT, &layout));
        CHECK(cyberdeck_wifi_icon_validate(&layout));
        CHECK_EQ_SIZE(cyberdeck_wifi_icon_path_count(&layout), 4);
        CHECK_NEAR(cyberdeck_wifi_icon_determinant_outer_radius(&layout), 17.0f, 0.0005f);
        CHECK_NEAR(layout.center_x + cyberdeck_wifi_icon_visible_half_width(&layout),
                   widths[i] - CYBERDECK_WIFI_ICON_MARGIN_RIGHT, 0.01f);
    }
}

/* ======================================================= centro Y do ponto */

void test_point_center_y_preserves_requested_visual_gap()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));

    const float arc_bottom = cyberdeck_wifi_icon_visual_arc_bottom_y(&layout);
    for (const float gap : {CYBERDECK_WIFI_ICON_POINT_GAP_MIN,
                            CYBERDECK_WIFI_ICON_POINT_GAP_MAX}) {
        const float point_center =
            cyberdeck_wifi_icon_calculate_point_center_y(&layout, gap);
        const float point_top = point_center - layout.point_radius;
        CHECK_NEAR(point_top - arc_bottom, gap, 0.0005f);
    }
}

void test_point_center_y_keeps_point_radius_unchanged()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));
    const float original_radius = layout.point_radius;
    const float point_center =
        cyberdeck_wifi_icon_calculate_point_center_y(&layout, 1.5f);

    CHECK_NEAR(layout.point_radius, original_radius, 0.0f);
    CHECK_NEAR(point_center -
                   (cyberdeck_wifi_icon_visual_arc_bottom_y(&layout) + 1.5f),
               original_radius, 0.0005f);
}

void test_point_center_y_is_below_arcs_and_inside_header()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(
        216.0f, CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT,
        CYBERDECK_WIFI_ICON_MARGIN_RIGHT, &layout));

    const float point_center =
        cyberdeck_wifi_icon_calculate_point_center_y(&layout, 1.5f);
    const float arc_bottom = cyberdeck_wifi_icon_visual_arc_bottom_y(&layout);
    const float point_bottom = point_center + layout.point_radius;

    CHECK(point_center - layout.point_radius > arc_bottom);
    CHECK(point_center - layout.point_radius >= 0.0f);
    CHECK(point_bottom <= CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT);
}

void test_point_center_y_rejects_invalid_gaps_and_layouts()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));
    CHECK_NEAR(cyberdeck_wifi_icon_calculate_point_center_y(
                   &layout, CYBERDECK_WIFI_ICON_POINT_GAP_MIN - 0.001f),
               0.0f, 0.0f);
    CHECK_NEAR(cyberdeck_wifi_icon_calculate_point_center_y(
                   &layout, CYBERDECK_WIFI_ICON_POINT_GAP_MAX + 0.001f),
               0.0f, 0.0f);
    CHECK_NEAR(cyberdeck_wifi_icon_calculate_point_center_y(nullptr, 1.5f),
               0.0f, 0.0f);
}

void test_point_center_y_rejects_non_finite_gaps()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float invalid[] = {nan, inf, -inf};
    for (const float gap : invalid) {
        CHECK_NEAR(cyberdeck_wifi_icon_calculate_point_center_y(&layout, gap),
                   0.0f, 0.0f);
    }
}

void test_point_center_y_is_deterministic()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(720.0f, 42.0f, 2.0f, &layout));
    const float first =
        cyberdeck_wifi_icon_calculate_point_center_y(&layout, 1.5f);
    const float second =
        cyberdeck_wifi_icon_calculate_point_center_y(&layout, 1.5f);
    CHECK_NEAR(first, second, 0.0f);
}

/* ======================================================= centro visual vertical */

void test_visual_center_y_is_near_textual_center_for_42px_box()
{
    cyberdeck_wifi_icon_layout_t layout = {};
    CHECK(cyberdeck_wifi_icon_calculate_layout(216.0f, 42.0f, 2.0f, &layout));

    float visual_center_y = -1.0f;
    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &visual_center_y));
    CHECK_NEAR(visual_center_y, CYBERDECK_WIFI_ICON_VISUAL_CENTER_Y_42PX, 1.5f);
    CHECK(visual_center_y >= 28.5f && visual_center_y <= 30.5f);
}

void test_visual_center_y_is_deterministic()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    float first = 0.0f;
    float second = 0.0f;

    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &first));
    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &second));
    CHECK_NEAR(first, second, 0.0f);
}

void test_visual_center_y_preserves_input_and_output_on_success()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    const cyberdeck_wifi_icon_layout_t before = layout;
    float visual_center_y = -7.0f;

    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &visual_center_y));
    /* O contrato deriva o centro da altura da caixa: box_height - 12,5 px.
     * Este fixture usa 24 px, portanto nao se deve esperar o valor especifico
     * da caixa padrao de 42 px. */
    CHECK_NEAR(visual_center_y, layout.box_height - 12.5f, 0.0f);
    CHECK(layout_bitwise_equal(layout, before));
}

void test_visual_center_y_rejects_null_invalid_and_non_finite_inputs()
{
    cyberdeck_wifi_icon_layout_t layout = make_valid_layout();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    float output = 123.25f;
    CHECK(!cyberdeck_wifi_icon_calculate_visual_center_y(nullptr, &output));
    CHECK_NEAR(output, 123.25f, 0.0f);
    CHECK(!cyberdeck_wifi_icon_calculate_visual_center_y(&layout, nullptr));

    for (const float invalid : {nan, inf, -inf}) {
        cyberdeck_wifi_icon_layout_t invalid_layout = layout;
        invalid_layout.box_height = invalid;
        const cyberdeck_wifi_icon_layout_t before = invalid_layout;
        output = 123.25f;
        CHECK(!cyberdeck_wifi_icon_calculate_visual_center_y(&invalid_layout, &output));
        CHECK_NEAR(output, 123.25f, 0.0f);
        CHECK(layout_bitwise_equal(invalid_layout, before));
    }

    cyberdeck_wifi_icon_layout_t zero_height = layout;
    zero_height.box_height = 0.0f;
    const cyberdeck_wifi_icon_layout_t before_zero_height = zero_height;
    output = 123.25f;
    CHECK(!cyberdeck_wifi_icon_calculate_visual_center_y(&zero_height, &output));
    CHECK_NEAR(output, 123.25f, 0.0f);
    CHECK(layout_bitwise_equal(zero_height, before_zero_height));
}

void test_visual_center_y_is_invariant_to_geometry_gap_and_state()
{
    cyberdeck_wifi_icon_layout_t baseline = make_valid_layout();
    float expected = 0.0f;
    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&baseline, &expected));

    const float centers_x[] = {0.0f, 17.5f, 201.0f, 999.0f};
    const float radii[] = {1.0f, 8.0f, 20.0f, 49.0f};
    const float thicknesses[] = {0.1f, 1.0f, 3.0f, 10.0f};
    const float point_radii[] = {0.1f, 2.0f, 7.0f, 20.0f};
    const float gaps[] = {CYBERDECK_WIFI_ICON_POINT_GAP_MIN,
                          CYBERDECK_WIFI_ICON_POINT_GAP,
                          CYBERDECK_WIFI_ICON_POINT_GAP_MAX};

    for (const float center_x : centers_x) {
        for (const float radius : radii) {
            for (const float thickness : thicknesses) {
                for (const float point_radius : point_radii) {
                    cyberdeck_wifi_icon_layout_t layout = baseline;
                    layout.center_x = center_x;
                    layout.radii[2] = radius;
                    layout.thickness = thickness;
                    layout.point_radius = point_radius;
                     float actual = 0.0f;
                     CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &actual));
                     CHECK_NEAR(actual, expected, 0.0f);
                     for (const float gap : gaps) {
                         /* O centro visual nao recebe gap e deve permanecer
                          * inalterado; o centro do ponto, por outro lado,
                          * deve refletir exatamente o gap solicitado. */
                         const float point_center =
                             cyberdeck_wifi_icon_calculate_point_center_y(&layout, gap);
                         const float expected_point_center =
                             cyberdeck_wifi_icon_visual_arc_bottom_y(&layout) + gap +
                             layout.point_radius;
                         CHECK_NEAR(point_center, expected_point_center, 0.0f);

                         float gap_independent_center = 0.0f;
                         CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(
                             &layout, &gap_independent_center));
                         CHECK_NEAR(gap_independent_center, expected, 0.0f);
                         for (int enabled = 0; enabled <= 1; ++enabled) {
                             for (int connected = 0; connected <= 1; ++connected) {
                                 for (int has_ip = 0; has_ip <= 1; ++has_ip) {
                                    (void)cyberdeck_wifi_icon_is_lit(enabled != 0,
                                                                     connected != 0,
                                                                     has_ip != 0);
                                    float state_actual = 0.0f;
                                    CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(
                                        &layout, &state_actual));
                                    CHECK_NEAR(state_actual, expected, 0.0f);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void test_visual_center_y_keeps_y_across_container_widths()
{
    const float widths[] = {0.5f, 1.0f, 40.0f, 100.0f, 216.0f, 720.0f, 1280.0f};
    float reference = 0.0f;
    bool have_reference = false;

    for (const float width : widths) {
        cyberdeck_wifi_icon_layout_t layout = {};
        CHECK(cyberdeck_wifi_icon_calculate_layout(width, 42.0f, 2.0f, &layout));
        float actual = 0.0f;
        CHECK(cyberdeck_wifi_icon_calculate_visual_center_y(&layout, &actual));
        if (!have_reference) {
            reference = actual;
            have_reference = true;
        } else {
            CHECK_NEAR(actual, reference, 0.0f);
        }
        CHECK_NEAR(actual, 29.5f, 1.5f);
    }
}

}  // namespace anonymous

/* ---------------------------------------------------------- main */

int main()
{
    /* Validacao de dimensoes */
    test_valid_layout();
    test_null_layout_rejected();
    test_zero_box_dimensions_rejected();
    test_negative_box_dimensions_rejected();
    test_non_finite_box_dimensions_rejected();
    test_radius_out_of_range_rejected();
    test_thickness_out_of_range_rejected();
    test_angle_out_of_range_rejected();
    test_point_radius_out_of_range_rejected();
    test_non_finite_geometry_rejected();
    test_all_boundaries_valid();

    /* Tres arcos + ponto */
    test_three_arcs_and_point();
    test_path_count_null_rejected();
    test_path_count_invalid_rejected();
    test_arc_count_constant();

    /* Determinismo */
    test_deterministic_validate();
    test_deterministic_path_count();
    test_deterministic_is_lit();
    test_deterministic_with_different_inputs();

    /* Estado lit separado */
    test_lit_delegates_to_existing_predicate();
    test_lit_only_when_all_three_true();
    test_lit_disabled_dominates();

    /* Geometria do layout */
    test_layout_fields_default_to_zero();
    test_layout_center_position();
    test_layout_radii_independent();
    test_layout_box_dimensions();

    /* Integracao */
    test_full_pipeline_valid_layout();
    test_full_pipeline_invalid_layout();
    test_layout_with_all_arc_angles();

    /* Calculo de layout e margem ~2px */
    test_determinant_outer_radius();
    test_visible_half_width();
    test_margin_2px_positioning();
    test_calculate_layout_invalid_args();
    test_header_42px_invariants();

    /* Selecao do arco determinante */
    test_determinant_selects_dominant_arc();
    test_determinant_handles_tie();
    test_determinant_dominates_every_arc();
    test_determinant_invalid_layout_zero();

    /* Extensao visivel */
    test_visible_extent_default_layout();
    test_visible_extent_from_dominant_arc();
    test_visible_half_width_invalid_layout_zero();
    test_visible_half_width_invariant_to_container_width();
    test_visible_half_width_uses_45_degree_projection();
    test_visual_arc_bottom_uses_open_up_geometry();
    test_visual_arc_bottom_selects_dominant_outer_radius();
    test_visual_arc_bottom_invalid_layout_returns_zero();
    test_visual_arc_bottom_is_deterministic_and_finite();

    /* Larguras validas/invalidas */
    test_calculate_layout_valid_widths();
    test_calculate_layout_invalid_widths();
    test_calculate_layout_rejects_non_finite_args();
    test_calculate_layout_preserves_out_on_failure();

    /* Determinismo do calculo */
    test_calculate_layout_deterministic();
    test_calculate_layout_deterministic_non_default_margin();

    /* Monotonicidade */
    test_calculate_layout_monotonic_in_width();
    test_calculate_layout_monotonic_in_margin();

    /* Margem final ~2px */
    test_final_margin_tracks_container_width();
    test_final_margin_zero_requested();
    test_final_margin_custom_requested();

    /* Casos angulares */
    test_calculate_layout_sets_default_angles();
    test_validate_angular_boundaries_per_arc();
    test_validate_rejects_angle_out_of_range_per_arc();

    /* Integracao de geometria */
    test_full_geometry_pipeline_various_widths();

    /* Centro Y do ponto e gap visual */
    test_point_center_y_preserves_requested_visual_gap();
    test_point_center_y_keeps_point_radius_unchanged();
    test_point_center_y_is_below_arcs_and_inside_header();
    test_point_center_y_rejects_invalid_gaps_and_layouts();
    test_point_center_y_rejects_non_finite_gaps();
    test_point_center_y_is_deterministic();

    /* Centro visual vertical */
    test_visual_center_y_is_near_textual_center_for_42px_box();
    test_visual_center_y_is_deterministic();
    test_visual_center_y_preserves_input_and_output_on_success();
    test_visual_center_y_rejects_null_invalid_and_non_finite_inputs();
    test_visual_center_y_is_invariant_to_geometry_gap_and_state();
    test_visual_center_y_keeps_y_across_container_widths();

    if (s_failures == 0) {
        std::printf("PASS: cyberdeck_wifi_icon_layout (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
