#include "platform/display/cyberdeck_terminal_scrollback.h"

#include <cassert>
#include <iostream>
#include <string>

using cyberdeck_terminal_scrollback::model;

static std::string repeated(char c, std::size_t n)
{
    return std::string(n, c);
}

static void test_exact_boundaries_and_clear()
{
    model m;
    assert(model::k_capacity == 12288);
    m.append(repeated('a', 12287).data(), 12287);
    assert(m.size() == 12287);
    m.append("b", 1);
    assert(m.size() == 12288);
    assert(m.text().front() == 'a' && m.text().back() == 'b');
    m.append("c", 1);
    assert(m.size() == 12288);
    assert(m.text().front() == 'a' && m.text().back() == 'c');
    m.clear();
    assert(m.size() == 0 && m.text().empty() && m.viewport(4096).empty());
    m.append("reusable", 8);
    assert(m.text() == "reusable");
}

static void test_utf8_cut_and_fragmented_append()
{
    model m;
    const std::string prefix = repeated('x', 12286);
    m.append(prefix.data(), prefix.size());
    const std::string utf8 = "\xC3\xA9Z";
    m.append(utf8.data(), 1); // split lead byte
    m.append(utf8.data() + 1, 2); // continuation + following ASCII
    assert(m.size() == 12288); // bounded pruning happens on each append
    assert(m.size() <= model::k_capacity);
    assert(m.text().find("\xC3\xA9Z") != std::string::npos);

    model cut;
    const std::string full = repeated('q', 12287) + "\xC3\xA9";
    cut.append(full.data(), full.size());
    assert(cut.size() <= model::k_capacity);
    assert(cut.text().find("\xC3\xA9") != std::string::npos);
    // The retained tail begins on a code-point boundary.
    assert((static_cast<unsigned char>(cut.text().front()) & 0xC0U) != 0x80U);
    const std::string view = cut.viewport(4096);
    assert(view.size() <= 4096);
    if (!view.empty()) assert((static_cast<unsigned char>(view.front()) & 0xC0U) != 0x80U);
}

static void test_viewport_is_bounded_without_changing_model()
{
    model m;
    const std::string data = repeated('d', 12288);
    m.append(data.data(), data.size());
    const std::string before = m.text();
    assert(m.viewport(4096).size() == 4096);
    assert(m.text() == before);
    char buffer[4096]{};
    assert(m.copy(buffer, sizeof(buffer)) == sizeof(buffer));
}

int main()
{
    test_exact_boundaries_and_clear();
    test_utf8_cut_and_fragmented_append();
    test_viewport_is_bounded_without_changing_model();
    std::cout << "PASS: terminal scrollback REQ-LAT-03-01/02/03/04/11\n";
}
