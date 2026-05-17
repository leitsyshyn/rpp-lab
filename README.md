# wf-benchmark

Benchmarking word-frequency counting across sequential, OpenMP, and MPI implementations.

## Prerequisites

```bash
brew install cmake ninja llvm libomp open-mpi clang-format
```

| Package       | Purpose                     |
|---------------|-----------------------------|
| `cmake`       | Build system (>= 3.20)      |
| `ninja`       | Fast build tool (optional)  |
| `llvm`        | Compiler toolchain (clang++, clang-format, LLD) |
| `libomp`      | OpenMP runtime              |
| `open-mpi`    | MPI implementation          |
| `clang-format`| Code formatter              |

## Why Homebrew LLVM?

AppleClang does not ship with native `-fopenmp` support. Using it for OpenMP
requires the `-Xpreprocessor` workaround plus manual libomp header/library
paths. Homebrew LLVM provides a standard compiler with clean `-fopenmp`
support, keeping the CMake setup simple and portable.

If you configure with AppleClang and OpenMP is not found, CMake will print a
clear message telling you to reconfigure with Homebrew LLVM.

## Configure

**Canonical macOS command:**

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm);$(brew --prefix libomp);$(brew --prefix open-mpi)"
```

To disable optional features:

```bash
cmake -B build -DWF_BUILD_OPENMP=OFF -DWF_BUILD_MPI=OFF -DWF_BUILD_TESTS=OFF
```

## Build

```bash
cmake --build build
```

## Run

```bash
# Sequential to stdout
./build/wf-benchmark input.txt

# Sequential to a file
./build/wf-benchmark input.txt --output output.txt

# Sequential benchmark without writing frequencies
./build/wf-benchmark input.txt --benchmark --no-output

# OpenMP
./build/wf-benchmark --mode openmp input.txt

# MPI
mpirun -np 4 ./build/wf-benchmark --mode mpi input.txt
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Format

```bash
cmake --build build -t format       # apply
cmake --build build -t check-format # dry-run
```

Requires `clang-format` on `PATH` or reachable via `CMAKE_PREFIX_PATH`.

## Project structure

```
include/wf/      – public contracts, primitive declarations, runner APIs
src/core/        – shared core library (compiled, not header-only)
src/sequential/  – sequential reference implementation
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

**Shared interface layer** (`include/wf/`, target `wf_core`):
The public contracts in `wf/contracts.h`, primitive declarations in
`wf/primitives.h`, and runner declarations in `wf/runners.h` define the shared
semantic surface for the whole project.

Method-specific implementations must not redefine what a word is, how words are
normalised, how counts are merged, how outputs are formatted, or how benchmark
reports are represented. Those semantics belong to the shared primitive layer.

The sequential implementation will become the correctness reference.
OpenMP and MPI implementations must use the same shared contracts so their
outputs and benchmark reports remain comparable.

Canonical word semantics:

- a word is a maximal contiguous sequence of ASCII letters or digits
- normalization lowercases ASCII letters
- any non-alphanumeric byte is a delimiter

Frequency-map serialization contract:

- deterministic `std::map` order is preserved in the serialized byte stream
- one record is emitted per frequency entry as `word count\n`
- each record stores the normalized word and its count
- the format is designed for tokenizer-produced ASCII-normalized words
- future MPI implementations will use it for byte-buffer exchange
- malformed serialized input is rejected during deserialization

## Status

This repository now implements the shared primitive layer and the sequential
reference runner. The sequential method defines the canonical output semantics
for future OpenMP and MPI implementations.

### Not implemented yet

- MPI file I/O
- OpenMP parallel loops
- Distributed aggregation

Future agents must not reintroduce AppleClang-specific OpenMP workaround
logic. Always use Homebrew LLVM for a clean OpenMP experience.
