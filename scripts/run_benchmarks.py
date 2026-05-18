#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmark_common import (
    build_mpi_command,
    discover_input_files,
    discover_mpi_launcher,
    ensure_directory,
    extract_single_benchmark_report,
    infer_input_metadata,
    repo_root,
    run_command,
    serialize_report,
    write_csv_rows,
    write_json,
)


DEFAULT_METHODS = ("sequential", "openmp", "mpi")
DEFAULT_OPENMP_WORKERS = (1, 2, 4, 8, 10)
DEFAULT_MPI_PROCESSES = (1, 2, 4, 8, 10)
DRY_RUN_PROFILES = {"natural", "highcard"}
DRY_RUN_SIZE_MB = 10


def parse_count_list(value: str) -> list[int]:
    items = [item.strip() for item in value.replace(",", " ").split() if item.strip()]
    if not items:
        raise argparse.ArgumentTypeError("expected at least one positive integer")
    counts = sorted({int(item) for item in items})
    if any(count <= 0 for count in counts):
        raise argparse.ArgumentTypeError("counts must be positive integers")
    return counts


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run benchmark matrices and capture machine-readable results")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("--input-dir", action="append", type=Path, default=[])
    parser.add_argument("--methods", nargs="+", choices=DEFAULT_METHODS, default=list(DEFAULT_METHODS))
    parser.add_argument("--openmp-workers", type=parse_count_list)
    parser.add_argument("--mpi-processes", type=parse_count_list)
    parser.add_argument("--runs", type=int)
    parser.add_argument("--warmup", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root() / "benchmarks" / "results" / "raw.csv",
    )
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--mpiexec")
    parser.add_argument("--mpiexec-numproc-flag")
    parser.add_argument("--continue-on-failure", action="store_true")
    parser.add_argument("--skip-warmup-records", action="store_true")
    return parser


def select_inputs(paths: list[Path], directories: list[Path], dry_run: bool) -> list[Path]:
    discovered = discover_input_files(paths, directories)
    if not dry_run:
        return discovered

    filtered: list[Path] = []
    for path in discovered:
        metadata = infer_input_metadata(path)
        if metadata.profile in DRY_RUN_PROFILES and metadata.size_mb == DRY_RUN_SIZE_MB:
            filtered.append(path)
    return filtered


def compute_defaults(args: argparse.Namespace) -> tuple[list[int], list[int], int, int]:
    if args.dry_run:
        openmp_workers = args.openmp_workers or [1, 4]
        mpi_processes = args.mpi_processes or [1, 4]
        runs = args.runs if args.runs is not None else 2
        warmup = args.warmup if args.warmup is not None else 1
        return openmp_workers, mpi_processes, runs, warmup

    openmp_workers = args.openmp_workers or list(DEFAULT_OPENMP_WORKERS)
    mpi_processes = args.mpi_processes or list(DEFAULT_MPI_PROCESSES)
    runs = args.runs if args.runs is not None else 5
    warmup = args.warmup if args.warmup is not None else 1
    return openmp_workers, mpi_processes, runs, warmup


def build_method_commands(
    binary: Path,
    input_path: Path,
    methods: list[str],
    openmp_workers: list[int],
    mpi_processes: list[int],
    mpi_launcher: str,
    mpi_numproc_flag: str,
) -> list[tuple[str, int, list[str]]]:
    commands: list[tuple[str, int, list[str]]] = []
    for method in methods:
        if method == "sequential":
            commands.append(
                (
                    method,
                    1,
                    [str(binary), "--mode", "sequential", str(input_path), "--no-output", "--benchmark"],
                )
            )
            continue
        if method == "openmp":
            for workers in openmp_workers:
                commands.append(
                    (
                        method,
                        workers,
                        [
                            str(binary),
                            "--mode",
                            "openmp",
                            "--workers",
                            str(workers),
                            str(input_path),
                            "--no-output",
                            "--benchmark",
                        ],
                    )
                )
            continue
        if method == "mpi":
            for process_count in mpi_processes:
                commands.append(
                    (
                        method,
                        process_count,
                        build_mpi_command(
                            mpi_launcher,
                            mpi_numproc_flag,
                            process_count,
                            binary,
                            ["--mode", "mpi", str(input_path), "--no-output", "--benchmark"],
                        ),
                    )
                )
            continue
        raise ValueError(f"unsupported method: {method}")
    return commands


def make_failure_row(
    timestamp: str,
    metadata: Any,
    method: str,
    requested_workers: int,
    run_index: int,
    is_warmup: bool,
    command_result: Any,
    status: str,
) -> dict[str, Any]:
    return {
        "timestamp": timestamp,
        "input_file": str(metadata.path),
        "input_name": metadata.input_name,
        "profile": metadata.profile,
        "size_mb": metadata.size_mb or "",
        "input_bytes": metadata.input_bytes,
        "method": method,
        "workers": requested_workers,
        "requested_workers": requested_workers,
        "run_index": run_index,
        "is_warmup": str(is_warmup).lower(),
        "phase": "",
        "phase_scope": "",
        "phase_seconds": "",
        "total_seconds": "",
        "word_count": "",
        "unique_word_count": "",
        "exit_code": command_result.returncode,
        "success": "false",
        "status": status,
        "wall_seconds": f"{command_result.wall_seconds:.6f}",
    }


def make_phase_rows(
    timestamp: str,
    metadata: Any,
    report: Any,
    requested_workers: int,
    run_index: int,
    is_warmup: bool,
    command_result: Any,
) -> list[dict[str, Any]]:
    rows = []
    for phase in report.phases:
        rows.append(
            {
                "timestamp": timestamp,
                "input_file": str(metadata.path),
                "input_name": metadata.input_name,
                "profile": metadata.profile,
                "size_mb": metadata.size_mb or "",
                "input_bytes": report.input_size_bytes or metadata.input_bytes,
                "method": report.method,
                "workers": report.worker_count,
                "requested_workers": requested_workers,
                "run_index": run_index,
                "is_warmup": str(is_warmup).lower(),
                "phase": phase.name,
                "phase_scope": phase.scope,
                "phase_seconds": f"{phase.seconds:.6f}",
                "total_seconds": f"{report.total_seconds:.6f}",
                "word_count": report.word_count,
                "unique_word_count": report.unique_word_count,
                "exit_code": command_result.returncode,
                "success": "true",
                "status": "ok",
                "wall_seconds": f"{command_result.wall_seconds:.6f}",
            }
        )
    return rows


def main() -> int:
    args = build_argument_parser().parse_args()
    binary = args.binary.resolve()
    output_path = args.output if args.output.is_absolute() else repo_root() / args.output
    json_output_path = None
    if args.json_output is not None:
        json_output_path = args.json_output if args.json_output.is_absolute() else repo_root() / args.json_output

    input_paths = select_inputs(args.input, args.input_dir, args.dry_run)
    if not input_paths:
        raise SystemExit("no input files selected; pass --input/--input-dir or generate a matching suite first")

    openmp_workers, mpi_processes, runs, warmup = compute_defaults(args)
    mpi_launcher, mpi_numproc_flag = discover_mpi_launcher(
        args.build_dir.resolve() if args.build_dir is not None else None,
        args.mpiexec,
        args.mpiexec_numproc_flag,
    )

    fieldnames = [
        "timestamp",
        "input_file",
        "input_name",
        "profile",
        "size_mb",
        "input_bytes",
        "method",
        "workers",
        "requested_workers",
        "run_index",
        "is_warmup",
        "phase",
        "phase_scope",
        "phase_seconds",
        "total_seconds",
        "word_count",
        "unique_word_count",
        "exit_code",
        "success",
        "status",
        "wall_seconds",
    ]

    rows: list[dict[str, Any]] = []
    raw_records: list[dict[str, Any]] = []
    halt = False
    for input_path in input_paths:
        metadata = infer_input_metadata(input_path)
        commands = build_method_commands(
            binary,
            input_path,
            args.methods,
            openmp_workers,
            mpi_processes,
            mpi_launcher,
            mpi_numproc_flag,
        )
        for method, requested_workers, command in commands:
            total_runs = warmup + runs
            for execution_index in range(total_runs):
                is_warmup = execution_index < warmup
                run_index = execution_index + 1
                timestamp = datetime.now(timezone.utc).isoformat()
                result = run_command(command)
                record: dict[str, Any] = {
                    "timestamp": timestamp,
                    "input_file": str(metadata.path),
                    "input_name": metadata.input_name,
                    "profile": metadata.profile,
                    "size_mb": metadata.size_mb,
                    "input_bytes": metadata.input_bytes,
                    "requested_method": method,
                    "requested_workers": requested_workers,
                    "run_index": run_index,
                    "is_warmup": is_warmup,
                    "command": command,
                    "exit_code": result.returncode,
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                    "wall_seconds": result.wall_seconds,
                }

                if result.returncode != 0:
                    record["status"] = "command_failed"
                    if not (is_warmup and args.skip_warmup_records):
                        rows.append(
                            make_failure_row(
                                timestamp,
                                metadata,
                                method,
                                requested_workers,
                                run_index,
                                is_warmup,
                                result,
                                "command_failed",
                            )
                        )
                    raw_records.append(record)
                    print(
                        f"FAIL {metadata.input_name} method={method} workers={requested_workers} run={run_index} exit={result.returncode}"
                    )
                    if not args.continue_on_failure:
                        halt = True
                    if halt:
                        break
                    continue

                try:
                    report = extract_single_benchmark_report(result.stderr)
                except Exception as error:  # noqa: BLE001
                    record["status"] = "parse_failed"
                    record["parse_error"] = str(error)
                    if not (is_warmup and args.skip_warmup_records):
                        rows.append(
                            make_failure_row(
                                timestamp,
                                metadata,
                                method,
                                requested_workers,
                                run_index,
                                is_warmup,
                                result,
                                "parse_failed",
                            )
                        )
                    raw_records.append(record)
                    print(
                        f"FAIL {metadata.input_name} method={method} workers={requested_workers} run={run_index} parse_error"
                    )
                    if not args.continue_on_failure:
                        halt = True
                    if halt:
                        break
                    continue

                record["status"] = "ok"
                record["report"] = serialize_report(report)
                raw_records.append(record)
                if not (is_warmup and args.skip_warmup_records):
                    rows.extend(
                        make_phase_rows(
                            timestamp,
                            metadata,
                            report,
                            requested_workers,
                            run_index,
                            is_warmup,
                            result,
                        )
                    )
                print(
                    f"OK {metadata.input_name} method={report.method} workers={report.worker_count} run={run_index} total={report.total_seconds:.6f}s"
                )
            if halt:
                break
        if halt:
            break

    write_csv_rows(output_path, fieldnames, rows)
    print(f"wrote raw CSV {output_path}")

    if json_output_path is not None:
        write_json(json_output_path, {"runs": raw_records})
        print(f"wrote raw JSON {json_output_path}")

    if halt:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
