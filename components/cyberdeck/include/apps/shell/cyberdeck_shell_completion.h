#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/*
 * Completacao por Tab do shell local (REQ-TAB-01..05).
 *
 * Este modulo concentra a parte PURA: localizar o token sob o cursor, decidir
 * se ele e o verbo (comandos) ou um caminho (cwd virtual), aplicar o resultado
 * de uma lista de candidatos e formatar a lista bounded que o terminal exibe.
 * Nada aqui faz I/O, toca LVGL ou conhece a sessao.
 *
 * A unica funcao com I/O e `scan_directory()`, declarada no fim deste header e
 * implementada em `cyberdeck_shell_completion_scan.cpp`; ela roda fora da task
 * LVGL, no worker de completacao.
 *
 * Por que header-only: o inventario de TUs de `test_shell_session`/
 * `test_shell_app` e fixado por um contrato de baseline imutavel
 * (`tests/host/keymap/incremental_final_baseline.json`), entao a logica pura
 * compartilhada pela sessao nao pode virar um objeto novo desses alvos. Mesmo
 * motivo de `cyberdeck_shell_help.h`.
 *
 * Regras de dominio (AGENTS/plano aprovado):
 *  - comandos = catalogo de help + comandos declarados por manifests, sem
 *    duplicatas (a sessao monta essa lista; aqui so filtramos por prefixo);
 *  - caminhos sao virtuais (raiz `/`), relativos ao cwd ou absolutos;
 *  - `cd` completa somente diretorios; qualquer diretorio completado leva `/`;
 *  - nomes com espaco/controle nao entram: o tokenizador do shell nao tem
 *    aspas/escape, entao inserir tal nome geraria uma linha invalida;
 *  - zero candidatos NUNCA altera a linha;
 *  - lista truncada com MAIS DE UM candidato nunca estreita o token: nesse
 *    caso o Tab apenas lista (no segundo Tab) e nunca insere prefixo. Excecao
 *    deliberada: um UNICO candidato real completa, mesmo com a varredura
 *    truncada, porque nao existe ambiguidade a resolver;
 *  - 1 candidato completa; varios inserem o prefixo comum; o segundo Tab sem
 *    edicao apenas lista.
 */
namespace cyberdeck_shell_completion {

/* Limites duros: candidatos por lista (commands e paths), bytes de um nome,
 * bytes de um token completavel, bytes de um diretorio virtual e entradas de
 * diretorio examinadas por varredura.  Nada aqui cresce com o conteudo do SD. */
constexpr std::size_t k_max_candidates = 64;
constexpr std::size_t k_max_name_bytes = 128;
constexpr std::size_t k_max_token_bytes = 256;
constexpr std::size_t k_max_directory_bytes = 256;
constexpr std::size_t k_max_scan_entries = 512;
constexpr std::size_t k_list_line_bytes = 64;
constexpr std::string_view k_truncated_note = "tab: more candidates not shown\n";

/* `cd` aceita somente diretorios; os demais comandos de caminho aceitam
 * arquivos e diretorios. */
enum class path_mode { any, directory_only };

struct candidate {
    std::string name;
    bool directory = false;
};

using candidate_list = std::vector<candidate>;

/* Comandos locais que recebem um caminho.  A tabela e cruzada com
 * `cyberdeck_shell_help::kCatalog` pelo teste focado, para nao divergir do help
 * publicado (REQ-TAB-01). */
struct path_command {
    std::string_view command;
    path_mode mode;
};

inline constexpr path_command kPathCommands[] = {
    {"cd", path_mode::directory_only}, {"ls", path_mode::any}, {"cat", path_mode::any},   {"touch", path_mode::any},
    {"mkdir", path_mode::any},         {"rm", path_mode::any}, {"rmdir", path_mode::any},
};

inline bool is_separator(char byte) {
    return byte == ' ' || byte == '\t';
}

inline std::string_view first_token(std::string_view line) {
    std::size_t at = 0;
    while (at < line.size() && is_separator(line[at]))
        ++at;
    const std::size_t begin = at;
    while (at < line.size() && !is_separator(line[at]))
        ++at;
    return line.substr(begin, at - begin);
}

struct token_span {
    std::size_t start = 0;
    std::size_t end = 0;
};

/* Token que contem ou termina no cursor.  Cursor sobre um separador (ou fora
 * da linha) nao define token. */
inline bool token_at(std::string_view line, std::size_t cursor, token_span &out) {
    if (cursor > line.size())
        return false;
    if (cursor < line.size() && is_separator(line[cursor]))
        return false;
    std::size_t start = cursor;
    while (start > 0 && !is_separator(line[start - 1]))
        --start;
    std::size_t end = cursor;
    while (end < line.size() && !is_separator(line[end]))
        ++end;
    out.start = start;
    out.end = end;
    return true;
}

inline bool command_accepts_path(std::string_view command, path_mode &mode) {
    for (const path_command &entry : kPathCommands) {
        if (entry.command == command) {
            mode = entry.mode;
            return true;
        }
    }
    return false;
}

/* Acrescenta os componentes de `text`, aplicando `.`/`..` com clamp na raiz
 * virtual.  `parts` comeca vazio na raiz ou com o cwd ja resolvido. */
inline bool append_components(std::string_view text, std::vector<std::string> &parts) {
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == '/')
            ++at;
        if (at == text.size())
            break;
        const std::size_t begin = at;
        while (at < text.size() && text[at] != '/')
            ++at;
        const std::string_view part = text.substr(begin, at - begin);
        if (part == ".")
            continue;
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
            continue;
        }
        if (part.size() > k_max_name_bytes)
            return false;
        parts.emplace_back(part.data(), part.size());
    }
    return true;
}

/* Diretorio virtual absoluto que `prefix` designa a partir de `cwd`. */
inline bool resolve_virtual_directory(std::string_view cwd, std::string_view prefix, std::string &out) {
    out.clear();
    if (cwd.empty() || cwd.front() != '/')
        return false;
    std::vector<std::string> parts;
    if (!prefix.empty() && prefix.front() == '/') {
        if (!append_components(prefix, parts))
            return false;
    } else {
        if (!append_components(cwd, parts))
            return false;
        if (!append_components(prefix, parts))
            return false;
    }
    /* `/sdcard` e o alias fisico reservado: o shell virtual o rejeita. */
    if (!parts.empty() && parts.front() == "sdcard")
        return false;
    out = "/";
    for (const std::string &part : parts) {
        if (out.back() != '/')
            out += '/';
        out += part;
    }
    return out.size() <= k_max_directory_bytes;
}

/* Pedido de enumeracao produzido por `classify()` para um caminho. */
struct request {
    std::string directory{"/"};
    std::string name_prefix;
    bool directory_only = false;
};

/* Resultado de `classify()`: o que o Tab deve completar nesta linha. */
struct target {
    bool valid = false;
    bool command = false;
    std::size_t start = 0;
    std::size_t end = 0;
    /* Bytes do token que permanecem antes do nome escolhido (prefixo de
     * diretorio, por exemplo "albuns/"). */
    std::string retained;
    std::string name_prefix;
    request scan;
};

/* Decide o alvo do Tab na posicao (line, cursor).  Devolve false quando a
 * posicao nao tem completacao (token de opcao, comando desconhecido, token
 * grande demais, caminho que escapa da raiz virtual). */
inline bool classify(std::string_view line, std::size_t cursor, std::string_view cwd, target &out) {
    out = {};
    token_span span;
    if (!token_at(line, cursor, span))
        return false;
    const std::string_view token = line.substr(span.start, span.end - span.start);
    if (token.size() > k_max_token_bytes)
        return false;

    out.start = span.start;
    out.end = span.end;
    if (line.substr(0, span.start).find_first_not_of(" \t") == std::string_view::npos) {
        out.command = true;
        out.name_prefix.assign(token.data(), token.size());
        out.valid = true;
        return true;
    }
    /* Opcoes (-a, -r, -h) nao sao completadas. */
    if (!token.empty() && token[0] == '-')
        return false;
    path_mode mode = path_mode::any;
    if (!command_accepts_path(first_token(line), mode))
        return false;

    const std::size_t slash = token.rfind('/');
    const std::string_view directory_prefix =
        slash == std::string_view::npos ? std::string_view() : token.substr(0, slash + 1);
    const std::string_view name_prefix = slash == std::string_view::npos ? token : token.substr(slash + 1);
    if (name_prefix.size() > k_max_name_bytes)
        return false;
    if (!resolve_virtual_directory(cwd, directory_prefix, out.scan.directory))
        return false;

    out.retained.assign(directory_prefix.data(), directory_prefix.size());
    out.name_prefix.assign(name_prefix.data(), name_prefix.size());
    out.scan.name_prefix = out.name_prefix;
    out.scan.directory_only = mode == path_mode::directory_only;
    out.valid = true;
    return true;
}

inline void match_commands(const std::vector<std::string> &commands, std::string_view prefix, candidate_list &out,
                           bool &truncated) {
    out.clear();
    truncated = false;
    for (const std::string &command : commands) {
        if (command.size() < prefix.size())
            continue;
        if (command.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (out.size() == k_max_candidates) {
            truncated = true;
            break;
        }
        out.push_back(candidate{command, false});
    }
}

inline std::string common_prefix(const candidate_list &candidates) {
    if (candidates.empty())
        return {};
    std::string prefix = candidates.front().name;
    for (std::size_t index = 1; index < candidates.size() && !prefix.empty(); ++index) {
        const std::string &name = candidates[index].name;
        std::size_t length = 0;
        while (length < prefix.size() && length < name.size() && prefix[length] == name[length])
            ++length;
        prefix.resize(length);
    }
    return prefix;
}

struct application {
    bool changed = false;
    bool list = false;
    std::string line;
    std::size_t cursor = 0;
};

/* Aplica a lista ao token.  `list_requested` marca o segundo Tab (a lista ja
 * foi pedida uma vez para esta mesma linha/cursor). */
inline application apply(std::string_view line, std::size_t cursor, const target &target,
                         const candidate_list &candidates, bool list_requested, bool truncated) {
    application result;
    result.line.assign(line.data(), line.size());
    result.cursor = cursor;
    if (!target.valid || target.end < target.start || target.end > result.line.size())
        return result;
    /* Zero candidatos: a linha e preservada. */
    if (candidates.empty())
        return result;
    /* Segundo Tab sem edicao: apenas lista, nunca reescreve o token. */
    if (list_requested) {
        result.list = true;
        return result;
    }
    /* Conjunto incompleto: nao ha como provar que o prefixo comum e o final. */
    if (truncated && candidates.size() != 1)
        return result;
    std::string replacement;
    if (candidates.size() == 1) {
        replacement = target.retained + candidates.front().name;
        if (candidates.front().directory)
            replacement += '/';
    } else {
        const std::string prefix = common_prefix(candidates);
        if (prefix.size() <= target.name_prefix.size())
            return result;
        replacement = target.retained + prefix;
    }
    result.line.replace(target.start, target.end - target.start, replacement);
    result.cursor = target.start + replacement.size();
    result.changed = true;
    return result;
}

/* Lista bounded, uma linha por vez, nomes separados por dois espacos. */
inline std::string format_candidates(const candidate_list &candidates, bool truncated) {
    std::string output;
    std::string line;
    for (const candidate &item : candidates) {
        std::string name = item.name;
        if (item.directory)
            name += '/';
        const std::size_t needed = line.empty() ? name.size() : line.size() + 2 + name.size();
        if (!line.empty() && needed > k_list_line_bytes) {
            output += line;
            output += '\n';
            line.clear();
        }
        if (!line.empty())
            line += "  ";
        line += name;
    }
    if (!line.empty()) {
        output += line;
        output += '\n';
    }
    if (truncated)
        output.append(k_truncated_note.data(), k_truncated_note.size());
    return output;
}

/* Enumeracao bounded de um diretorio virtual, confinada a `host_root`.
 * Implementada em `cyberdeck_shell_completion_scan.cpp` (unica parte com I/O);
 * roda no worker, nunca na task LVGL. */
bool scan_directory(const char *host_root, const request &request, candidate_list &out, bool &truncated);

} // namespace cyberdeck_shell_completion
