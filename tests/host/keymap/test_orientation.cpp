#include "platform/sensors/orientation.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static void test_orientation_from_accel_quadrants(void)
{
    // Quadrante 0 graus (aceleração orientada para baixo / padrão normal)
    assert(orientation_from_accel(0.0F, 1.0F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_0);
    assert(orientation_from_accel(0.2F, 0.9F, 0.1F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_0);
    assert(orientation_from_accel(-0.2F, 0.9F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_0);

    // Quadrante 90 graus (girado 90 graus para a direita)
    assert(orientation_from_accel(1.0F, 0.0F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_90);
    assert(orientation_from_accel(0.9F, 0.2F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_90);
    assert(orientation_from_accel(0.9F, -0.2F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_90);

    // Quadrante 180 graus (invertido)
    assert(orientation_from_accel(0.0F, -1.0F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_180);
    assert(orientation_from_accel(0.2F, -0.9F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_180);
    assert(orientation_from_accel(-0.2F, -0.9F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_180);

    // Quadrante 270 graus (girado 90 graus para a esquerda)
    assert(orientation_from_accel(-1.0F, 0.0F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_270);
    assert(orientation_from_accel(-0.9F, 0.2F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_270);
    assert(orientation_from_accel(-0.9F, -0.2F, 0.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_270);
}

static void test_orientation_from_accel_flat_fallback(void)
{
    // Plano / deitado na mesa: plano XY < 0.45G -> deve retornar o fallback fornecido
    assert(orientation_from_accel(0.0F, 0.0F, 1.0F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_0);
    assert(orientation_from_accel(0.1F, 0.1F, 0.98F, LV_DISPLAY_ROTATION_0) == LV_DISPLAY_ROTATION_0);
    assert(orientation_from_accel(0.2F, 0.2F, 0.9F, LV_DISPLAY_ROTATION_180) == LV_DISPLAY_ROTATION_180);
    assert(orientation_from_accel(0.0F, 0.0F, 0.0F, LV_DISPLAY_ROTATION_270) == LV_DISPLAY_ROTATION_270);
}

static void test_orientation_debounce_and_transitions(void)
{
    // Começa resetado em 0
    orientation_reset();

    // Configura orientação inicial detectada como 90 graus (simulando boot)
    orientation_set_current(LV_DISPLAY_ROTATION_90);

    // Amostras estáveis em 90 graus não devem alterar estado
    assert(orientation_update(1.0F, 0.0F, 0.0F) == LV_DISPLAY_ROTATION_90);

    // Envia 4 amostras na orientação 180 graus (DEBOUNCE_SAMPLES = 5)
    // Nenhuma das 4 primeiras deve trocar a orientação atual
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_90); // 1
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_90); // 2
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_90); // 3
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_90); // 4

    // 5ª amostra consecutiva em 180 graus deve consumar a transição
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_180); // 5 (estável)

    // Amostra seguinte mantém 180 graus
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_180);

    // Ruído/glitch transitório: 2 amostras de 270 graus seguidas de volta para 180 graus
    assert(orientation_update(-1.0F, 0.0F, 0.0F) == LV_DISPLAY_ROTATION_180); // ruído 1
    assert(orientation_update(-1.0F, 0.0F, 0.0F) == LV_DISPLAY_ROTATION_180); // ruído 2
    assert(orientation_update(0.0F, -1.0F, 0.0F) == LV_DISPLAY_ROTATION_180); // recuperado: contador reiniciado

    // Amostra em repouso plano não deve alterar a orientação estável de 180 graus
    assert(orientation_update(0.0F, 0.0F, 1.0F) == LV_DISPLAY_ROTATION_180);
}

int main(void)
{
    test_orientation_from_accel_quadrants();
    test_orientation_from_accel_flat_fallback();
    test_orientation_debounce_and_transitions();
    std::puts("PASS: test_orientation passed all test assertions.");
    return 0;
}
