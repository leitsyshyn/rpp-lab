# wf-benchmark

Benchmarking word-frequency counting across sequential, OpenMP, and MPI implementations.

## Prerequisites

| Tool      | Version        | Install (macOS)                        |
|-----------|----------------|----------------------------------------|
| CMake     | >= 3.20        | `brew install cmake`                   |
| C++       | C++20          | Xcode CLI Tools (`xcode-select --install`) |
| Ninja     | (optional)     | `brew install ninja`                   |
| OpenMP    | (optional)     | `brew install libomp`                  |
| MPI       | (optional)     | `brew install open-mpi`                |
| clang-format | (optional) | `brew install clang-format`            |

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To disable optional features:

```bash
cmake -B build -DWF_BUILD_OPENMP=OFF -DWF_BUILD_MPI=OFF -DWF_BUILD_TESTS=OFF
```

## Run

```bash
# Sequential (default)
./build/wf-benchmark

# OpenMP
./build/wf-benchmark --mode openmp

# MPI
mpirun -np 4 ./build/wf-benchmark --mode mpi
```

## Test

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## Format

```bash
cmake --build build -t format       # apply
cmake --build build -t check-format # dry-run
```

Requires `clang-format` on `PATH`.

## Project structure

```
include/wf/      – public interfaces / version header
src/sequential/  – sequential implementation (future)
src/openmp/      – OpenMP implementation (future)
src/mpi/         – MPI implementation (future)
tests/           – Google Test unit tests
scripts/         – benchmark / validation (future)
benchmarks/      – output directory (git ignored)
cmake/           – custom CMake modules (future)
```

All three modes are compiled into a single `wf-benchmark` executable and
selected at runtime with `--mode <name>`. OpenMP and MPI stubs are only
compiled when the respective dependency is detected.

## Status

This repository contains **only tooling bootstrap**. No word-frequency
algorithms have been implemented.

### Not implemented yet

- Word extraction / tokenisation
- Frequency counting
- Serialisation
- MPI file I/O
- OpenMP parallel loops
- Benchmark harness
- Correctness comparison

## macOS notes

- **OpenMP**: Apple Clang does not support `-fopenmp`. CMake 3.23+ handles
  the `-Xpreprocessor -fopenmp` flag automatically when `libomp` is
  installed via Homebrew. If CMake fails to detect OpenMP, verify that
  `brew --prefix libomp` exists.
- **MPI**: Open MPI from Homebrew is detected automatically by
  `find_package(MPI)`. The wrapper compilers (`mpic++`, etc.) are not
  needed for CMake builds.
