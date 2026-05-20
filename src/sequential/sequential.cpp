#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <wf/utils.h>
#include <wf/runners.h>

namespace wf {

run_result run_sequential(const run_config& config) {
    if (config.input_path.empty()) {
        throw std::runtime_error("sequential runner requires an input path");
    }
    if (config.output_enabled && !config.finalize_enabled) {
        throw std::runtime_error("sequential output requires deterministic finalization");
    }

    const auto total_start = clock::now();

    const auto read_start = clock::now();
    const std::string text = read_file(config.input_path);
    const auto read_end = clock::now();

    std::size_t total_word_count = 0;
    unordered_frequency_map unordered_frequencies;

    const auto count_start = clock::now();
    for_each_word(text, {0, text.size()}, [&](std::string word) {
        count_word(unordered_frequencies, total_word_count, std::move(word));
    });
    const auto count_end = clock::now();
    const std::size_t unique_word_count = unordered_frequencies.size();

    const auto finalize_start = clock::now();
    std::optional<frequency_map> frequencies;
    if (config.finalize_enabled) {
        frequencies = materialize_frequency_map(std::move(unordered_frequencies));
    }
    const auto finalize_end = clock::now();

    const auto write_start = clock::now();
    if (config.output_enabled) {
        if (config.output_path.has_value()) {
            write_frequency_map(*config.output_path, *frequencies);
        } else {
            write_frequency_map(std::cout, *frequencies);
        }
    }
    const auto write_end = clock::now();

    const auto total_end = clock::now();

    run_result result;
    result.frequencies = std::move(frequencies);
    result.total_word_count = total_word_count;
    result.unique_word_count = unique_word_count;
    result.text_size = text.size();

    if (config.benchmark_enabled) {
        benchmark_data benchmark;
        benchmark.worker_count = 1;
        benchmark.total_duration = duration(total_start, total_end);
        benchmark.phases.push_back({"read", duration(read_start, read_end)});
        benchmark.phases.push_back({"count", duration(count_start, count_end)});
        benchmark.phases.push_back({"finalize", duration(finalize_start, finalize_end)});
        benchmark.phases.push_back({"write", duration(write_start, write_end)});
        result.benchmark = std::move(benchmark);
    }

    return result;
}

} // namespace wf
