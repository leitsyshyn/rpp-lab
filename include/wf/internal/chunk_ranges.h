#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <wf/contracts.h>
#include <wf/primitives.h>

namespace wf::internal {

struct byte_range {
    file_size_type begin{0};
    file_size_type end{0};
};

[[nodiscard]] inline std::vector<byte_range> build_even_byte_ranges(file_size_type total_size,
                                                                    std::size_t worker_count) {
    if (worker_count == 0) {
        throw std::runtime_error("worker count must be positive");
    }
    if (worker_count > static_cast<std::size_t>(std::numeric_limits<file_size_type>::max())) {
        throw std::runtime_error("worker count exceeds supported byte-range partitioning range");
    }

    std::vector<byte_range> ranges(worker_count);
    const file_size_type worker_count_value = static_cast<file_size_type>(worker_count);
    const file_size_type base_chunk_size = total_size / worker_count_value;
    const file_size_type remainder = total_size % worker_count_value;

    file_size_type begin = 0;
    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        const file_size_type extra_byte = worker_index < remainder ? 1U : 0U;
        const file_size_type chunk_size = base_chunk_size + extra_byte;
        const file_size_type end = begin + chunk_size;
        ranges[worker_index] = {begin, end};
        begin = end;
    }

    return ranges;
}

template <typename ConsumeWord>
void for_each_owned_word(std::string_view text, byte_range nominal_range, bool starts_inside_word,
                         ConsumeWord&& consume_word) {
    const file_size_type text_size = static_cast<file_size_type>(text.size());
    file_size_type position = std::min(nominal_range.begin, text_size);

    if (starts_inside_word) {
        while (position < text_size && detail::is_ascii_alphanumeric(static_cast<unsigned char>(
                                           text[static_cast<std::size_t>(position)]))) {
            ++position;
        }
    }

    while (position < text_size) {
        while (position < text_size && !detail::is_ascii_alphanumeric(static_cast<unsigned char>(
                                           text[static_cast<std::size_t>(position)]))) {
            ++position;
        }

        if (position >= text_size || position >= nominal_range.end) {
            break;
        }

        word_type word;
        while (position < text_size && detail::is_ascii_alphanumeric(static_cast<unsigned char>(
                                           text[static_cast<std::size_t>(position)]))) {
            word.push_back(detail::ascii_to_lower(
                static_cast<unsigned char>(text[static_cast<std::size_t>(position)])));
            ++position;
        }

        consume_word(std::move(word));
    }
}

} // namespace wf::internal
