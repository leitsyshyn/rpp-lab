#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <wf/primitives.h>

#include <wf/internal/chunk_ranges.h>
#include <wf/internal/frequency_maps.h>
#include <wf/runners.h>

namespace wf {

namespace {

using clock_type = std::chrono::steady_clock;

[[nodiscard]] double elapsed_seconds(clock_type::time_point start, clock_type::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

run_summary run_sequential_impl(const run_config& config) {
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

    const std::string input_path_text = config.input_path.string();
    const file_size_type input_size_bytes =
        detail::checked_input_size_bytes(text.size(), input_path_text);

    std::optional<double> tokenize_count_duration_seconds;
    std::optional<double> canonicalize_duration_seconds;

    count_type total_word_count = 0;
    internal::local_frequency_map local_frequencies;
    const auto tokenize_count_start = clock_type::now();
    internal::for_each_owned_word(text, {0, input_size_bytes}, false, [&](word_type word) {
        const auto [it, inserted] = local_frequencies.try_emplace(std::move(word), 0);
        it->second = detail::checked_add(it->second, 1);
        total_word_count = detail::checked_add(total_word_count, 1);
        static_cast<void>(inserted);
    });
    const auto tokenize_count_end = clock_type::now();
    tokenize_count_duration_seconds = elapsed_seconds(tokenize_count_start, tokenize_count_end);

    const auto canonicalize_start = clock_type::now();
    frequency_map frequencies = internal::canonicalize_frequency_map(local_frequencies);
    const auto canonicalize_end = clock_type::now();
    canonicalize_duration_seconds = elapsed_seconds(canonicalize_start, canonicalize_end);

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

    run_summary summary;
    summary.result.frequencies = std::move(frequencies);
    summary.result.total_word_count = total_word_count;
    summary.result.unique_word_count =
        detail::checked_word_count_size(summary.result.frequencies->size(), input_path_text);
    summary.result.input_size_bytes = input_size_bytes;

    if (config.benchmark_enabled) {
        report.input_size_bytes = summary.result.input_size_bytes;
        report.word_count = summary.result.total_word_count;
        report.unique_word_count = summary.result.unique_word_count;
        report.total_seconds = elapsed_seconds(total_start, total_end);
        report.phases.push_back(
            {"read", elapsed_seconds(read_start, read_end), phase_scope::local});
        report.phases.push_back(
            {"tokenize_count", *tokenize_count_duration_seconds, phase_scope::local});
        report.phases.push_back(
            {"canonicalize", *canonicalize_duration_seconds, phase_scope::local});
        if (write_duration_seconds.has_value()) {
            report.phases.push_back({"write", *write_duration_seconds, phase_scope::local});
        }
        report.phases.push_back({"total", report.total_seconds, phase_scope::local});
        summary.benchmark = std::move(report);
    }

    return summary;
}

} // namespace

run_summary run_sequential(const run_config& config) {
    return run_sequential_impl(config);
}

} // namespace wf
