#pragma once

#include <cstddef>
#include <string>
#include <vector>

/**
 * Historico local de comandos do terminal TUI unificado.
 *
 * Logica pura extraida de cyberdeck_ui.cpp (namespace anonimo, acoplado ao
 * LVGL) para permitir testes host-side sem shim/mocks frágeis. O contrato
 * espelha EXATAMENTE o comportamento atual do TUI:
 *
 *  - Linha "em branco" (apenas espacos e tabs, ou vazia) NAO e persistida.
 *    Predicado identico ao do TUI: find_first_not_of(" \t") == npos.
 *  - Limite HARD de `limit` (64) entradas: ao exceder, a entrada mais antiga
 *    e descartada (erase(begin)).
 *  - Navegacao Up/Down percorre do final (mais recente) para o inicio
 *    (mais antigo), com clamp nas bordas; a posicao "final" (== size())
 *    significa "linha de edicao vazia".
 *  - add() espelha execute_line(): SEMPRE re-sincroniza a posicao de
 *    navegacao com o final (m_pos == size(), linha de edicao vazia).
 *    Persistir acontece apenas para o fluxo de comando local (os guards de
 *    estado SSH/password/host-key sao fluxo do TUI e ficam fora deste
 *    utilitario puro).
 *
 * Nao inclui LVGL - pode ser compilado e testado diretamente em host.
 */
class cyberdeck_history {
public:
    static constexpr size_t limit = 64;

    /** True sse a linha contem apenas espacos/tabs (ou e vazia ou nula).
     *  Paridade com find_first_not_of(" \t") == npos. */
    static bool is_blank(const std::string &line);
    static bool is_blank(const char *line);

    cyberdeck_history() = default;

    /** Espelha execute_line(): re-sincroniza a posicao com o final e, se a
     *  linha nao for em branco, acrescenta ao historico (descartando a mais
     *  antiga se o limite for excedido). Retorna true se persistiu.
     *  Linhas em branco retornam false e NAO sao persistidas. */
    bool add(const std::string &line);
    bool add(const char *line);

    /** Sobe em direcao a entrada mais antiga (Up). Sem efeito se o
     *  historico estiver vazio ou ja estiver na borda (clamp). */
    void move_up();
    /** Desce em direcao ao final / linha de edicao vazia (Down). */
    void move_down();

    /** Sincroniza a posicao de navegacao com o final. Equivale a
     *  `s_history_pos = s_history.size();` de execute_line(). */
    void reset_position();

    /** Linha na posicao de navegacao atual; "" na posicao final
     *  (nenhuma entrada selecionada). */
    const std::string &current() const;

    size_t size() const { return m_entries.size(); }
    bool empty() const { return m_entries.empty(); }

    /** Entrada no indice i; "" se i >= size() (acesso seguro e
     *  deterministico, sem UB em bordas). */
    const std::string &at(size_t i) const;

    void clear() { m_entries.clear(); m_pos = 0; }

private:
    std::vector<std::string> m_entries;
    size_t m_pos = 0; /* posicao de navegacao; == size() => final/linha vazia */
};