#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import statistics
from pathlib import Path
from typing import Any

from benchmark_common import bool_from_csv, repo_root, write_csv_rows


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Summarize raw benchmark CSV results")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase-output", type=Path)
    return parser


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", newline="", encoding="utf-8") as input_file:
        return list(csv.DictReader(input_file))


def summarize_series(values: list[float]) -> tuple[float, float, float, float | None]:
    median = statistics.median(values)
    minimum = min(values)
    mean = statistics.fmean(values)
    stddev = statistics.stdev(values) if len(values) >= 2 else None
    return median, minimum, mean, stddev


def group_key(row: dict[str, str]) -> tuple[str, str, str, str]:
    return (row["profile"], row["size_mb"], row["method"], row["workers"])


def build_summaries(raw_rows: list[dict[str, str]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    usable_rows = [
        row
        for row in raw_rows
        if bool_from_csv(row["success"]) and not bool_from_csv(row["is_warmup"]) and row["phase"]
    ]

    total_groups: dict[tuple[str, str, str, str], list[dict[str, str]]] = {}
    phase_groups: dict[tuple[str, str, str, str, str], list[dict[str, str]]] = {}
    for row in usable_rows:
        if row["phase"] == "total":
            total_groups.setdefault(group_key(row), []).append(row)
        phase_groups.setdefault((*group_key(row), row["phase"]), []).append(row)

    sequential_medians: dict[tuple[str, str], float] = {}
    for key, rows in total_groups.items():
        profile, size_mb, method, _workers = key
        if method != "sequential":
            continue
        totals = [float(row["total_seconds"]) for row in rows]
        sequential_medians[(profile, size_mb)] = statistics.median(totals)

    total_summary_rows: list[dict[str, Any]] = []
    for key in sorted(total_groups):
        profile, size_mb, method, workers_text = key
        rows = total_groups[key]
        totals = [float(row["total_seconds"]) for row in rows]
        median_total, min_total, mean_total, stddev_total = summarize_series(totals)
        baseline = sequential_medians.get((profile, size_mb), median_total)
        workers = int(workers_text)
        speedup = baseline / median_total if median_total > 0.0 else math.nan
        efficiency = speedup / workers if workers > 0 else math.nan
        if method == "sequential":
            speedup = 1.0
            efficiency = 1.0
        word_counts = [int(row["word_count"]) for row in rows if row["word_count"]]
        unique_word_counts = [int(row["unique_word_count"]) for row in rows if row["unique_word_count"]]
        total_summary_rows.append(
            {
                "profile": profile,
                "size_mb": size_mb,
                "method": method,
                "workers": workers,
                "runs": len(rows),
                "median_total": f"{median_total:.6f}",
                "min_total": f"{min_total:.6f}",
                "mean_total": f"{mean_total:.6f}",
                "stddev_total": "" if stddev_total is None else f"{stddev_total:.6f}",
                "speedup": f"{speedup:.6f}",
                "efficiency": f"{efficiency:.6f}",
                "word_count": int(statistics.median(word_counts)) if word_counts else "",
                "unique_word_count": int(statistics.median(unique_word_counts)) if unique_word_counts else "",
            }
        )

    phase_summary_rows: list[dict[str, Any]] = []
    for key in sorted(phase_groups):
        profile, size_mb, method, workers_text, phase = key
        rows = phase_groups[key]
        phase_values = [float(row["phase_seconds"]) for row in rows if row["phase_seconds"]]
        if not phase_values:
            continue
        median_phase, min_phase, mean_phase, stddev_phase = summarize_series(phase_values)
        scopes = sorted({row["phase_scope"] for row in rows if row["phase_scope"]})
        phase_summary_rows.append(
            {
                "profile": profile,
                "size_mb": size_mb,
                "method": method,
                "workers": int(workers_text),
                "phase": phase,
                "phase_scope": scopes[0] if len(scopes) == 1 else ",".join(scopes),
                "runs": len(rows),
                "median_phase_seconds": f"{median_phase:.6f}",
                "min_phase_seconds": f"{min_phase:.6f}",
                "mean_phase_seconds": f"{mean_phase:.6f}",
                "stddev_phase_seconds": "" if stddev_phase is None else f"{stddev_phase:.6f}",
            }
        )

    return total_summary_rows, phase_summary_rows


def main() -> int:
    args = build_argument_parser().parse_args()
    input_path = args.input if args.input.is_absolute() else repo_root() / args.input
    output_path = args.output if args.output.is_absolute() else repo_root() / args.output
    phase_output_path = args.phase_output
    if phase_output_path is None:
        phase_output_path = output_path.with_name(f"{output_path.stem}_phases{output_path.suffix}")
    elif not phase_output_path.is_absolute():
        phase_output_path = repo_root() / phase_output_path

    total_rows, phase_rows = build_summaries(load_rows(input_path))
    write_csv_rows(
        output_path,
        [
            "profile",
            "size_mb",
            "method",
            "workers",
            "runs",
            "median_total",
            "min_total",
            "mean_total",
            "stddev_total",
            "speedup",
            "efficiency",
            "word_count",
            "unique_word_count",
        ],
        total_rows,
    )
    write_csv_rows(
        phase_output_path,
        [
            "profile",
            "size_mb",
            "method",
            "workers",
            "phase",
            "phase_scope",
            "runs",
            "median_phase_seconds",
            "min_phase_seconds",
            "mean_phase_seconds",
            "stddev_phase_seconds",
        ],
        phase_rows,
    )
    print(f"wrote summary CSV {output_path}")
    print(f"wrote phase summary CSV {phase_output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
