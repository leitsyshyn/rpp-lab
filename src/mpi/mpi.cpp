#ifdef WF_HAS_MPI
#include <cstdio>
#include <mpi.h>

int run_mpi(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (rank == 0) {
        std::printf("mode=mpi: not implemented yet (world size=%d)\n", size);
    }
    MPI_Finalize();
    return 0;
}
#endif
