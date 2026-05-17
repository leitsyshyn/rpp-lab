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

# OpenMP with 4 workers
./build/wf-benchmark --mode openmp --workers 4 input.txt

# MPI
mpirun -np 4 ./build/wf-benchmark --mode mpi input.txt

# MPI to a file
mpirun -np 4 ./build/wf-benchmark --mode mpi input.txt --output output.txt
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
src/openmp/      – OpenMP shared-memory implementation
src/mpi/         – MPI distributed-memory implementation
tests/           – Google Test unit tests
scripts/         – benchmark / validation (future)
benchmarks/      – output directory (git ignored)
cmake/           – custom CMake modules (future)
```

All three modes are compiled into a single `wf-benchmark` executable and
selected at runtime with `--mode <name>`. OpenMP and MPI code paths are only
compiled when the respective dependency is detected.

**Shared interface layer** (`include/wf/`, target `wf_core`):
The public contracts in `wf/contracts.h`, primitive declarations in
`wf/primitives.h`, and runner declarations in `wf/runners.h` define the shared
semantic surface for the whole project.

Method-specific implementations must not redefine what a word is, how words are
normalised, how counts are merged, how outputs are formatted, or how benchmark
reports are represented. Those semantics belong to the shared primitive layer.

The sequential implementation is the correctness reference.
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
- MPI uses it for byte-buffer exchange
- malformed serialized input is rejected during deserialization

## Status

This repository implements the shared primitive layer plus sequential, OpenMP,
and MPI runners.

The OpenMP mode reads the input once, splits the text into byte ranges across
workers, performs boundary-safe shared-memory chunk scanning, counts into
thread-local hash maps, merges them with a tree reduction, and canonicalizes
the final result into the deterministic frequency map used by the sequential
reference.

The MPI mode uses parallel MPI file I/O, boundary-safe local tokenization,
stable-hash partitioned aggregation, `MPI_Alltoallv` bucket exchange, owner-side
merging, and a final rank-0 gather for deterministic output. The sequential
mode remains the correctness reference.

### Known limitation

- final output is centralized on rank 0

Future agents must not reintroduce AppleClang-specific OpenMP workaround
logic. Always use Homebrew LLVM for a clean OpenMP experience.
