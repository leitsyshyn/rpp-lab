#ifdef WF_HAS_OPENMP
#include <stdexcept>

#include <wf/runners.h>

namespace wf {

run_summary run_openmp(const run_config&) {
    throw std::runtime_error("OpenMP runner is not implemented yet");
}

} // namespace wf
#endif
