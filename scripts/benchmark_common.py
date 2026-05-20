from __future__ import annotations

import csv
import json
import random
import re
import shutil
import subprocess
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


MB = 1024 * 1024
DEFAULT_SEED = 12345
SUPPORTED_PROFILES = ("natural", "lowcard", "highcard", "boundary")

_REPORT_START_RE = re.compile(r"^method:\s+(\S+)\s*$")
_PHASE_RE = re.compile(r"^-\s+(\S+)\s+([0-9]+(?:\.[0-9]+)?)(?:\s+(\S+))?\s*$")
_NAME_RE = re.compile(
    r"^(?P<profile>[a-z]+)_(?P<size_mb>[0-9]+)mb(?:_seed(?P<seed>[0-9]+))?\.txt$"
)

_NATURAL_WORDS = (
    "the",
    "quick",
    "brown",
    "fox",
    "version",
    "jumps",
    "over",
    "lazy",
    "dog",
    "generated",
    "scale",
    "benchmark",
    "cluster",
    "memory",
    "process",
    "thread",
    "input",
    "output",
    "kernel",
    "apple",
    "banana",
    "orange",
    "signal",
    "token",
    "parser",
    "report",
    "timing",
    "phase",
    "count",
    "merge",
    "collect",
    "reduce",
    "gather",
    "runtime",
    "throughput",
    "latency",
    "cache",
    "local",
    "distributed",
    "worker",
    "result",
    "deterministic",
    "validate",
    "correctness",
    "serial",
    "parallel",
)

_LOWCARD_WORDS = (
    "apple",
    "banana",
    "orange",
    "grape",
    "pear",
    "mpi",
    "openmp",
    "sequential",
)

_DELIMITERS = (" ", "  ", ", ", ". ", "! ", "? ", "\n", "\n\n", "\t")


@dataclass
class ParsedPhase:
    name: str
    seconds: float
    scope: str


@dataclass
class ParsedBenchmarkReport:
    method: str
    worker_count: int
    input_size_bytes: int | None
    word_count: int
    unique_word_count: int
    total_seconds: float
    phases: list[ParsedPhase]


@dataclass
class CommandResult:
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str
    wall_seconds: float

    @property
    def success(self) -> bool:
        return self.returncode == 0


@dataclass
class GeneratedInputRecord:
    filename: str
    profile: str
    size_mb: int
    seed: int
    input_bytes: int


@dataclass
class InputMetadata:
    path: Path
    input_name: str
    profile: str
    size_mb: int | None
    input_bytes: int
    seed: int | None


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def ensure_directory(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def bool_from_csv(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes"}


def format_input_filename(profile: str, size_mb: int, seed: int) -> str:
    return f"{profile}_{size_mb}mb_seed{seed}.txt"


def derive_suite_seed(base_seed: int, profile: str, size_mb: int) -> int:
    profile_index = SUPPORTED_PROFILES.index(profile)
    return base_seed + profile_index * 100000 + size_mb


def run_command(argv: list[str], cwd: Path | None = None) -> CommandResult:
    started_at = time.perf_counter()
    completed = subprocess.run(
        argv,
        cwd=str(cwd) if cwd is not None else None,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    finished_at = time.perf_counter()
    return CommandResult(
        argv=list(argv),
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        wall_seconds=finished_at - started_at,
    )


def read_text_file(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def write_csv_rows(path: Path, fieldnames: list[str], rows: Iterable[dict[str, Any]]) -> None:
    ensure_directory(path.parent)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_json(path: Path, payload: Any) -> None:
    ensure_directory(path.parent)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_benchmark_report(text: str) -> ParsedBenchmarkReport:
    lines = [line.rstrip("\n") for line in text.splitlines() if line.strip()]
    if len(lines) < 7:
        raise ValueError("benchmark report is incomplete")

    def expect_prefix(index: int, *prefixes: str) -> str:
        line = lines[index]
        for prefix in prefixes:
            if line.startswith(prefix):
                return line[len(prefix) :].strip()
        expected = " or ".join(repr(prefix) for prefix in prefixes)
        raise ValueError(f"expected {expected} at line {index + 1}, got: {line!r}")

    method = expect_prefix(0, "method:")
    worker_count = int(expect_prefix(1, "worker_count:"))
    input_size_text = expect_prefix(2, "input_size_bytes:", "text_size:")
    input_size_bytes = None if input_size_text == "unknown" else int(input_size_text)
    word_count = int(expect_prefix(3, "word_count:"))
    unique_word_count = int(expect_prefix(4, "unique_word_count:"))
    total_seconds = float(expect_prefix(5, "total_seconds:", "total_duration:"))

    if lines[6] != "phases:":
        raise ValueError(f"expected 'phases:' line, got: {lines[6]!r}")

    phases: list[ParsedPhase] = []
    for line in lines[7:]:
        match = _PHASE_RE.match(line)
        if match is None:
            raise ValueError(f"invalid phase line: {line!r}")
        phases.append(
            ParsedPhase(
                name=match.group(1),
                seconds=float(match.group(2)),
                scope=match.group(3) or "global",
            )
        )

    if not phases:
        raise ValueError("benchmark report does not contain any phases")

    return ParsedBenchmarkReport(
        method=method,
        worker_count=worker_count,
        input_size_bytes=input_size_bytes,
        word_count=word_count,
        unique_word_count=unique_word_count,
        total_seconds=total_seconds,
        phases=phases,
    )


def extract_single_benchmark_report(text: str) -> ParsedBenchmarkReport:
    all_lines = text.splitlines()
    start_indexes = [index for index, line in enumerate(all_lines) if _REPORT_START_RE.match(line)]
    if len(start_indexes) != 1:
        raise ValueError(f"expected exactly one benchmark report, found {len(start_indexes)}")

    start_index = start_indexes[0]
    report_lines: list[str] = []
    saw_phases = False
    for line in all_lines[start_index:]:
        if report_lines and _REPORT_START_RE.match(line):
            break
        if saw_phases and line.strip() and not _PHASE_RE.match(line):
            break
        if line.strip() == "phases:":
            saw_phases = True
        report_lines.append(line)

    return parse_benchmark_report("\n".join(report_lines))


def serialize_report(report: ParsedBenchmarkReport) -> dict[str, Any]:
    return {
        "method": report.method,
        "worker_count": report.worker_count,
        "input_size_bytes": report.input_size_bytes,
        "text_size": report.input_size_bytes,
        "word_count": report.word_count,
        "unique_word_count": report.unique_word_count,
        "total_seconds": report.total_seconds,
        "total_duration": report.total_seconds,
        "phases": [asdict(phase) for phase in report.phases],
    }


def parse_cmake_cache(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        key, separator, rest = line.partition(":")
        if not separator:
            continue
        _, separator, value = rest.partition("=")
        if not separator:
            continue
        values[key] = value
    return values


def discover_mpi_launcher(
    build_dir: Path | None,
    mpiexec_override: str | None,
    numproc_flag_override: str | None,
) -> tuple[str, str]:
    if mpiexec_override is not None:
        return mpiexec_override, numproc_flag_override or "-n"

    if build_dir is not None:
        cache = parse_cmake_cache(build_dir / "CMakeCache.txt")
        launcher = cache.get("MPIEXEC_EXECUTABLE")
        numproc_flag = cache.get("MPIEXEC_NUMPROC_FLAG")
        if launcher:
            return launcher, numproc_flag or "-n"

    mpiexec_path = shutil.which("mpiexec")
    if mpiexec_path is not None:
        return mpiexec_path, numproc_flag_override or "-n"

    mpirun_path = shutil.which("mpirun")
    if mpirun_path is not None:
        return mpirun_path, numproc_flag_override or "-np"

    raise FileNotFoundError("could not find mpiexec or mpirun; pass --mpiexec explicitly")


def build_mpi_command(
    launcher: str,
    numproc_flag: str,
    process_count: int,
    binary: Path,
    app_args: list[str],
) -> list[str]:
    return [launcher, numproc_flag, str(process_count), str(binary), *app_args]


def load_manifest(output_dir: Path) -> dict[str, GeneratedInputRecord]:
    manifest_path = output_dir / "manifest.json"
    if not manifest_path.exists():
        return {}

    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    records: dict[str, GeneratedInputRecord] = {}
    for item in payload.get("files", []):
        record = GeneratedInputRecord(
            filename=item["filename"],
            profile=item["profile"],
            size_mb=int(item["size_mb"]),
            seed=int(item["seed"]),
            input_bytes=int(item["input_bytes"]),
        )
        records[record.filename] = record
    return records


def update_manifest(output_dir: Path, records: Iterable[GeneratedInputRecord]) -> Path:
    ensure_directory(output_dir)
    merged = load_manifest(output_dir)
    for record in records:
        merged[record.filename] = record

    payload = {
        "files": [asdict(merged[name]) for name in sorted(merged)],
    }
    manifest_path = output_dir / "manifest.json"
    write_json(manifest_path, payload)
    return manifest_path


def infer_input_metadata(path: Path) -> InputMetadata:
    resolved = path.resolve()
    stat = resolved.stat()
    manifest = load_manifest(resolved.parent)
    record = manifest.get(resolved.name)
    if record is not None:
        return InputMetadata(
            path=resolved,
            input_name=resolved.name,
            profile=record.profile,
            size_mb=record.size_mb,
            input_bytes=record.input_bytes,
            seed=record.seed,
        )

    match = _NAME_RE.match(resolved.name)
    if match is not None:
        seed = match.group("seed")
        return InputMetadata(
            path=resolved,
            input_name=resolved.name,
            profile=match.group("profile"),
            size_mb=int(match.group("size_mb")),
            input_bytes=stat.st_size,
            seed=int(seed) if seed is not None else None,
        )

    return InputMetadata(
        path=resolved,
        input_name=resolved.name,
        profile="custom",
        size_mb=None,
        input_bytes=stat.st_size,
        seed=None,
    )


def discover_input_files(paths: Iterable[Path], directories: Iterable[Path]) -> list[Path]:
    discovered: list[Path] = []
    seen: set[Path] = set()
    for path in paths:
        resolved = path.resolve()
        if resolved not in seen:
            discovered.append(resolved)
            seen.add(resolved)
    for directory in directories:
        for path in sorted(directory.resolve().glob("*.txt")):
            if path.is_file() and path not in seen:
                discovered.append(path)
                seen.add(path)
    return discovered


def describe_output_difference(reference_path: Path, candidate_path: Path) -> str:
    reference_lines = reference_path.read_text(encoding="utf-8", errors="replace").splitlines()
    candidate_lines = candidate_path.read_text(encoding="utf-8", errors="replace").splitlines()
    max_lines = max(len(reference_lines), len(candidate_lines))
    for index in range(max_lines):
        reference_line = reference_lines[index] if index < len(reference_lines) else "<missing>"
        candidate_line = candidate_lines[index] if index < len(candidate_lines) else "<missing>"
        if reference_line != candidate_line:
            return (
                f"first differing line {index + 1}:\n"
                f"reference: {reference_line!r}\n"
                f"candidate: {candidate_line!r}"
            )
    return "files differ in binary content"


def apply_case_variant(word: str, rng: random.Random) -> str:
    variant = rng.randrange(4)
    if variant == 0:
        return word
    if variant == 1:
        return word.upper()
    if variant == 2:
        return word.title()
    return "".join(character.upper() if index % 2 == 0 else character for index, character in enumerate(word))


def make_boundary_word(token_index: int, length: int) -> str:
    pattern = f"boundary{token_index:08d}segment"
    repeated = (pattern * ((length // len(pattern)) + 1))[:length]
    return repeated


def next_generated_chunk(
    profile: str,
    rng: random.Random,
    state: dict[str, Any],
) -> str:
    if profile == "natural":
        sentence_count = rng.randint(2, 5)
        fragments: list[str] = []
        for _ in range(sentence_count):
            word_count = rng.randint(8, 18)
            sentence_parts: list[str] = []
            for word_index in range(word_count):
                if rng.random() < 0.14:
                    token = str(2000 + rng.randrange(0, 50))
                else:
                    token = apply_case_variant(rng.choice(_NATURAL_WORDS), rng)
                sentence_parts.append(token)
                if word_index != word_count - 1:
                    sentence_parts.append(rng.choice((" ", " ", " ", ", ", "; ", " - ")))
            sentence_parts.append(rng.choice((".\n", "!\n", "?\n", ", version 2026.\n", "\n")))
            fragments.append("".join(sentence_parts))
        return "".join(fragments)

    if profile == "lowcard":
        token_count = rng.randint(64, 128)
        fragments = []
        for _ in range(token_count):
            fragments.append(apply_case_variant(rng.choice(_LOWCARD_WORDS), rng))
            fragments.append(rng.choice(_DELIMITERS))
        return "".join(fragments)

    if profile == "highcard":
        token_count = rng.randint(48, 96)
        fragments = []
        recent_tokens: list[str] = state.setdefault("recent_tokens", [])
        next_index = state.setdefault("next_index", 1)
        for _ in range(token_count):
            if recent_tokens and rng.random() < 0.12:
                token = rng.choice(recent_tokens)
            else:
                token = f"word{next_index:08d}"
                next_index += 1
                recent_tokens.append(token)
                if len(recent_tokens) > 256:
                    del recent_tokens[0]
            fragments.append(token)
            fragments.append(rng.choice((" ", "\n", " ", ", ", "; ")))
        state["next_index"] = next_index
        return "".join(fragments)

    if profile == "boundary":
        token_count = rng.randint(2, 5)
        fragments = []
        next_index = state.setdefault("next_index", 1)
        for _ in range(token_count):
            length = rng.choice((257, 511, 1023, 2047, 4095, 8191))
            word = make_boundary_word(next_index, length)
            next_index += 1
            if rng.random() < 0.25:
                word = word.upper()
            fragments.append(word)
            fragments.append(rng.choice((" ", "\n", "\t", " | ")))
        state["next_index"] = next_index
        return "".join(fragments)

    raise ValueError(f"unsupported profile: {profile}")


def generate_profile_file(
    profile: str,
    output_path: Path,
    seed: int,
    *,
    size_mb: int | None = None,
    target_size_bytes: int | None = None,
) -> GeneratedInputRecord:
    if profile not in SUPPORTED_PROFILES:
        raise ValueError(f"unsupported profile: {profile}")
    if size_mb is None and target_size_bytes is None:
        raise ValueError("size_mb or target_size_bytes is required")

    requested_bytes = target_size_bytes if target_size_bytes is not None else size_mb * MB
    assert requested_bytes is not None

    ensure_directory(output_path.parent)
    rng = random.Random(seed)
    state: dict[str, Any] = {}
    bytes_written = 0

    with output_path.open("w", encoding="ascii", newline="") as output:
        while bytes_written < requested_bytes:
            chunk = next_generated_chunk(profile, rng, state)
            output.write(chunk)
            bytes_written += len(chunk)

    actual_bytes = output_path.stat().st_size
    effective_size_mb = size_mb if size_mb is not None else max(1, round(actual_bytes / MB))
    return GeneratedInputRecord(
        filename=output_path.name,
        profile=profile,
        size_mb=effective_size_mb,
        seed=seed,
        input_bytes=actual_bytes,
    )
