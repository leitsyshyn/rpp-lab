#ifdef WF_HAS_MPI
#include <stdexcept>

#include <wf/runners.h>

namespace wf {

run_summary run_mpi(const run_config&) {
    throw std::runtime_error("MPI runner is not implemented yet");
}

} // namespace wf
#endif
