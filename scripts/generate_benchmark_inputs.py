#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from benchmark_common import (
    DEFAULT_SEED,
    SUPPORTED_PROFILES,
    derive_suite_seed,
    format_input_filename,
    generate_profile_file,
    repo_root,
    update_manifest,
)


SUITES = {
    "smoke": [(profile, 10) for profile in SUPPORTED_PROFILES],
    "standard": [
        *( (profile, 10) for profile in SUPPORTED_PROFILES ),
        *( (profile, 100) for profile in SUPPORTED_PROFILES ),
    ],
}


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate deterministic benchmark inputs")
    parser.add_argument("--profile", choices=SUPPORTED_PROFILES)
    parser.add_argument("--size-mb", type=int, choices=(10, 100, 500, 1000))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--suite", choices=tuple(SUITES))
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repo_root() / "data" / "generated",
        help="Directory for suite generation or manifest updates",
    )
    parser.add_argument(
        "--include-500mb",
        action="store_true",
        help="Add 500 MB cases to the selected suite",
    )
    return parser


def generate_suite(args: argparse.Namespace) -> int:
    suite_cases = list(SUITES[args.suite])
    if args.include_500mb:
        suite_cases.extend((profile, 500) for profile in SUPPORTED_PROFILES)

    records = []
    for profile, size_mb in suite_cases:
        seed = derive_suite_seed(args.seed, profile, size_mb)
        output_path = args.output_dir / format_input_filename(profile, size_mb, seed)
        record = generate_profile_file(profile, output_path, seed, size_mb=size_mb)
        records.append(record)
        print(f"generated {output_path} ({record.input_bytes} bytes)")

    manifest_path = update_manifest(args.output_dir, records)
    print(f"updated manifest {manifest_path}")
    return 0


def generate_single(args: argparse.Namespace) -> int:
    if args.profile is None or args.size_mb is None or args.output is None:
        raise SystemExit("--profile, --size-mb, and --output are required without --suite")

    output_path = args.output if args.output.is_absolute() else (repo_root() / args.output)
    record = generate_profile_file(args.profile, output_path, args.seed, size_mb=args.size_mb)
    update_manifest(output_path.parent, [record])
    print(f"generated {output_path} ({record.input_bytes} bytes)")
    return 0


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    if args.suite is not None:
        return generate_suite(args)
    return generate_single(args)


if __name__ == "__main__":
    raise SystemExit(main())
