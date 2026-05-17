#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

#include <wf/contracts.h>
#include <wf/runners.h>
#include <wf/version.h>

int main(int argc, char** argv) {
    const char* mode = "sequential";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
            break;
        }
    }

    const auto selected_method = wf::parse_execution_method(mode);
    if (!selected_method.has_value()) {
        std::fprintf(stderr, "Error: unknown mode '%s'\n", mode);
        return 1;
    }

    wf::run_config config;
    config.selected_method = *selected_method;

    try {
        switch (*selected_method) {
        case wf::execution_method::sequential:
            static_cast<void>(wf::run_sequential(config));
            return 0;
        case wf::execution_method::openmp:
#ifdef WF_HAS_OPENMP
            static_cast<void>(wf::run_openmp(config));
            return 0;
#else
            throw std::runtime_error("OpenMP support is not available in this build");
#endif
        case wf::execution_method::mpi:
#ifdef WF_HAS_MPI
            static_cast<void>(wf::run_mpi(config));
            return 0;
#else
            throw std::runtime_error("MPI support is not available in this build");
#endif
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }

    return 1;
}
