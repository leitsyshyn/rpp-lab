#pragma once

#include <unordered_map>

#include <wf/contracts.h>

namespace wf::internal {

using local_frequency_map = std::unordered_map<word_type, count_type>;

[[nodiscard]] inline frequency_map
canonicalize_frequency_map(const local_frequency_map& local_frequencies) {
    frequency_map frequencies;
    for (const auto& [word, count] : local_frequencies) {
        frequencies.emplace(word, count);
    }

    return frequencies;
}

} // namespace wf::internal
