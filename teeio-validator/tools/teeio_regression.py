#!/usr/bin/env python3

"""Parse teeio-validator summaries and compare them with a baseline."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


CASE_RE = re.compile(
    r"TestCase\s+(?P<name>[A-Za-z0-9_.-]+):\s+"
    r"(?P<status>pass|fail|skipped)"
    r"(?:\s+\(pass:\s*(?P<passed>\d+),\s*fail:\s*(?P<failed>\d+)\))?"
)


def parse_log(path: Path) -> dict[str, dict[str, int | str | None]]:
    cases: dict[str, dict[str, int | str | None]] = {}
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        match = CASE_RE.search(line)
        if not match:
            continue
        name = match.group("name")
        if name in cases:
            raise ValueError(f"duplicate TestCase {name} at line {line_number}")
        cases[name] = {
            "status": match.group("status"),
            "passed": int(match.group("passed")) if match.group("passed") else None,
            "failed": int(match.group("failed")) if match.group("failed") else None,
        }
    if not cases:
        raise ValueError(f"no TestCase summary lines found in {path}")
    return cases


def load_baseline(path: Path) -> dict:
    baseline = json.loads(path.read_text(encoding="utf-8"))
    if baseline.get("schema_version") != 1:
        raise ValueError("unsupported baseline schema_version")
    if not isinstance(baseline.get("cases"), dict) or not baseline["cases"]:
        raise ValueError("baseline has no cases")
    return baseline


def compare(cases: dict, baseline: dict) -> list[str]:
    differences: list[str] = []
    expected_cases = baseline["cases"]

    for name in sorted(expected_cases):
        expected = expected_cases[name]
        actual = cases.get(name)
        if actual is None:
            differences.append(f"{name}: missing from log")
            continue
        if actual["status"] != expected["status"]:
            differences.append(
                f"{name}: status {actual['status']} != {expected['status']}"
            )
        for field in ("passed", "failed"):
            if expected.get(field) is not None and actual[field] != expected[field]:
                differences.append(
                    f"{name}: {field} {actual[field]} != {expected[field]}"
                )

    for name in sorted(set(cases) - set(expected_cases)):
        differences.append(f"{name}: unexpected case in log")

    return differences


def update_baseline(cases: dict, baseline: dict, output: Path) -> None:
    expected_cases = baseline["cases"]
    if set(cases) != set(expected_cases):
        missing = sorted(set(expected_cases) - set(cases))
        extra = sorted(set(cases) - set(expected_cases))
        raise ValueError(f"case set differs; missing={missing}, extra={extra}")
    for name, result in cases.items():
        expected_cases[name] = result
    baseline["assertion_counts_complete"] = True
    output.write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")


def write_result(path: Path | None, cases: dict, differences: list[str]) -> None:
    result = {
        "schema_version": 1,
        "result": "pass" if not differences else "regression",
        "case_count": len(cases),
        "differences": differences,
        "cases": cases,
    }
    text = json.dumps(result, indent=2) + "\n"
    if path:
        path.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--update", type=Path, metavar="NEW_BASELINE")
    args = parser.parse_args()

    try:
        cases = parse_log(args.log)
        baseline = load_baseline(args.baseline)
        if args.update:
            update_baseline(cases, baseline, args.update)
            return 0
        differences = compare(cases, baseline)
        write_result(args.output, cases, differences)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"teeio_regression: {error}", file=sys.stderr)
        return 2

    return 1 if differences else 0


if __name__ == "__main__":
    raise SystemExit(main())