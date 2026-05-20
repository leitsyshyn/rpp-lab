#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wf {

enum class method {
    sequential,
    openmp,
    mpi,
};

using frequency_map = std::map<std::string, std::size_t>;

[[nodiscard]] constexpr std::string_view to_string(method method) noexcept {
    switch (method) {
    case method::sequential:
        return "sequential";
    case method::openmp:
        return "openmp";
    case method::mpi:
        return "mpi";
    }

    return "unknown";
}

[[nodiscard]] constexpr std::optional<method>
parse_execution_method(std::string_view value) noexcept {
    if (value == "sequential") {
        return method::sequential;
    }
    if (value == "openmp") {
        return method::openmp;
    }
    if (value == "mpi") {
        return method::mpi;
    }

    return std::nullopt;
}

struct run_config {
    method selected_method{method::sequential};
    std::filesystem::path input_path;

    // Controls how the output frequency table is delivered.
    //
    //   output_enabled == false  => compute only; output_path is ignored
    //   output_enabled == true   => write to output_path if set, else stdout
    bool output_enabled{false};
    std::optional<std::filesystem::path> output_path;

    bool benchmark_enabled{false};

    // Requested logical worker / thread / process count where the selected
    // method can honour it.  OpenMP implementations may use this to control
    // thread count.  Sequential implementations may ignore it or treat it as
    // 1.  MPI process count is typically set by the launcher (mpirun -np);
    // this field is then informational or for validation / reporting.
    std::optional<int> requested_worker_count;
};

struct benchmark_phase {
    std::string name;
    double duration{0.0};
};

struct benchmark_data {
    int worker_count{1};
    double total_duration{0.0};
    std::vector<benchmark_phase> phases;
};

struct run_result {
    // The materialised final frequency map when the current execution context
    // owns / collects the final result.
    //
    //   has_value  =>  this context holds the full map
    //   nullopt    =>  this context did not materialise the full map
    //
    // Sequential and OpenMP implementations should normally return populated
    // frequencies. Distributed implementations (MPI) may return nullopt on
    // non-collecting ranks or contexts.
    std::optional<frequency_map> frequencies;
    std::size_t total_word_count{0};
    std::size_t unique_word_count{0};
    std::size_t text_size{0};
    std::optional<benchmark_data> benchmark;
};

} // namespace wf
