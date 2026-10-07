#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cyberdeck_editor {

inline constexpr std::size_t k_max_document_bytes = 12000;
inline constexpr std::size_t k_max_undo = 8;
inline constexpr std::size_t k_max_history_bytes = 32768;

enum class encoding : std::uint8_t { utf8, utf16_le, utf16_be, windows_1252 };
enum class line_ending : std::uint8_t { lf, crlf, cr };
enum class load_status : std::uint8_t { ok, too_large, binary, invalid_encoding, unsupported };

struct document_info {
    encoding codec = encoding::utf8;
    line_ending eol = line_ending::lf;
    bool bom = false;
};
struct load_result {
    load_status status = load_status::invalid_encoding;
    std::string text;
    document_info info{};
};

load_result decode(std::string_view bytes);
bool encode(std::string_view text, document_info info, std::string &bytes);

enum class key : std::uint8_t { left, right, up, down, backspace, del, enter, undo, redo, character };

class model final {
  public:
    bool open(std::string_view bytes);
    bool insert(std::string_view utf8);
    bool handle(key pressed, std::string_view character = {});
    bool replace_selection(std::string_view utf8);
    bool find_next(std::string_view needle);
    bool move_vertical(int lines);
    bool gesture_scroll(int pixel_delta);
    bool undo();
    bool redo();
    bool dirty() const {
        return dirty_;
    }
    bool save_as_confirmation_required() const {
        return save_as_confirmation_;
    }
    void confirm_save_as(bool accepted) {
        save_as_confirmation_ = !accepted;
    }
    bool save(std::string &bytes) const;
    std::string_view text() const {
        return text_;
    }
    std::size_t cursor() const {
        return cursor_;
    }
    std::size_t selection_start() const {
        return selection_start_;
    }
    std::size_t selection_end() const {
        return selection_end_;
    }
    const document_info &info() const {
        return info_;
    }
    void set_path(std::string path) {
        path_ = std::move(path);
    }
    std::string_view path() const {
        return path_;
    }
    void request_save_as() {
        save_as_confirmation_ = true;
    }
    void clear_dirty() {
        dirty_ = false;
    }

  private:
    struct snapshot {
        std::string text;
        std::size_t cursor;
        std::size_t start;
        std::size_t end;
    };
    bool mutate(std::string_view replacement, std::size_t begin, std::size_t end);
    bool remember(std::array<std::unique_ptr<snapshot>, k_max_undo> &history, std::size_t &count);
    static std::size_t previous_codepoint(std::string_view value, std::size_t offset);
    static std::size_t next_codepoint(std::string_view value, std::size_t offset);
    std::string text_;
    std::string path_;
    document_info info_{};
    std::array<std::unique_ptr<snapshot>, k_max_undo> undo_{};
    std::size_t undo_count_ = 0;
    std::array<std::unique_ptr<snapshot>, k_max_undo> redo_{};
    std::size_t redo_count_ = 0;
    std::size_t cursor_ = 0;
    std::size_t selection_start_ = 0;
    std::size_t selection_end_ = 0;
    bool dirty_ = false;
    bool save_as_confirmation_ = false;
};

} // namespace cyberdeck_editor
