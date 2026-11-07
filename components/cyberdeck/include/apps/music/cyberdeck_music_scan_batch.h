#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace cyberdeck_music {

inline constexpr std::size_t k_scan_name_bytes = 128;
inline constexpr std::size_t k_scan_name_page = 64;

/* One bounded page of child names for the recursive queue scan.
 *
 * The scan has to visit every child of a directory in ascending name order.
 * Selecting the next name by re-reading the whole directory for every child
 * made that cost quadratic and re-opened the FatFs directory per child, which
 * is what made reopening the player feel slow. The scan now keeps the smallest
 * `k_scan_name_page` names of one directory pass and repeats the pass with the
 * last processed name as watermark only when the page overflowed. Memory stays
 * bounded for any directory size, the visiting order is the same as the
 * previous lexicographic selection, and a single directory pass covers the
 * common case. */
class scan_name_page final {
  public:
    void clear() {
        count_ = 0;
    }
    std::size_t size() const {
        return count_;
    }
    std::string_view at(std::size_t index) const {
        return std::string_view(names_[index].data());
    }

    /* Offers one child name and keeps the page sorted and deduplicated.
     * Returns false when the page is full and the name either displaced the
     * current largest entry or is larger than every entry: the caller must
     * read the directory again after consuming the current page. Entries that
     * are displaced are larger than the page maximum, so the next pass with
     * that maximum as watermark offers them again. */
    bool offer(std::string_view name) {
        if (name.size() > k_scan_name_bytes)
            return true;
        std::size_t index = 0;
        while (index < count_ && at(index) < name)
            ++index;
        if (index < count_ && at(index) == name)
            return true;
        if (index >= k_scan_name_page)
            return false;
        const bool fits = count_ < k_scan_name_page;
        const std::size_t last = fits ? count_ : k_scan_name_page - 1;
        for (std::size_t slot = last; slot > index; --slot)
            names_[slot] = names_[slot - 1];
        std::memcpy(names_[index].data(), name.data(), name.size());
        names_[index][name.size()] = '\0';
        if (fits)
            ++count_;
        return fits;
    }

  private:
    std::array<std::array<char, k_scan_name_bytes + 1>, k_scan_name_page> names_{};
    std::size_t count_ = 0;
};

} // namespace cyberdeck_music
