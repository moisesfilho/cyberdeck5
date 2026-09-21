#include "cyberdeck_history.h"

/* Acesso seguro fora de range: retorna referencia a uma string vazia
 * estatica (deterministico, sem UB, thread-safe para leitura). */
namespace {
const std::string k_empty;
}

bool cyberdeck_history::is_blank(const std::string &line)
{
    return line.find_first_not_of(" \t") == std::string::npos;
}

bool cyberdeck_history::is_blank(const char *line)
{
    if (line == nullptr) {
        return true;
    }
    for (const char *p = line; *p != '\0'; ++p) {
        if (*p != ' ' && *p != '\t') {
            return false;
        }
    }
    return true;
}

bool cyberdeck_history::add(const std::string &line)
{
    if (is_blank(line)) {
        m_pos = m_entries.size();
        return false;
    }
    m_entries.push_back(line);
    if (m_entries.size() > limit) {
        m_entries.erase(m_entries.begin()); /* descarta a mais antiga */
    }
    m_pos = m_entries.size();
    return true;
}

bool cyberdeck_history::add(const char *line)
{
    if (line == nullptr) {
        m_pos = m_entries.size();
        return false;
    }
    return add(std::string(line));
}

void cyberdeck_history::move_up()
{
    if (m_entries.empty()) {
        return;
    }
    if (m_pos > 0) {
        --m_pos;
    }
}

void cyberdeck_history::move_down()
{
    if (m_entries.empty()) {
        return;
    }
    if (m_pos < m_entries.size()) {
        ++m_pos;
    }
}

void cyberdeck_history::reset_position()
{
    m_pos = m_entries.size();
}

const std::string &cyberdeck_history::current() const
{
    return m_pos < m_entries.size() ? m_entries[m_pos] : k_empty;
}

const std::string &cyberdeck_history::at(size_t i) const
{
    return i < m_entries.size() ? m_entries[i] : k_empty;
}
