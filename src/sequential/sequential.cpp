#include <stdexcept>

#include <wf/runners.h>

namespace wf {

run_summary run_sequential(const run_config&) {
    throw std::runtime_error("sequential runner is not implemented yet");
}

} // namespace wf
