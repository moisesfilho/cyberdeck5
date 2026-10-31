#include "apps/serial/cyberdeck_ui_dump_serializer.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } } while (0)

void test_text_boundaries()
{
    using cyberdeck_serial::ui_dump::clamp_text;
    CHECK(clamp_text(std::string(119, 'x').c_str()).value.size() == 119);
    CHECK(!clamp_text(std::string(119, 'x').c_str()).truncated);
    CHECK(clamp_text(std::string(120, 'x').c_str()).value.size() == 120);
    CHECK(!clamp_text(std::string(120, 'x').c_str()).truncated);
    const auto over = clamp_text(std::string(121, 'x').c_str());
    CHECK(over.value.size() == 120 && over.truncated);

    const std::string boundary = std::string(118, 'x') + "€";
    const auto utf8 = clamp_text(boundary.c_str());
    CHECK(utf8.value.size() == 118 && utf8.truncated);
    CHECK(utf8.value == std::string(118, 'x'));
}

void test_node_and_result_json()
{
    using namespace cyberdeck_serial::ui_dump;
    const std::string plain = serialize_node("label", "exact", false, 1, 2, 3, 4);
    CHECK(plain == "{\"class\":\"label\",\"text\":\"exact\",\"x\":1,\"y\":2,\"w\":3,\"h\":4}");
    CHECK(plain.find("text_truncated") == std::string::npos);
    const std::string limited = serialize_node("textarea", std::string(120, 'x'), true, 0, 0, 1, 1);
    CHECK(limited.find("\"text_truncated\":true") != std::string::npos);
    CHECK(serialize_result("", false) == "{\"nodes\":[],\"truncated\":false}");
    CHECK(serialize_result(plain, true).find("\"truncated\":true") != std::string::npos);

    std::string nodes;
    bool truncated = false;
    const std::string too_large(k_max_nodes_bytes + 1, 'x');
    CHECK(!append_node(nodes, 0, too_large, truncated));
    CHECK(truncated && nodes.empty());
    CHECK(!append_node(nodes, k_max_nodes, plain, truncated));
    CHECK(truncated && nodes.empty());
    CHECK(append_node(nodes, 0, plain, truncated));
}

void test_malformed_utf8_is_escaped_without_changing_bounds()
{
    using namespace cyberdeck_serial::ui_dump;
    const std::string near_cut = std::string(119, 'x') + static_cast<char>(0xFF) + "tail";
    const auto clamped = clamp_text(near_cut.c_str());
    CHECK(clamped.truncated && clamped.value.size() == k_max_text_bytes);
    const std::string node = serialize_node("label", clamped.value, clamped.truncated, 0, 0, 1, 1);
    CHECK(node.find("\\u00ff") != std::string::npos);
    CHECK(node.find("\"text_truncated\":true") != std::string::npos);

    const std::string outside_cut = std::string(120, 'x') + static_cast<char>(0x80);
    const auto outside = clamp_text(outside_cut.c_str());
    CHECK(outside.truncated && outside.value.size() == k_max_text_bytes);
    CHECK(serialize_node("label", outside.value, outside.truncated, 0, 0, 1, 1)
              .find("\\u0080") == std::string::npos);

    const std::string valid = "valid-€-😀";
    CHECK(serialize_node("label", valid, false, 0, 0, 1, 1).find(valid) != std::string::npos);
}
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        using namespace cyberdeck_serial::ui_dump;
        if (std::string(argv[1]) == "--emit-malformed-json") {
            const std::string value = std::string(119, 'x') + static_cast<char>(0xFF) + "tail";
            const auto clamped = clamp_text(value.c_str());
            std::puts(serialize_result(serialize_node("label", clamped.value, clamped.truncated,
                                                      1, 2, 3, 4), true).c_str());
            return 0;
        }
        std::puts(serialize_result(serialize_node("label", "valid-json", false, 1, 2, 3, 4), true).c_str());
        return 0;
    }
    test_text_boundaries();
    test_node_and_result_json();
    test_malformed_utf8_is_escaped_without_changing_bounds();
    if (failures != 0) return 1;
    std::printf("PASS: serial_ui_dump (%d checks)\n", checks);
    return 0;
}
