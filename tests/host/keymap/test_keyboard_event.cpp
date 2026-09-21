/*
 * Testes unitarios host-side para o parser PURO de eventos Character do
 * teclado M5Stack Tab5 (SKU A164). Cobre o contrato provavel exposto em
 * components/cyberdeck/include/platform/input/tab5_keyboard_event.h, com implementacao
 * futura em components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp (logica
 * extraida de tab5_keyboard.cpp -> drain_events()/emit_event()).
 *
 * CONTRATO SOB TESTE (especificado por este arquivo; a implementacao deve
 * fornece-lo com assinatura identica):
 *
 *   #define TAB5_CHAR_EVENT_MAX_TEXT 16     (paridade com CHAR_EVENT_MAX=16
 *                                            de tab5_keyboard.cpp)
 *
 *   typedef struct {
 *       uint8_t modifier;   -- byte 0 do evento bruto (bit0=Ctrl, bit2=Alt)
 *       uint8_t length;     -- comprimento do texto em bytes (sem o NUL)
 *       char text[TAB5_CHAR_EVENT_MAX_TEXT + 1];  -- texto UTF-8 NUL-terminated
 *   } tab5_char_event_t;
 *
 *   bool tab5_char_event_parse(const uint8_t *raw, size_t raw_len,
 *                              tab5_char_event_t *out);
 *
 * Semantica:
 *   - raw_len INCLUI o byte de modificador (paridade com REG_CHAR_EVENT_LEN
 *     do firmware): texto = raw[1 .. raw_len-1] e length = raw_len - 1.
 *     Ex.: {0,'a'} -> length 1 e texto "a"; {0,C3,A7} -> length 2.
 *   - Retorna true SO quando ha evento utilizavel (length > 0). false para
 *     raw==NULL, out==NULL, raw_len==0 ou raw_len==1 (modificador sem texto).
 *   - raw_len acima do limite e clampado; o texto nunca excede
 *     TAB5_CHAR_EVENT_MAX_TEXT bytes e termina com exatamente um NUL
 *     (text[length] == '\0', sem NUL extra).
 *   - O parser e guiado por raw_len, NUNCA por busca de NUL no buffer:
 *     bytes alem de raw_len (incluindo NULs no buffer fisico) sao ignorados.
 *
 * Logica pura (sem LVGL/FreeRTOS), mesma condicao de build de test_history:
 * apenas g++/make. Build: make test (alvo test_keyboard_event).
 */
#include "platform/input/tab5_keyboard_event.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int s_failures = 0;
int s_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

/* Limites do contrato. raw_len inclui o modificador, logo o maximo e
 * kSpecMaxTextLen + 1. O static_assert trava o build se a implementacao
 * encolher o buffer text[] abaixo do exigido. */
constexpr size_t kSpecMaxTextLen = 16;
constexpr size_t kSpecMaxRawLen = kSpecMaxTextLen + 1;

static_assert(sizeof(static_cast<tab5_char_event_t *>(nullptr)->text) >=
                  kSpecMaxTextLen + 1,
              "tab5_char_event_t::text deve caber 16 bytes de texto + NUL");

/* Executa o parser e valida o contrato completo: retorno, modificador,
 * length, bytes do texto (byte a byte) e terminador unico. Guarda contra
 * NUL extra no texto, truncamento e vazamento de bytes alem de raw_len. */
void expect_parse(const uint8_t *raw, size_t raw_len, uint8_t exp_modifier,
                  const std::string &exp_text, bool exp_usable)
{
    tab5_char_event_t ev{};
    const bool ok = tab5_char_event_parse(raw, raw_len, &ev);
    ++s_checks;
    if (ok != exp_usable) {
        ++s_failures;
        std::printf("FAIL %s:%d  parse(raw_len=%zu) retornou %d, esperado %d\n",
                    __FILE__, __LINE__, raw_len, (int)ok, (int)exp_usable);
        return;
    }
    if (!exp_usable) {
        return;
    }

    CHECK(ev.modifier == exp_modifier);
    CHECK(ev.length == exp_text.size());
    if (ev.length != exp_text.size()) {
        /* Evita memcmp fora dos limites quando length divergiu. */
        return;
    }
    CHECK(std::memcmp(ev.text, exp_text.data(), exp_text.size()) == 0);
    /* Terminador unico: exatamente um NUL ao final do texto reportado.
     * "sem NUL extra" do requisito -> nenhum byte NUL dentro de [0,length)
     * (coberto pelo memcmp) e text[length] e o unico terminador. */
    CHECK(ev.text[ev.length] == '\0');
}

/* Texto de teste: 'A'..'Z' repetido, n bytes (sem NUL interno). */
std::string pattern_text(size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += static_cast<char>('A' + (i % 26));
    }
    return s;
}

void test_ascii_char_basic()
{
    /* raw {0,'a'}, raw_len=2 (modificador incluso): texto "a", length 1,
     * sem NUL extra (terminador unico em text[1]). */
    const uint8_t raw[] = {0x00, 'a'};
    expect_parse(raw, sizeof(raw), 0x00, "a", true);
}

void test_modifier_propagated()
{
    /* Byte 0 e o modificador e nunca entra no texto nem no length. */
    const uint8_t raw_ctrl[] = {0x01, 'a'};
    expect_parse(raw_ctrl, sizeof(raw_ctrl), 0x01, "a", true);

    /* Ctrl|Alt (bit0|bit2 = 5), como emitido pelo firmware em Character. */
    const uint8_t raw_ctrl_alt[] = {0x05, 'a'};
    expect_parse(raw_ctrl_alt, sizeof(raw_ctrl_alt), 0x05, "a", true);

    /* Alt apenas (bit2 = 4). */
    const uint8_t raw_alt[] = {0x04, 'a'};
    expect_parse(raw_alt, sizeof(raw_alt), 0x04, "a", true);

    /* Bits nao documentados passam intactos (parser puro, sem mascara). */
    const uint8_t raw_opaque[] = {0x40, 'a'};
    expect_parse(raw_opaque, sizeof(raw_opaque), 0x40, "a", true);
}

void test_utf8_multibyte_preserved()
{
    /* c-cedilha UTF-8 {C3,A7} com raw_len=3: texto de 2 bytes preservado.
     * O parser entrega os bytes crus, sem interpretar/truncar UTF-8. */
    const uint8_t raw_cced[] = {0x00, 0xC3, 0xA7};
    expect_parse(raw_cced, sizeof(raw_cced), 0x00,
                 std::string("\xC3\xA7", 2), true);

    /* Euro UTF-8 {E2,82,AC} com raw_len=4: 3 bytes preservados. */
    const uint8_t raw_euro[] = {0x00, 0xE2, 0x82, 0xAC};
    expect_parse(raw_euro, sizeof(raw_euro), 0x00,
                 std::string("\xE2\x82\xAC", 3), true);

    /* Multibyte com modificador ativo: modificador propagado e texto intacto. */
    const uint8_t raw_cced_ctrl[] = {0x01, 0xC3, 0xA7};
    expect_parse(raw_cced_ctrl, sizeof(raw_cced_ctrl), 0x01,
                 std::string("\xC3\xA7", 2), true);
}

void test_raw_len_zero_and_one()
{
    /* raw_len 0: nada a parsear -> false. */
    expect_parse(nullptr, 0, 0, "", false);
    const uint8_t raw0[] = {0x00};
    expect_parse(raw0, 0, 0, "", false);

    /* raw_len 1: apenas o modificador, sem texto -> sem evento utilizavel. */
    const uint8_t raw_mod0[] = {0x00};
    expect_parse(raw_mod0, sizeof(raw_mod0), 0x00, "", false);
    const uint8_t raw_mod1[] = {0x01};
    expect_parse(raw_mod1, sizeof(raw_mod1), 0x01, "", false);

    /* raw==NULL com raw_len>0: defensivo, sem crash -> false. */
    expect_parse(nullptr, 5, 0, "", false);
}

void test_max_size_clamped_without_overflow()
{
    /* raw_len muito acima do limite (64): texto clampado em
     * kSpecMaxTextLen, sem estouro e com terminador no lugar. */
    uint8_t big[64] = {};
    big[0] = 0x01; /* modificador: Ctrl */
    for (size_t i = 1; i < sizeof(big); ++i) {
        big[i] = static_cast<uint8_t>('A' + ((i - 1) % 26));
    }
    expect_parse(big, sizeof(big), 0x01, pattern_text(kSpecMaxTextLen), true);

    /* raw_len exatamente no maximo (modificador + 16 bytes): length 16. */
    expect_parse(big, kSpecMaxRawLen, 0x01, pattern_text(kSpecMaxTextLen), true);

    /* raw_len um abaixo do maximo (16): length 15 (texto = raw_len - 1). */
    expect_parse(big, kSpecMaxTextLen, 0x01, pattern_text(kSpecMaxTextLen - 1), true);
}

void test_bounds_ignores_bytes_past_raw_len()
{
    /* Terceiro byte NUL com raw_len=2: apenas raw[0..1] participam; o NUL
     * na posicao 2 e ignorado (parser guiado por raw_len, nao por NUL). */
    const uint8_t raw_nul3[] = {0x00, 'a', 0x00, 'b'};
    expect_parse(raw_nul3, 2, 0x00, "a", true);

    /* Bytes nao-NUL alem de raw_len tambem sao ignorados: com raw_len=2 o
     * texto e 'a' mesmo que o buffer fisico continue com 'b' (uma busca
     * strlen por NUL erraria aqui, copiando "ab"). */
    const uint8_t raw_trail[] = {0x00, 'a', 'b', 0x00};
    expect_parse(raw_trail, 2, 0x00, "a", true);

    /* Mesmo cenario com modificador ativo. */
    const uint8_t raw_trail_ctrl[] = {0x01, 'a', 'b', 0x00};
    expect_parse(raw_trail_ctrl, 2, 0x01, "a", true);
}

void test_out_null_safe()
{
    /* out==NULL: retorno false sem crash (defensivo). */
    const uint8_t raw[] = {0x00, 'a'};
    CHECK(tab5_char_event_parse(raw, sizeof(raw), nullptr) == false);
}

} // namespace

int main()
{
    test_ascii_char_basic();
    test_modifier_propagated();
    test_utf8_multibyte_preserved();
    test_raw_len_zero_and_one();
    test_max_size_clamped_without_overflow();
    test_bounds_ignores_bytes_past_raw_len();
    test_out_null_safe();

    if (s_failures == 0) {
        std::printf("PASS: tab5_char_event_parse (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d de %d checks falharam\n", s_failures, s_checks);
    return 1;
}
