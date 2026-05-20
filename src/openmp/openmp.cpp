#include <omp.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wf/utils.h>
#include <wf/runners.h>

namespace wf {

namespace {

[[nodiscard]] int get_worker_count(const run_config& config) {
    int worker_count = 0;

    if (config.requested_worker_count.has_value()) {
        const int requested_worker_count = *config.requested_worker_count;
        if (requested_worker_count <= 0) {
            throw std::runtime_error("OpenMP runner requires a positive worker count");
        }

#pragma omp parallel num_threads(requested_worker_count)
        {
#pragma omp single
            worker_count = omp_get_num_threads();
        }
    } else {
#pragma omp parallel
        {
#pragma omp single
            worker_count = omp_get_num_threads();
        }
    }

    if (worker_count <= 0) {
        throw std::runtime_error("OpenMP reported an invalid worker count");
    }

    return worker_count;
}

void merge_frequency_maps(unordered_frequency_map& destination,
                          unordered_frequency_map& source) {
    destination.reserve(destination.size() + source.size());
    for (const auto& [word, count] : source) {
        const auto [it, inserted] = destination.try_emplace(word, 0);
        it->second += count;
        static_cast<void>(inserted);
    }
    source.clear();
}

} // namespace

run_result run_openmp(const run_config& config) {
    if (config.input_path.empty()) {
        throw std::runtime_error("OpenMP runner requires an input path");
    }

    const auto total_start = clock::now();

    const auto read_start = clock::now();
    const std::string text = read_file(config.input_path);
    const auto read_end = clock::now();
    const std::size_t text_size = text.size();

    const int worker_count = get_worker_count(config);

    const auto partition_start = clock::now();
    const auto ranges = build_even_ranges(text_size, worker_count);
    const auto partition_end = clock::now();

    const auto count_start = clock::now();
    std::vector<unordered_frequency_map> local_frequencies((worker_count));
    std::vector<std::size_t> local_word_counts((worker_count), 0);

#pragma omp parallel num_threads(worker_count)
    {
        const std::size_t worker_index = omp_get_thread_num();
        const range range = ranges[worker_index];

        for_each_word(text, range, [&](std::string word) {
            count_word(local_frequencies[worker_index], local_word_counts[worker_index],
                       std::move(word));
        });
    }

    std::size_t total_word_count = 0;
    for (const std::size_t local_word_count : local_word_counts) {
        total_word_count += local_word_count;
    }
    const auto count_end = clock::now();

    const auto merge_start = clock::now();
    for (std::size_t stride = 1; stride < local_frequencies.size(); stride *= 2) {
        const std::size_t pair_count = (local_frequencies.size() + (2 * stride) - 1) / (2 * stride);

#pragma omp parallel for num_threads(worker_count) schedule(static)
        for (std::ptrdiff_t pair_index = 0; pair_index < static_cast<std::ptrdiff_t>(pair_count);
             ++pair_index) {
            const std::size_t destination_index = static_cast<std::size_t>(pair_index) * 2 * stride;
            const std::size_t source_index = destination_index + stride;
            if (source_index < local_frequencies.size()) {
                merge_frequency_maps(local_frequencies[destination_index],
                                     local_frequencies[source_index]);
            }
        }
    }
    const auto merge_end = clock::now();

    const auto finalize_start = clock::now();
    frequency_map frequencies(local_frequencies.front().begin(),
                              local_frequencies.front().end());
    const auto finalize_end = clock::now();

    const auto write_start = clock::now();
    if (config.output_enabled) {
        if (config.output_path.has_value()) {
            write_frequency_map(*config.output_path, frequencies);
        } else {
            write_frequency_map(std::cout, frequencies);
        }
    }
    const auto write_end = clock::now();

    const auto total_end = clock::now();

    run_result result;
    result.frequencies = std::move(frequencies);
    result.total_word_count = total_word_count;
    result.unique_word_count = result.frequencies->size();
    result.text_size = text_size;

    if (config.benchmark_enabled) {
        benchmark_data benchmark;
        benchmark.worker_count = worker_count;
        benchmark.total_duration = duration(total_start, total_end);
        benchmark.phases.push_back({"read", duration(read_start, read_end)});
        benchmark.phases.push_back({"partition", duration(partition_start, partition_end)});
        benchmark.phases.push_back({"count", duration(count_start, count_end)});
        benchmark.phases.push_back({"merge", duration(merge_start, merge_end)});
        benchmark.phases.push_back({"finalize", duration(finalize_start, finalize_end)});
        benchmark.phases.push_back({"write", duration(write_start, write_end)});
        result.benchmark = std::move(benchmark);
    }

    return result;
}

} // namespace wf
