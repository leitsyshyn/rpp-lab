#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <wf/primitives.h>

#include <wf/runners.h>

namespace wf {

namespace {

using clock_type = std::chrono::steady_clock;

[[nodiscard]] double elapsed_seconds(clock_type::time_point start, clock_type::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

} // namespace

run_summary run_sequential(const run_config& config) {
    if (config.input_path.empty()) {
        throw std::runtime_error("sequential runner requires an input path");
    }

    benchmark_report report;
    report.method = execution_method::sequential;
    report.worker_count = 1;

    const auto total_start = clock_type::now();

    const auto read_start = clock_type::now();
    const std::string text = read_text_file(config.input_path);
    const auto read_end = clock_type::now();

    const auto tokenize_start = clock_type::now();
    std::vector<word_type> words = extract_words(text);
    const auto tokenize_end = clock_type::now();

    const auto count_start = clock_type::now();
    frequency_map frequencies = count_words(words);
    const auto count_end = clock_type::now();

    std::optional<double> write_duration_seconds;
    if (config.output_enabled) {
        const auto write_start = clock_type::now();
        if (config.output_path.has_value()) {
            write_frequency_map(*config.output_path, frequencies);
        } else {
            write_frequency_map(std::cout, frequencies);
        }
        const auto write_end = clock_type::now();
        write_duration_seconds = elapsed_seconds(write_start, write_end);
    }

    const auto total_end = clock_type::now();
    const std::string input_path_text = config.input_path.string();

    run_summary summary;
    summary.result.frequencies = std::move(frequencies);
    summary.result.total_word_count =
        detail::checked_word_count_size(words.size(), input_path_text);
    summary.result.unique_word_count =
        detail::checked_word_count_size(summary.result.frequencies->size(), input_path_text);
    summary.result.input_size_bytes =
        detail::checked_input_size_bytes(text.size(), input_path_text);

    if (config.benchmark_enabled) {
        report.input_size_bytes = summary.result.input_size_bytes;
        report.word_count = summary.result.total_word_count;
        report.unique_word_count = summary.result.unique_word_count;
        report.total_seconds = elapsed_seconds(total_start, total_end);
        report.phases.push_back(
            {"read", elapsed_seconds(read_start, read_end), phase_scope::local});
        report.phases.push_back(
            {"tokenize", elapsed_seconds(tokenize_start, tokenize_end), phase_scope::local});
        report.phases.push_back(
            {"count", elapsed_seconds(count_start, count_end), phase_scope::local});
        if (write_duration_seconds.has_value()) {
            report.phases.push_back({"write", *write_duration_seconds, phase_scope::local});
        }
        report.phases.push_back({"total", report.total_seconds, phase_scope::local});
        summary.benchmark = std::move(report);
    }

    return summary;
}

} // namespace wf
