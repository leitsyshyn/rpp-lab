#pragma once

#include <wf/contracts.h>

namespace wf {

[[nodiscard]] run_result run_sequential(const run_config& config);

[[nodiscard]] run_result run_openmp(const run_config& config);

[[nodiscard]] run_result run_mpi(const run_config& config);

} // namespace wf
