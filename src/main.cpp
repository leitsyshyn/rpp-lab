#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <wf/contracts.h>
#include <wf/utils.h>
#include <wf/runners.h>

namespace {

int parse_worker_count_argument(std::string_view value) {
    int worker_count = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, error_code] = std::from_chars(begin, end, worker_count);
    if (error_code != std::errc{} || ptr != end || worker_count <= 0) {
        throw std::runtime_error("--workers requires a positive integer");
    }

    return worker_count;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const char* mode = "sequential";
        wf::run_config config;
        config.output_enabled = true;

        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--mode") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--mode requires a value");
                }
                mode = argv[++i];
                continue;
            }
            if (argument == "--output") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--output requires a path");
                }
                config.output_enabled = true;
                config.output_path = argv[++i];
                continue;
            }
            if (argument == "--no-output") {
                config.output_enabled = false;
                config.output_path.reset();
                continue;
            }
            if (argument == "--benchmark") {
                config.benchmark_enabled = true;
                continue;
            }
            if (argument == "--workers") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--workers requires a value");
                }

                const auto worker_count = parse_worker_count_argument(argv[++i]);
                config.requested_worker_count = worker_count;
                continue;
            }
            if (!argument.empty() && argument.front() == '-') {
                throw std::runtime_error("unknown argument: " + std::string(argument));
            }
            if (!config.input_path.empty()) {
                throw std::runtime_error("multiple input paths were provided");
            }

            config.input_path = argv[i];
        }

        const auto selected_method = wf::parse_execution_method(mode);
        if (!selected_method.has_value()) {
            throw std::runtime_error("unknown mode: " + std::string(mode));
        }
        if (config.input_path.empty()) {
            throw std::runtime_error("input path is required");
        }

        config.selected_method = *selected_method;

        wf::run_result result;
        switch (*selected_method) {
        case wf::method::sequential:
            result = wf::run_sequential(config);
            break;
        case wf::method::openmp:
#ifdef WF_HAS_OPENMP
            result = wf::run_openmp(config);
            break;
#else
            throw std::runtime_error("OpenMP support is not available in this build");
#endif
        case wf::method::mpi:
#ifdef WF_HAS_MPI
            result = wf::run_mpi(config);
            break;
#else
            throw std::runtime_error("MPI support is not available in this build");
#endif
        }

        if (result.benchmark.has_value()) {
            wf::print_benchmark_report(std::cerr, config.selected_method, result);
        }

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }

    return 1;
}
