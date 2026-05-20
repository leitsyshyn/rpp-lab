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

Preferred on macOS: use the checked-in LLVM preset so CMake frontends do not
silently fall back to AppleClang and drop OpenMP support.

```bash
cmake --preset llvm-debug
```

Release build:

```bash
cmake --preset llvm-release
```

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
cmake --build --preset llvm-debug
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

`--benchmark` writes the benchmark report to `stderr`. For performance runs, use
`--no-output --benchmark` so frequency serialization does not dominate the
measurements.

## Test

```bash
ctest --preset llvm-debug
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
scripts/         – benchmark generation / validation / aggregation helpers
data/generated/  – generated benchmark inputs (git ignored)
benchmarks/results/ – raw benchmark CSV/JSON and summaries (git ignored)
cmake/           – custom CMake modules (future)
```

All three modes are compiled into a single `wf-benchmark` executable and
selected at runtime with `--mode <name>`. OpenMP and MPI code paths are only
compiled when the respective dependency is detected. There is no separate
`sequential_2` mode; `sequential` is the canonical scanner-based baseline.

**Shared interface layer** (`include/wf/`, target `wf_core`):
The public contracts in `wf/contracts.h`, primitive declarations in
`wf/primitives.h`, and runner declarations in `wf/runners.h` define the shared
semantic surface for the whole project.

Method-specific implementations must not redefine what a word is, how words are
normalised, how counts are merged, how outputs are formatted, or how benchmark
reports are represented. Those semantics belong to the shared primitive layer.

The sequential implementation is the correctness reference and the fair
single-thread baseline for the OpenMP implementation. OpenMP and MPI
implementations must use the same shared contracts so their outputs and
benchmark reports remain comparable.

Canonical word semantics:

- a word is a maximal contiguous sequence of ASCII letters or digits
- normalization lowercases ASCII letters
- any non-alphanumeric byte is a delimiter

Frequency-map output contract:

- final output is emitted in deterministic `std::map` order
- one record is emitted per frequency entry as `word count\n`
- each record stores the normalized word and its count
- MPI uses an internal packed binary transport and only materializes the final
  sorted map on rank 0

## Status

This repository implements the shared primitive layer plus sequential, OpenMP,
and MPI runners.

The sequential mode reads the input once, scans the full byte range directly,
counts into an internal hash map, and finalizes into the deterministic
sorted frequency map used for output.

The OpenMP mode reads the input once, splits the text into byte ranges across
workers, performs boundary-safe shared-memory chunk scanning, counts into
thread-local hash maps, merges them with a tree reduction, and finalizes
the final result into the same deterministic frequency map as the sequential
baseline.

The MPI mode uses parallel MPI file I/O, boundary-safe local tokenization,
stable-hash partitioned aggregation, `MPI_Alltoallv` bucket exchange, owner-side
merging, and a final rank-0 gather for deterministic output. The sequential
mode remains the correctness reference.

Sequential benchmark phases are:

- `read`
- `count`
- `finalize`
- `write` when output is enabled
- `total`

### Known limitation

- final output is centralized on rank 0

## Benchmark Workflow

Generated benchmark data is not committed by default.

- Generated inputs go under `data/generated/`
- Raw benchmark results and summaries go under `benchmarks/results/`
- Both directories are ignored by Git

### Generate Inputs

Generate a single deterministic file:

```bash
python3 scripts/generate_benchmark_inputs.py \
  --profile natural \
  --size-mb 100 \
  --output data/generated/natural_100mb_seed12345.txt \
  --seed 12345
```

Generate the standard suite:

```bash
python3 scripts/generate_benchmark_inputs.py --suite standard --output-dir data/generated
```

Available profiles:

- `natural`: moderate vocabulary, mixed case, punctuation, numeric tokens, newlines
- `lowcard`: very small vocabulary with heavy repetition
- `highcard`: many unique deterministic words with light repetition
- `boundary`: long words and sparse delimiters for chunk-boundary stress

Suite defaults:

- `smoke`: all four profiles at `10 MB`
- `standard`: all four profiles at `10 MB` and `100 MB`
- add `--include-500mb` to opt into `500 MB` generation

Each generated directory gets a `manifest.json` file so the runner can recover
profile, size, and seed metadata.

### Validate Correctness

Compare OpenMP and MPI outputs against the sequential reference:

```bash
python3 scripts/validate_correctness.py \
  --binary ./build/wf-benchmark \
  --input-dir data/generated \
  --openmp-workers 4 \
  --mpi-processes 4
```

The validation script always covers these edge cases even without generated
files:

- empty input
- delimiters-only input
- mixed case / punctuation / numeric input
- input smaller than worker/process count
- boundary-sensitive input
- long-word input
- generated low-cardinality input
- generated high-cardinality input

MPI validation is launched through `mpiexec` or `mpirun`. The script prefers
the launcher values discovered in `build/CMakeCache.txt` when available.

### Run Benchmarks

Dry-run matrix:

```bash
python3 scripts/run_benchmarks.py \
  --binary ./build/wf-benchmark \
  --input-dir data/generated \
  --dry-run \
  --output benchmarks/results/raw.csv \
  --json-output benchmarks/results/raw.json
```

Dry-run defaults:

- profiles: `natural`, `highcard`
- size: `10 MB`
- OpenMP workers: `1,4`
- MPI processes: `1,4`
- measured runs: `2`
- warmup runs: `1`

Standard benchmark defaults:

- methods: `sequential`, `openmp`, `mpi`
- OpenMP workers: `1,2,4,8,10`
- MPI processes: `1,2,4,8,10`
- measured runs: `5`
- warmup runs: `1`

Recommended local Mac worker/process counts are `1`, `4`, and `8`; use `10`
only if the machine actually has enough logical cores and MPI ranks to make the
comparison meaningful.

### Summarize Results

```bash
python3 scripts/summarize_benchmarks.py \
  --input benchmarks/results/raw.csv \
  --output benchmarks/results/summary.csv
```

This writes:

- `benchmarks/results/summary.csv`: total-time summary per profile/size/method/worker count
- `benchmarks/results/summary_phases.csv`: phase medians and other per-phase stats

Summary metrics:

- median total time
- minimum total time
- mean total time
- standard deviation when at least two measured runs exist
- speedup versus the sequential median for the same input profile and size
- efficiency as `speedup / worker_count`

For sequential runs, speedup and efficiency are both `1.0`.

### Reproducibility Notes

- Use a `Release` build for all benchmark runs
- Use the same `wf-benchmark` binary for sequential, OpenMP, and MPI modes
- Use the same input files for all modes
- Use `--no-output --benchmark` for the main performance runs
- Use repeated runs and treat the median as the primary metric
- File-system cache effects can materially change timings across repeated runs
- On a single Mac, OpenMP may outperform MPI because MPI pays extra launch and communication overhead even in shared-memory environments
- If you want to measure output writing separately, run dedicated `--output ... --benchmark` experiments outside the main benchmark matrix

### End-To-End Example

```bash
python3 scripts/generate_benchmark_inputs.py --suite smoke --output-dir data/generated
python3 scripts/validate_correctness.py --binary ./build/wf-benchmark --input-dir data/generated --openmp-workers 4 --mpi-processes 4
python3 scripts/run_benchmarks.py --binary ./build/wf-benchmark --input-dir data/generated --dry-run --output benchmarks/results/raw.csv --json-output benchmarks/results/raw.json
python3 scripts/summarize_benchmarks.py --input benchmarks/results/raw.csv --output benchmarks/results/summary.csv
```

Future agents must not reintroduce AppleClang-specific OpenMP workaround
logic. Always use Homebrew LLVM for a clean OpenMP experience.
