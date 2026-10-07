#include "apps/editor/cyberdeck_editor_model.h"

#include <cstdio>
#include <initializer_list>
#include <string>

using namespace cyberdeck_editor;
namespace {
int failures = 0;
int checks = 0;
void check(bool ok, const char *what) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } }
std::string bytes(std::initializer_list<unsigned char> input) {
    std::string result;
    for (unsigned char value : input) result.push_back(static_cast<char>(value));
    return result;
}

void codec_boundaries() {
    check(decode({}).status == load_status::ok, "empty document is valid");
    check(decode(std::string(1, 'x')).status == load_status::ok, "one byte loads");
    check(decode(std::string(11999, 'x')).status == load_status::ok, "11999 bytes load");
    check(decode(std::string(12000, 'x')).status == load_status::ok, "12000 bytes load");
    check(decode(std::string(12001, 'x')).status == load_status::too_large, "12001 bytes reject");

    const auto utf8 = decode("\xef\xbb\xbf" "caf\xc3\xa9\r\nnext");
    check(utf8.status == load_status::ok && utf8.info.codec == encoding::utf8 && utf8.info.bom &&
          utf8.info.eol == line_ending::crlf && utf8.text == "caf\xc3\xa9\r\nnext", "UTF-8 BOM and EOL preserved");
    const auto le = decode(bytes({0xff, 0xfe, 'A', 0, '\n', 0}));
    const auto be = decode(bytes({0xfe, 0xff, 0, 'A', 0, '\n'}));
    check(le.status == load_status::ok && le.info.codec == encoding::utf16_le && le.text == "A\n", "UTF-16 LE decodes");
    check(be.status == load_status::ok && be.info.codec == encoding::utf16_be && be.text == "A\n", "UTF-16 BE decodes");
    const auto cp = decode("\x93" "quote" "\x94" " " "\x80");
    check(cp.status == load_status::ok && cp.info.codec == encoding::windows_1252 &&
          cp.text == "\xe2\x80\x9cquote\xe2\x80\x9d \xe2\x82\xac", "Windows-1252 decodes");
    check(decode("\xff\xfe" "A").status == load_status::invalid_encoding, "odd UTF-16 rejects");
    check(decode(bytes({0xef, 0xbb, 0xbf, 0})).status == load_status::binary, "NUL UTF-8 rejects as binary");
    check(decode("\x01\x02\x03").status == load_status::ok, "binary-looking non-NUL bytes remain text");
    const auto isolated_cr = decode("first\rsecond");
    check(isolated_cr.status == load_status::ok && isolated_cr.info.eol == line_ending::cr,
          "isolated CR is detected as the document EOL");
}

void encoding_round_trip() {
    std::string bytes;
    check(encode("new\nfile\n", {}, bytes) && bytes == "new\nfile\n", "new files are UTF-8 without BOM and LF");
    document_info le{encoding::utf16_le, line_ending::crlf, true};
    check(encode("A\nB", le, bytes) && bytes == ::bytes({0xff, 0xfe, 'A', 0, '\r', 0, '\n', 0, 'B', 0}), "UTF-16 LE encodes code units and EOL");
    document_info be{encoding::utf16_be, line_ending::lf, true};
    check(encode("A\n", be, bytes) && bytes == ::bytes({0xfe, 0xff, 0, 'A', 0, '\n'}), "UTF-16 BE encodes code units and EOL");
    document_info cp{encoding::windows_1252, line_ending::cr, false};
    check(encode("\xe2\x80\x9c" "X" "\xe2\x80\x9d\n", cp, bytes) && bytes == ::bytes({0x93, 'X', 0x94, '\r'}), "Windows-1252 encodes supported glyphs");
    document_info isolated_cr{encoding::utf8, line_ending::cr, false};
    check(encode("first\rsecond\n", isolated_cr, bytes) && bytes == "first\rsecond\r",
          "isolated CR round-trips without being doubled");
}

void editing_and_history() {
    model m;
    check(m.open("one\ntwo\nthree"), "model opens text");
    check(m.find_next("two") && m.selection_start() == 4 && m.selection_end() == 7, "find selects next match");
    check(m.replace_selection("TWO") && m.text() == "one\nTWO\nthree", "selection replacement edits");
    check(m.undo() && m.text() == "one\ntwo\nthree", "undo restores edit");
    m.open("one\ntwo\nthree");
    check(m.handle(key::right) && m.handle(key::enter) && m.text() == "o\nne\ntwo\nthree", "enter inserts newline");
    check(m.handle(key::backspace) && m.text() == "one\ntwo\nthree", "backspace edits by codepoint");
    check(m.move_vertical(1) && m.gesture_scroll(-1), "vertical and gesture navigation are handled");
    m.request_save_as(); check(m.save_as_confirmation_required(), "save-as requires confirmation");
    m.confirm_save_as(true); check(!m.save_as_confirmation_required(), "save-as confirmation clears");
    std::string saved; check(m.save(saved), "edited document saves");
}

void utf8_editing_stays_on_codepoint_boundaries() {
    model vertical;
    check(vertical.open("a\xc3\xa9\nz\xc3\xa7\n"), "UTF-8 vertical document opens");
    check(vertical.handle(key::right) && vertical.handle(key::right) && vertical.cursor() == 3,
          "right navigation stops after the complete UTF-8 codepoint");
    check(vertical.move_vertical(1) && vertical.cursor() == 7,
          "vertical navigation preserves the codepoint column in bytes");
    check(vertical.move_vertical(-1) && vertical.cursor() == 3,
          "reverse vertical navigation returns to a codepoint boundary");

    model backspace;
    check(backspace.open("A\xc3\xa9" "B"), "UTF-8 backspace document opens");
    check(backspace.handle(key::right) && backspace.handle(key::right) && backspace.cursor() == 3,
          "cursor reaches the boundary after a multibyte codepoint");
    check(backspace.handle(key::backspace) && backspace.text() == "AB" && backspace.cursor() == 1,
          "backspace removes one complete UTF-8 codepoint");

    model del;
    check(del.open("A\xc3\xa9" "B"), "UTF-8 delete document opens");
    check(del.handle(key::right) && del.cursor() == 1,
          "delete starts at the boundary before the multibyte codepoint");
    check(del.handle(key::del) && del.text() == "AB" && del.cursor() == 1,
          "delete removes one complete UTF-8 codepoint");
}

void oversized_open_is_rejected_before_mutation() {
    model m;
    check(m.open("keep"), "oversize regression starts with a valid document");
    check(!m.open(std::string(k_max_document_bytes + 1, 'x')) && m.text() == "keep" && !m.dirty(),
          "document over 12000 bytes is rejected without replacing the open document");
}
}

int main() {
    codec_boundaries(); encoding_round_trip(); editing_and_history();
    utf8_editing_stays_on_codepoint_boundaries();
    oversized_open_is_rejected_before_mutation();
    std::printf("%s: editor model (%d checks)\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
