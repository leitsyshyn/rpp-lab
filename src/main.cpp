#include <cstdio>
#include <cstring>

#include <wf/version.h>

#ifdef WF_HAS_OPENMP
int run_openmp();
#endif

#ifdef WF_HAS_MPI
int run_mpi(int argc, char** argv);
#endif

int run_sequential();

int main(int argc, char** argv) {
    const char* mode = "sequential";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
            break;
        }
    }

    if (std::strcmp(mode, "sequential") == 0) {
        return run_sequential();
    }

#ifdef WF_HAS_OPENMP
    if (std::strcmp(mode, "openmp") == 0) {
        return run_openmp();
    }
#endif

#ifdef WF_HAS_MPI
    if (std::strcmp(mode, "mpi") == 0) {
        return run_mpi(argc, argv);
    }
#endif

    std::fprintf(stderr, "Error: unknown or unavailable mode '%s'\n", mode);
    return 1;
}
