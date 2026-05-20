#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wf/contracts.h>

namespace wf {

struct range {
    std::size_t begin{0};
    std::size_t end{0};
};

using unordered_frequency_map = std::unordered_map<std::string, std::size_t>;
using clock = std::chrono::steady_clock;

[[nodiscard]] bool is_ascii_alphanumeric(char value) noexcept;

[[nodiscard]] char ascii_to_lower(char value) noexcept;

[[nodiscard]] inline double duration(clock::time_point start, clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

[[nodiscard]] std::vector<range> build_even_ranges(std::size_t size, int count);

inline void count_word(unordered_frequency_map& frequencies, std::size_t& total_word_count,
                       std::string word) {
    const auto [it, inserted] = frequencies.try_emplace(std::move(word), 0);
    ++it->second;
    ++total_word_count;
    static_cast<void>(inserted);
}

inline void count_word(unordered_frequency_map& frequencies, std::string word) {
    const auto [it, inserted] = frequencies.try_emplace(std::move(word), 0);
    ++it->second;
    static_cast<void>(inserted);
}

template <typename Callback>
void for_each_word(const std::string& text, range range, Callback&& callback) {
    const std::size_t text_size = text.size();
    const std::size_t range_end = std::min(range.end, text_size);
    std::size_t position = std::min(range.begin, text_size);

    if (position > 0 && is_ascii_alphanumeric(text[position - 1])) {
        while (position < text_size && is_ascii_alphanumeric(text[position])) {
            ++position;
        }
    }

    while (position < text_size) {
        while (position < text_size && !is_ascii_alphanumeric(text[position])) {
            ++position;
        }

        if (position >= text_size || position >= range_end) {
            break;
        }

        std::string word;
        while (position < text_size && is_ascii_alphanumeric(text[position])) {
            word.push_back(ascii_to_lower(text[position]));
            ++position;
        }

        callback(std::move(word));
    }
}

[[nodiscard]] std::string read_file(const std::filesystem::path& input_path);

[[nodiscard]] frequency_map materialize_frequency_map(unordered_frequency_map frequencies);

void write_frequency_map(std::ostream& output, const frequency_map& frequencies);

void write_frequency_map(const std::filesystem::path& output_path,
                         const frequency_map& frequencies);

void print_benchmark_report(std::ostream& output, method selected_method,
                            const run_result& result);

} // namespace wf
