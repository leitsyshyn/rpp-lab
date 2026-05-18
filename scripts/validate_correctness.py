#!/usr/bin/env python3
from __future__ import annotations

import argparse
import filecmp
import tempfile
from pathlib import Path

from benchmark_common import (
    build_mpi_command,
    describe_output_difference,
    discover_input_files,
    discover_mpi_launcher,
    ensure_directory,
    generate_profile_file,
    run_command,
)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate OpenMP and MPI outputs against sequential")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("--input-dir", action="append", type=Path, default=[])
    parser.add_argument("--openmp-workers", type=int, default=4)
    parser.add_argument("--mpi-processes", type=int, default=4)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--mpiexec")
    parser.add_argument("--mpiexec-numproc-flag")
    parser.add_argument("--keep-temp", action="store_true")
    parser.add_argument("--continue-on-failure", action="store_true")
    return parser


def write_case(path: Path, contents: str) -> Path:
    ensure_directory(path.parent)
    path.write_text(contents, encoding="ascii")
    return path


def prepare_validation_inputs(temp_dir: Path, explicit_inputs: list[Path]) -> list[tuple[str, Path]]:
    cases = [
        ("empty", write_case(temp_dir / "builtin_empty.txt", "")),
        ("delimiters_only", write_case(temp_dir / "builtin_delimiters_only.txt", "... !!! --- ___     \n\t\n")),
        (
            "mixed_case_numeric",
            write_case(temp_dir / "builtin_mixed_case_numeric.txt", "Apple banana apple.\nMPI-2026 mpi\n"),
        ),
        ("smaller_than_workers", write_case(temp_dir / "builtin_smaller_than_workers.txt", "Go!")),
        (
            "boundary_sensitive",
            write_case(
                temp_dir / "builtin_boundary_sensitive.txt",
                "alpha betaGammaDelta epsilon zetaEtaTheta iota\n",
            ),
        ),
        (
            "long_word",
            write_case(
                temp_dir / "builtin_long_word.txt",
                "prefix " + ("A" * 8192) + " suffix " + ("A" * 8192) + "\n",
            ),
        ),
    ]

    lowcard_path = temp_dir / "generated_lowcard_validation.txt"
    highcard_path = temp_dir / "generated_highcard_validation.txt"
    generate_profile_file("lowcard", lowcard_path, 31415, target_size_bytes=256 * 1024)
    generate_profile_file("highcard", highcard_path, 27182, target_size_bytes=256 * 1024)
    cases.append(("generated_lowcard", lowcard_path))
    cases.append(("generated_highcard", highcard_path))

    for path in explicit_inputs:
        cases.append((path.name, path.resolve()))

    return cases


def build_app_command(binary: Path, method: str, input_path: Path, output_path: Path, workers: int | None) -> list[str]:
    command = [str(binary), "--mode", method]
    if workers is not None:
        command.extend(["--workers", str(workers)])
    command.extend([str(input_path), "--output", str(output_path)])
    return command


def run_checked(command: list[str], label: str) -> None:
    result = run_command(command)
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} failed with exit code {result.returncode}\n"
            f"command: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )


def validate_case(
    case_name: str,
    input_path: Path,
    temp_dir: Path,
    binary: Path,
    openmp_workers: int,
    mpi_processes: int,
    mpi_launcher: str,
    mpi_numproc_flag: str,
) -> None:
    sequential_output = temp_dir / f"{case_name}_sequential.txt"
    run_checked(build_app_command(binary, "sequential", input_path, sequential_output, None), f"{case_name} sequential")

    for workers in sorted({1, openmp_workers}):
        openmp_output = temp_dir / f"{case_name}_openmp_{workers}.txt"
        run_checked(
            build_app_command(binary, "openmp", input_path, openmp_output, workers),
            f"{case_name} openmp workers={workers}",
        )
        if not filecmp.cmp(sequential_output, openmp_output, shallow=False):
            raise RuntimeError(
                f"output mismatch for {case_name}: openmp workers={workers}\n"
                f"{describe_output_difference(sequential_output, openmp_output)}"
            )

    for process_count in sorted({1, mpi_processes}):
        mpi_output = temp_dir / f"{case_name}_mpi_{process_count}.txt"
        mpi_command = build_mpi_command(
            mpi_launcher,
            mpi_numproc_flag,
            process_count,
            binary,
            ["--mode", "mpi", str(input_path), "--output", str(mpi_output)],
        )
        run_checked(mpi_command, f"{case_name} mpi processes={process_count}")
        if not filecmp.cmp(sequential_output, mpi_output, shallow=False):
            raise RuntimeError(
                f"output mismatch for {case_name}: mpi processes={process_count}\n"
                f"{describe_output_difference(sequential_output, mpi_output)}"
            )


def main() -> int:
    args = build_argument_parser().parse_args()
    binary = args.binary.resolve()
    explicit_inputs = discover_input_files(args.input, args.input_dir)
    mpi_launcher, mpi_numproc_flag = discover_mpi_launcher(
        args.build_dir.resolve() if args.build_dir is not None else None,
        args.mpiexec,
        args.mpiexec_numproc_flag,
    )

    failures: list[str] = []
    context = tempfile.TemporaryDirectory(prefix="wf_validate_") if not args.keep_temp else None
    temp_root = Path(context.name) if context is not None else (Path("benchmarks") / "results" / "validation_temp")
    ensure_directory(temp_root)

    try:
        cases = prepare_validation_inputs(temp_root, explicit_inputs)
        for case_name, input_path in cases:
            try:
                validate_case(
                    case_name,
                    input_path,
                    temp_root,
                    binary,
                    args.openmp_workers,
                    args.mpi_processes,
                    mpi_launcher,
                    mpi_numproc_flag,
                )
                print(f"PASS {case_name} ({input_path})")
            except Exception as error:  # noqa: BLE001
                message = str(error)
                failures.append(message)
                print(f"FAIL {case_name}: {message}")
                if not args.continue_on_failure:
                    break
    finally:
        if context is not None:
            context.cleanup()

    if failures:
        print(f"validation failed with {len(failures)} failure(s)")
        return 1

    print(f"validated {len(cases)} input case(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
