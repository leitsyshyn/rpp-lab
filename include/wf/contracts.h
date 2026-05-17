#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wf {

enum class execution_method {
    sequential,
    openmp,
    mpi,
};

using word_type = std::string;
using count_type = std::uint64_t;
using file_size_type = std::uint64_t;
using frequency_map = std::map<word_type, count_type>;

[[nodiscard]] constexpr std::string_view to_string(execution_method method) noexcept {
    switch (method) {
    case execution_method::sequential:
        return "sequential";
    case execution_method::openmp:
        return "openmp";
    case execution_method::mpi:
        return "mpi";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::optional<execution_method>
parse_execution_method(std::string_view value) noexcept {
    if (value == "sequential") {
        return execution_method::sequential;
    }
    if (value == "openmp") {
        return execution_method::openmp;
    }
    if (value == "mpi") {
        return execution_method::mpi;
    }

    return std::nullopt;
}

struct run_config {
    execution_method selected_method{execution_method::sequential};
    std::filesystem::path input_path;
    std::optional<std::filesystem::path> output_path;
    bool output_enabled{false};
    bool benchmark_enabled{false};
    std::optional<std::uint64_t> requested_worker_count;
};

struct word_frequency_result {
    // MPI runners may only populate frequencies on rank 0.
    std::optional<frequency_map> frequencies;
    count_type total_word_count{0};
    count_type unique_word_count{0};
    std::optional<file_size_type> input_size_bytes;
};

enum class phase_scope {
    local,
    root_only,
    distributed,
};

struct benchmark_phase {
    std::string name;
    double duration_seconds{0.0};
    phase_scope scope{phase_scope::local};
};

struct benchmark_report {
    execution_method method{execution_method::sequential};
    std::uint64_t worker_count{1};
    std::optional<file_size_type> input_size_bytes;
    count_type word_count{0};
    count_type unique_word_count{0};
    double total_seconds{0.0};
    std::vector<benchmark_phase> phases;
};

struct run_summary {
    word_frequency_result result;
    std::optional<benchmark_report> benchmark;
};

} // namespace wf
