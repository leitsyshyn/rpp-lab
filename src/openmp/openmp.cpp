#ifdef WF_HAS_OPENMP
#include <omp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wf/primitives.h>
#include <wf/runners.h>

#include <wf/internal/chunk_ranges.h>
#include <wf/internal/frequency_maps.h>

namespace wf {

namespace {

using clock_type = std::chrono::steady_clock;
using local_frequency_map = internal::local_frequency_map;

[[nodiscard]] double elapsed_seconds(clock_type::time_point start, clock_type::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

[[nodiscard]] int checked_requested_thread_count(std::uint64_t requested_worker_count) {
    if (requested_worker_count == 0) {
        throw std::runtime_error("OpenMP runner requires a positive worker count");
    }
    if (requested_worker_count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("requested worker count exceeds OpenMP supported range");
    }

    return static_cast<int>(requested_worker_count);
}

[[nodiscard]] int determine_actual_worker_count(const run_config& config) {
    int actual_worker_count = 0;

    if (config.requested_worker_count.has_value()) {
        const int requested_worker_count =
            checked_requested_thread_count(*config.requested_worker_count);
#pragma omp parallel num_threads(requested_worker_count)
        {
#pragma omp single
            actual_worker_count = omp_get_num_threads();
        }
    } else {
#pragma omp parallel
        {
#pragma omp single
            actual_worker_count = omp_get_num_threads();
        }
    }

    if (actual_worker_count <= 0) {
        throw std::runtime_error("OpenMP reported an invalid worker count");
    }

    return actual_worker_count;
}

void merge_local_frequency_maps(local_frequency_map& destination, local_frequency_map& source) {
    destination.reserve(destination.size() + source.size());
    for (const auto& [word, count] : source) {
        const auto [it, inserted] = destination.try_emplace(word, 0);
        it->second = detail::checked_add(it->second, count);
        static_cast<void>(inserted);
    }

    source.clear();
}

} // namespace

run_summary run_openmp(const run_config& config) {
    if (config.input_path.empty()) {
        throw std::runtime_error("OpenMP runner requires an input path");
    }

    benchmark_report report;
    report.method = execution_method::openmp;

    const auto total_start = clock_type::now();
    const std::string input_path_text = config.input_path.string();

    const auto read_start = clock_type::now();
    const std::string text = read_text_file(config.input_path);
    const auto read_end = clock_type::now();

    const file_size_type input_size_bytes =
        detail::checked_input_size_bytes(text.size(), input_path_text);

    const int actual_worker_count = determine_actual_worker_count(config);
    const auto ranges = internal::build_even_byte_ranges(
        input_size_bytes, static_cast<std::size_t>(actual_worker_count));

    const auto tokenize_count_start = clock_type::now();
    std::vector<local_frequency_map> local_frequencies(
        static_cast<std::size_t>(actual_worker_count));
    std::vector<count_type> local_word_totals(static_cast<std::size_t>(actual_worker_count), 0);

#pragma omp parallel num_threads(actual_worker_count)
    {
        const std::size_t worker_index = static_cast<std::size_t>(omp_get_thread_num());
        const internal::byte_range range = ranges[worker_index];
        const bool starts_inside_word =
            range.begin > 0 && detail::is_ascii_alphanumeric(static_cast<unsigned char>(
                                   text[static_cast<std::size_t>(range.begin - 1)]));

        internal::for_each_owned_word(text, range, starts_inside_word, [&](word_type word) {
            const auto [it, inserted] =
                local_frequencies[worker_index].try_emplace(std::move(word), 0);
            it->second = detail::checked_add(it->second, 1);
            local_word_totals[worker_index] =
                detail::checked_add(local_word_totals[worker_index], 1);
            static_cast<void>(inserted);
        });
    }

    count_type total_word_count = 0;
    for (const count_type local_word_total : local_word_totals) {
        total_word_count = detail::checked_add(total_word_count, local_word_total);
    }
    const auto tokenize_count_end = clock_type::now();

    const auto merge_start = clock_type::now();
    for (std::size_t stride = 1; stride < local_frequencies.size(); stride *= 2) {
        const std::size_t pair_count = (local_frequencies.size() + (2 * stride) - 1) / (2 * stride);

#pragma omp parallel for num_threads(actual_worker_count) schedule(static)
        for (std::ptrdiff_t pair_index = 0; pair_index < static_cast<std::ptrdiff_t>(pair_count);
             ++pair_index) {
            const std::size_t destination_index = static_cast<std::size_t>(pair_index) * 2 * stride;
            const std::size_t source_index = destination_index + stride;
            if (source_index < local_frequencies.size()) {
                merge_local_frequency_maps(local_frequencies[destination_index],
                                           local_frequencies[source_index]);
            }
        }
    }
    const auto merge_end = clock_type::now();

    const auto canonicalize_start = clock_type::now();
    frequency_map frequencies = internal::canonicalize_frequency_map(local_frequencies.front());
    const auto canonicalize_end = clock_type::now();

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
        report.worker_count = static_cast<std::uint64_t>(actual_worker_count);
        report.input_size_bytes = summary.result.input_size_bytes;
        report.word_count = summary.result.total_word_count;
        report.unique_word_count = summary.result.unique_word_count;
        report.total_seconds = elapsed_seconds(total_start, total_end);
        report.phases.push_back(
            {"read", elapsed_seconds(read_start, read_end), phase_scope::local});
        report.phases.push_back({"parallel_tokenize_count",
                                 elapsed_seconds(tokenize_count_start, tokenize_count_end),
                                 phase_scope::local});
        report.phases.push_back(
            {"tree_merge", elapsed_seconds(merge_start, merge_end), phase_scope::local});
        report.phases.push_back({"canonicalize",
                                 elapsed_seconds(canonicalize_start, canonicalize_end),
                                 phase_scope::local});
        if (write_duration_seconds.has_value()) {
            report.phases.push_back({"write", *write_duration_seconds, phase_scope::local});
        }
        report.phases.push_back({"total", report.total_seconds, phase_scope::local});
        summary.benchmark = std::move(report);
    }

    return summary;
}

} // namespace wf
#endif
