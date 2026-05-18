#pragma once

#include <wf/contracts.h>

namespace wf {

[[nodiscard]] run_summary run_sequential(const run_config& config);

[[nodiscard]] run_summary run_sequential_2(const run_config& config);

[[nodiscard]] run_summary run_openmp(const run_config& config);

[[nodiscard]] run_summary run_mpi(const run_config& config);

} // namespace wf
