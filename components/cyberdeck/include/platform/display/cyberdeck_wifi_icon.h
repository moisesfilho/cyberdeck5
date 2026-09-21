#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* O tipo LVGL permanece opaco para manter a API geometrica compilavel
 * tambem nos testes host-side, que nao incluem LVGL. */
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * API pura para o layout geometrico do icono Wi-Fi no header.
 *
 * O icono eh composto por tres arcos (barras de sinal) mais um ponto
 * central. Cada arco tem raio, espessura e angulo proprios, todos
 * configuraveis numa caixa variavel. O estado LIT e determinado pelo
 * predicado existente cyberdeck_wifi_indicator_is_lit().
 *
 * Contrato:
 *   - Tres arcos + ponto central;
 *   - Raios, espessura e angulos devem ser validos;
 *   - Dimensoes invalidas sao rejeitadas por cyberdeck_wifi_icon_validate;
 *   - Funcao determinista: mesma entrada -> mesma saida;
 *   - Estado LIT e separado do layout: reutiliza
 *     cyberdeck_wifi_indicator_is_lit();
 *   - O arco determinante (maior raio externo = radius + thickness) define a
 *     extensao visivel horizontal do icone;
 *   - O posicionamento final mantem a margem direita ~2px
 *     (CYBERDECK_WIFI_ICON_MARGIN_RIGHT) em relacao a largura real do
 *     container, via cyberdeck_wifi_icon_calculate_layout();
 *   - Monotonicidade: center_x cresce estritamente com a largura do container
 *     e decresce estritamente com a margem solicitada.
 */

#define CYBERDECK_WIFI_ICON_ARC_COUNT 3

/* Constantes geometricas padrao para o layout do icono no header */
#define CYBERDECK_WIFI_ICON_CONTAINER_HEIGHT 42.0f
#define CYBERDECK_WIFI_ICON_MARGIN_RIGHT     2.0f

#define CYBERDECK_WIFI_ICON_RADIUS_0         6.0f
#define CYBERDECK_WIFI_ICON_RADIUS_1        10.0f
#define CYBERDECK_WIFI_ICON_RADIUS_2        14.0f
#define CYBERDECK_WIFI_ICON_DEFAULT_THICKNESS 3.0f
#define CYBERDECK_WIFI_ICON_START_ANGLE     225.0f
#define CYBERDECK_WIFI_ICON_SWEEP_ANGLE      90.0f
#define CYBERDECK_WIFI_ICON_POINT_RADIUS     2.0f
#define CYBERDECK_WIFI_ICON_POINT_GAP_MIN    1.0f
#define CYBERDECK_WIFI_ICON_POINT_GAP_MAX    2.0f
#define CYBERDECK_WIFI_ICON_POINT_GAP         1.5f
#define CYBERDECK_WIFI_ICON_VISUAL_CENTER_Y_42PX 29.5f

/* Limites validos para as dimensoes geometricas. */
#define CYBERDECK_WIFI_ICON_MIN_RADIUS      0.1f
#define CYBERDECK_WIFI_ICON_MAX_RADIUS     50.0f
#define CYBERDECK_WIFI_ICON_MIN_THICKNESS  0.1f
#define CYBERDECK_WIFI_ICON_MAX_THICKNESS  50.0f
#define CYBERDECK_WIFI_ICON_MIN_ANGLE    -360.0f
#define CYBERDECK_WIFI_ICON_MAX_ANGLE     360.0f
#define CYBERDECK_WIFI_ICON_MIN_POINT_RADIUS 0.1f
#define CYBERDECK_WIFI_ICON_MAX_POINT_RADIUS 50.0f

/* Layout geometrico do icono Wi-Fi no caixote variavel do header. */
typedef struct {
    float center_x;              /* posicao X do centro do icono   */
    float center_y;              /* posicao Y do centro do icono   */
    float radii[CYBERDECK_WIFI_ICON_ARC_COUNT];   /* raio de cada arco       */
    float thickness;             /* espessura do traco dos arcos  */
    float start_angles[CYBERDECK_WIFI_ICON_ARC_COUNT]; /* angulo inicial de cada arco */
    float sweep_angles[CYBERDECK_WIFI_ICON_ARC_COUNT]; /* variacao angular de cada arco */
    float point_radius;          /* raio do ponto central          */
    float box_width;             /* largura da caixa variavel      */
    float box_height;            /* altura da caixa variavel       */
} cyberdeck_wifi_icon_layout_t;

/*
 * Valida todas as dimensoes geometricas do layout.
 * Retorna true se e somente se:
 *   - todos os raios estiverem em [MIN_RADIUS, MAX_RADIUS];
 *   - espessura estiver em [MIN_THICKNESS, MAX_THICKNESS];
 *   - angulos inicial e varredura estiverem em [MIN_ANGLE, MAX_ANGLE];
 *   - ponto_radius estiver em [MIN_POINT_RADIUS, MAX_POINT_RADIUS];
 *   - box_width e box_height estiverem estritamente positivas.
 * Se layout == NULL, retorna false.
 */
bool cyberdeck_wifi_icon_validate(const cyberdeck_wifi_icon_layout_t *layout);

/*
 * Calcula o centro visual vertical do icone, sem alterar o layout nem
 * depender de estado, largura, raio, espessura, ponto ou gap. Para a caixa
 * padrao de 42 px, o contrato fixa o centro em aproximadamente 29,5 px
 * (tolerancia de 1,5 px em relacao ao centro textual). A funcao usa apenas a
 * altura valida da caixa (center_y = box_height - 12,5 px), e retorna false
 * sem escrever em *out_center_y quando os argumentos forem nulos, invalidos
 * ou nao finitos.
 */
bool cyberdeck_wifi_icon_calculate_visual_center_y(
    const cyberdeck_wifi_icon_layout_t *layout, float *out_center_y);

/*
 * Calcula o layout geometrico completo para uma dada largura e altura do container
 * e margem direita desejada. O arco determinante (de maior raio externo =
 * radius + thickness) define a extensao visivel horizontal do traco.
 *
 * Contrato:
 *   - Preenche os raios padrao (RADIUS_0..RADIUS_2), a espessura padrao, os
 *     angulos padrao (START_ANGLE .. START_ANGLE + SWEEP_ANGLE) por arco, o
 *     ponto central (POINT_RADIUS) e as dimensoes da caixa;
 *   - A meia-largura visivel do arco determinante e
 *     (maior raio + espessura) * sin(45), pois o vao 225..315 graus projeta
 *     os extremos horizontais a 45 graus da vertical;
 *   - center_x = box_width - margin_right - meia_largura_visivel, de modo que
 *     a margem direita final
 *     (box_width - (center_x + meia_largura_visivel)) seja ~= margin_right,
 *     qualquer que seja a largura real do container;
 *   - center_y = box_height - 12,5 px (29,5 px para um header de 42 px);
 *   - Determinista: mesma entrada -> mesma saida;
 *   - Monotonico: center_x cresce com box_width e decresce com margin_right;
 *   - Retorna true e preenche *out_layout em caso de sucesso; retorna false
 *     sem escrever em *out_layout se box_width <= 0, box_height <= 0,
 *     margin_right < 0 ou out_layout for NULL.
 */
bool cyberdeck_wifi_icon_calculate_layout(float box_width,
                                          float box_height,
                                          float margin_right,
                                          cyberdeck_wifi_icon_layout_t *out_layout);

/*
 * Retorna o raio externo do arco determinante: o maior valor de
 * (radii[i] + thickness) entre os tres arcos. Em caso de empate, o arco de
 * menor indice define o determinante (o valor do raio externo e o mesmo).
 * Esse arco define a extensao visivel horizontal do icone.
 * Retorna 0.0f se layout for NULL ou invalido.
 */
float cyberdeck_wifi_icon_determinant_outer_radius(const cyberdeck_wifi_icon_layout_t *layout);

/*
 * Retorna a meia-largura visivel do traco do arco determinante projetada no
 * plano horizontal: (maior raio + espessura) * sin(45), pois o vao angular
 * padrao 225..315 graus coloca os extremos horizontais a 45 graus da vertical.
 * A extensao visivel total e 2 * meia-largura.
 * Retorna 0.0f se layout for NULL ou invalido.
 */
float cyberdeck_wifi_icon_visible_half_width(const cyberdeck_wifi_icon_layout_t *layout);

/*
 * Retorna o limite inferior visual dos arcos superiores. Para o vao padrao
 * aberto para cima, esse limite e calculado como
 * center_y - outer_radius * (1 - sin(45 graus)), usando o raio externo
 * determinante. Retorna 0.0f para layout nulo, invalido ou resultado nao
 * finito.
 */
float cyberdeck_wifi_icon_visual_arc_bottom_y(const cyberdeck_wifi_icon_layout_t *layout);

/*
 * Calcula o centro Y do ponto abaixo dos arcos, preservando a geometria do
 * ponto. O vao visual solicitado entre a borda inferior visual dos arcos e o
 * topo do ponto deve estar entre 1 e 2 px (inclusive). O chamador usa
 * CYBERDECK_WIFI_ICON_POINT_GAP (1.5 px) como gap padrao.
 * Retorna 0.0f se layout == NULL, layout for invalido ou gap estiver fora de
 * [CYBERDECK_WIFI_ICON_POINT_GAP_MIN, CYBERDECK_WIFI_ICON_POINT_GAP_MAX].
 * Funcao pura e deterministica.
 */
static inline float cyberdeck_wifi_icon_calculate_point_center_y(
    const cyberdeck_wifi_icon_layout_t *layout, float visual_gap)
{
    if (layout == NULL || !cyberdeck_wifi_icon_validate(layout) ||
        visual_gap != visual_gap ||
        visual_gap < CYBERDECK_WIFI_ICON_POINT_GAP_MIN ||
        visual_gap > CYBERDECK_WIFI_ICON_POINT_GAP_MAX) {
        return 0.0f;
    }

    return cyberdeck_wifi_icon_visual_arc_bottom_y(layout) + visual_gap +
           layout->point_radius;
}

/*
 * Calcula o numero total de segmentos de caminho necessarios para
 * renderizar o icono: 3 arcos (cada um com inicio e fim) + 1 ponto.
 * Retorna 4 para um layout valido. Se layout == NULL ou invalido,
 * retorna 0.
 */
size_t cyberdeck_wifi_icon_path_count(const cyberdeck_wifi_icon_layout_t *layout);

/*
 * Determina se o icono Wi-Fi deve ser exibido LIT.
 * Reutiliza o predicado existente cyberdeck_wifi_indicator_is_lit():
 *   LIT somente quando enabled && connected && has_ip.
 * Funcao pura e deterministica.
 */
bool cyberdeck_wifi_icon_is_lit(bool enabled, bool connected, bool has_ip);

/* Cria uma unica celula de header contendo os tres arcos e o ponto. */
lv_obj_t *cyberdeck_wifi_icon_create(lv_obj_t *parent);

/* Atualiza a posicao geometrica dos arcos quando a largura do container for alterada. */
void cyberdeck_wifi_icon_update_layout(lv_obj_t *icon, int32_t container_width);

/* Atualiza apenas as cores das primitivas ja criadas. */
void cyberdeck_wifi_icon_set_color(lv_obj_t *icon, uint32_t color_hex);

/* Remove a celula e todas as primitivas filhas, quando a UI for destruida. */
void cyberdeck_wifi_icon_destroy(lv_obj_t *icon);

#ifdef __cplusplus
}
#endif
