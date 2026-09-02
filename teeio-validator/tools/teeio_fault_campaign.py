#!/usr/bin/env python3

"""Run and archive a teeio-validator SPDM fault-injection campaign."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


RESULT_RE = re.compile(r"Fault scenario result:\s*(\{.*\})")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def find_repo(path: Path) -> Path:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def read_catalog(path: Path) -> configparser.ConfigParser:
    config = configparser.ConfigParser(
        interpolation=None,
        inline_comment_prefixes=(";", "#"),
    )
    config.optionxform = str
    with path.open(encoding="utf-8") as stream:
        config.read_file(stream)
    if "FaultInjection" not in config:
        raise ValueError("catalog has no [FaultInjection] section")
    return config


def catalog_scenarios(config: configparser.ConfigParser) -> dict[str, str]:
    scenarios: dict[str, str] = {}
    for section in config.sections():
        if not section.startswith("FaultRule_"):
            continue
        scenario = config[section].get("scenario", "")
        expected = config[section].get("expected", "")
        if not scenario or not expected:
            raise ValueError(f"{section} lacks scenario or expected")
        if scenario in scenarios and scenarios[scenario] != expected:
            raise ValueError(f"{scenario} has conflicting expected results")
        scenarios[scenario] = expected
    if not scenarios:
        raise ValueError("catalog has no fault scenarios")
    return scenarios


def write_scenario_config(
    catalog: configparser.ConfigParser, scenario: str, output: Path
) -> None:
    catalog["FaultInjection"]["enabled"] = "1"
    catalog["FaultInjection"]["scenario"] = scenario
    with output.open("w", encoding="utf-8") as stream:
        catalog.write(stream, space_around_delimiters=False)


def command_for(binary: Path, ini: Path, extra_args: list[str]) -> list[str]:
    return [str(binary), "-f", str(ini), *extra_args]


def execute(command: list[str], log: Path, timeout: int) -> tuple[int, str]:
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        output = result.stdout + result.stderr
        log.write_text(output, encoding="utf-8")
        return result.returncode, output
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or "") + (error.stderr or "")
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        output += f"\nCampaign timeout after {timeout} seconds\n"
        log.write_text(output, encoding="utf-8")
        return 124, output


def extract_result(output: str) -> dict | None:
    matches = RESULT_RE.findall(output)
    if not matches:
        return None
    return json.loads(matches[-1])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--positive-ini", type=Path)
    parser.add_argument(
        "--scenario",
        action="append",
        help="scenario name; repeat as needed or use 'all' (default)",
    )
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "extra_args",
        nargs=argparse.REMAINDER,
        help="arguments passed to teeio_validator after --",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    binary = args.binary.resolve()
    catalog_path = args.catalog.resolve()
    output = args.output.resolve()
    extra_args = args.extra_args
    if extra_args and extra_args[0] == "--":
        extra_args = extra_args[1:]

    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"campaign: binary is not executable: {binary}", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("campaign: timeout must be positive", file=sys.stderr)
        return 2

    try:
        catalog = read_catalog(catalog_path)
        available = catalog_scenarios(catalog)
        repo = find_repo(Path(__file__).resolve().parent)
        expected_libspdm = catalog["FaultInjection"].get("libspdm_revision", "")
        actual_libspdm = run_git(repo / "spdm-emu" / "libspdm", "rev-parse", "HEAD")
        if not expected_libspdm or actual_libspdm != expected_libspdm:
            raise ValueError(
                f"catalog requires libspdm {expected_libspdm or '<missing>'}, "
                f"checkout has {actual_libspdm}"
            )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"campaign: {error}", file=sys.stderr)
        return 2

    selected = args.scenario or ["all"]
    if "all" in selected:
        selected = sorted(available)
    unknown = sorted(set(selected) - set(available))
    if unknown:
        print(f"campaign: unknown scenarios: {unknown}", file=sys.stderr)
        return 2

    output.mkdir(parents=True, exist_ok=False)
    configs = output / "configs"
    logs = output / "logs"
    configs.mkdir()
    logs.mkdir()

    manifest: dict = {
        "schema_version": 1,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.node(),
        "dry_run": args.dry_run,
        "repository": str(repo),
        "commit": run_git(repo, "rev-parse", "HEAD"),
        "status": run_git(repo, "status", "--short"),
        "submodules": run_git(repo, "submodule", "status", "--recursive").splitlines(),
        "binary": str(binary),
        "binary_sha256": sha256(binary),
        "catalog": str(catalog_path),
        "catalog_sha256": sha256(catalog_path),
        "libspdm_revision": actual_libspdm,
        "scenario_count": len(selected),
        "scenario_names": selected,
        "extra_args": extra_args,
        "positive": None,
        "scenarios": [],
    }

    campaign_failed = False
    if args.positive_ini:
        positive_ini = args.positive_ini.resolve()
        positive_config = configs / "positive.ini"
        positive_config.write_bytes(positive_ini.read_bytes())
        command = command_for(binary, positive_config, extra_args)
        positive = {
            "command": command,
            "config_sha256": sha256(positive_config),
            "status": "planned" if args.dry_run else "not_run",
        }
        if not args.dry_run:
            returncode, _ = execute(command, logs / "positive.log", args.timeout)
            positive["returncode"] = returncode
            positive["status"] = "pass" if returncode == 0 else "fail"
            campaign_failed |= returncode != 0
        manifest["positive"] = positive

    for scenario in selected:
        scenario_config = configs / f"{scenario}.ini"
        write_scenario_config(catalog, scenario, scenario_config)
        command = command_for(binary, scenario_config, extra_args)
        record: dict = {
            "scenario": scenario,
            "expected": available[scenario],
            "command": command,
            "config_sha256": sha256(scenario_config),
            "status": "planned" if args.dry_run else "not_run",
        }
        if not args.dry_run:
            returncode, run_output = execute(
                command, logs / f"{scenario}.log", args.timeout
            )
            result = extract_result(run_output)
            record["returncode"] = returncode
            record["result"] = result
            executed = (
                result is not None
                and result.get("scenario") == scenario
                and result.get("fired") is True
                and result.get("actual") != "not_recorded"
            )
            record["status"] = "executed" if executed else "not_executed"
            campaign_failed |= not executed
        manifest["scenarios"].append(record)

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    manifest["result"] = (
        "planned" if args.dry_run else "fail" if campaign_failed else "pass"
    )
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"Campaign {manifest['result']}: {len(selected)} scenarios; "
        f"manifest={output / 'manifest.json'}"
    )
    return 1 if campaign_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())