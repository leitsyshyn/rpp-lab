#!/usr/bin/env python3
from __future__ import annotations

import csv
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from benchmark_common import generate_profile_file, parse_benchmark_report  # noqa: E402
from summarize_benchmarks import build_summaries  # noqa: E402


class BenchmarkScriptTest(unittest.TestCase):
    def test_benchmark_report_parser_matches_stable_format(self) -> None:
        report = parse_benchmark_report(
            "method: sequential\n"
            "worker_count: 1\n"
            "text_size: 17\n"
            "word_count: 4\n"
            "unique_word_count: 3\n"
            "total_duration: 0.500000\n"
            "phases:\n"
            "- read 0.100000\n"
            "- count 0.200000\n"
            "- finalize 0.050000\n"
        )

        self.assertEqual(report.method, "sequential")
        self.assertEqual(report.worker_count, 1)
        self.assertEqual(report.input_size_bytes, 17)
        self.assertEqual(report.word_count, 4)
        self.assertEqual(report.unique_word_count, 3)
        self.assertEqual(report.total_seconds, 0.5)
        self.assertEqual(
            [phase.name for phase in report.phases],
            ["read", "count", "finalize", "total"],
        )
        self.assertEqual(
            [phase.scope for phase in report.phases],
            ["global", "global", "global", "global"],
        )

    def test_benchmark_report_parser_accepts_legacy_format(self) -> None:
        report = parse_benchmark_report(
            "method: sequential\n"
            "worker_count: 1\n"
            "input_size_bytes: 17\n"
            "word_count: 4\n"
            "unique_word_count: 3\n"
            "total_seconds: 0.500000\n"
            "phases:\n"
            "- read 0.100000 local\n"
        )

        self.assertEqual(report.input_size_bytes, 17)
        self.assertEqual(report.total_seconds, 0.5)
        self.assertEqual(report.phases[0].scope, "local")
        self.assertEqual(report.phases[1].name, "total")

    def test_generator_is_deterministic_for_small_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="wf_script_test_") as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            first_path = temp_dir / "first.txt"
            second_path = temp_dir / "second.txt"
            generate_profile_file("highcard", first_path, 12345, target_size_bytes=4096)
            generate_profile_file("highcard", second_path, 12345, target_size_bytes=4096)
            self.assertEqual(first_path.read_bytes(), second_path.read_bytes())
            self.assertGreaterEqual(first_path.stat().st_size, 4096)

    def test_summary_computes_speedup_and_efficiency(self) -> None:
        rows = list(
            csv.DictReader(
                [
                    "timestamp,input_file,input_name,profile,size_mb,input_bytes,method,workers,requested_workers,run_index,is_warmup,phase,phase_scope,phase_seconds,total_seconds,word_count,unique_word_count,exit_code,success,status,wall_seconds",
                    "2026-01-01T00:00:00Z,input,natural_10mb_seed1.txt,natural,10,100,sequential,1,1,1,false,total,local,4.000000,4.000000,100,10,0,true,ok,4.000000",
                    "2026-01-01T00:00:01Z,input,natural_10mb_seed1.txt,natural,10,100,sequential,1,1,2,false,total,local,6.000000,6.000000,100,10,0,true,ok,6.000000",
                    "2026-01-01T00:00:02Z,input,natural_10mb_seed1.txt,natural,10,100,openmp,4,4,1,false,total,local,2.000000,2.000000,100,10,0,true,ok,2.000000",
                    "2026-01-01T00:00:03Z,input,natural_10mb_seed1.txt,natural,10,100,openmp,4,4,2,false,total,local,3.000000,3.000000,100,10,0,true,ok,3.000000",
                    "2026-01-01T00:00:04Z,input,natural_10mb_seed1.txt,natural,10,100,openmp,4,4,2,false,read,local,0.500000,3.000000,100,10,0,true,ok,3.000000",
                ]
            )
        )

        total_rows, phase_rows = build_summaries(rows)
        self.assertEqual(len(total_rows), 2)
        self.assertEqual(len(phase_rows), 3)

        sequential_row = next(row for row in total_rows if row["method"] == "sequential")
        openmp_row = next(row for row in total_rows if row["method"] == "openmp")
        self.assertEqual(sequential_row["speedup"], "1.000000")
        self.assertEqual(sequential_row["efficiency"], "1.000000")
        self.assertEqual(openmp_row["median_total"], "2.500000")
        self.assertEqual(openmp_row["speedup"], "2.000000")
        self.assertEqual(openmp_row["efficiency"], "0.500000")


if __name__ == "__main__":
    unittest.main()
