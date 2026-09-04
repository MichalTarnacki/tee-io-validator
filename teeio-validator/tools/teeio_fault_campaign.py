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

DEFAULT_ALLOWED_ACTUALS = {
    "error_or_timeout_then_recover": "doe_error,send_timeout,transport_receive_timeout,spdm_error_0x01,controlled_drop",
    "error_or_drop_then_recover": "doe_error,send_timeout,transport_receive_timeout,spdm_error_0x01,spdm_error_0x07,controlled_drop",
    "invalid_request_then_recover": "spdm_error_0x01",
    "unsupported_algorithm_no_session": "spdm_error_0x01,spdm_error_0x07",
    "invalid_request_no_session": "spdm_error_0x01",
    "version_mismatch_then_recover": "spdm_error_0x41",
    "unexpected_request_no_session": "spdm_error_0x04,spdm_error_0x0b",
    "unexpected_request_then_recover": "spdm_error_0x04",
    "invalid_session_then_recover": "transport_receive_failed,spdm_error_0x01,spdm_error_0x0b",
    "authentication_failure_clean_teardown": "spdm_error_0x06,transport_receive_failed,test_setup_failed",
    "certificate_verification_failure": "test_setup_failed",
    "unsupported_slot_then_recover": "spdm_error_0x01,spdm_error_0x07",
    "fresh_request_succeeds": "controlled_drop",
    "chunk_error_then_recover": "spdm_error_0x01,test_setup_failed",
    "incomplete_transfer_then_recover": "controlled_drop,transport_receive_timeout,test_setup_failed",
}


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


def source_metadata(repo: Path | None) -> dict:
    if repo is None:
        return {
            "repository": None,
            "commit": None,
            "status": None,
            "submodules": [],
            "source_verified": False,
        }
    return {
        "repository": str(repo),
        "commit": run_git(repo, "rev-parse", "HEAD"),
        "status": run_git(repo, "status", "--short"),
        "submodules": run_git(repo, "submodule", "status", "--recursive").splitlines(),
        "source_verified": True,
    }


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


def catalog_scenarios(config: configparser.ConfigParser) -> dict[str, dict[str, str]]:
    scenarios: dict[str, dict[str, str]] = {}
    for section in config.sections():
        if not section.startswith("FaultRule_"):
            continue
        scenario = config[section].get("scenario", "")
        expected = config[section].get("expected", "")
        allowed_actual = config[section].get(
            "allowed_actual", DEFAULT_ALLOWED_ACTUALS.get(expected, "")
        )
        runnable = config[section].get("runnable", "1")
        blocked_reason = config[section].get("blocked_reason", "")
        if not scenario or not expected:
            raise ValueError(f"{section} lacks scenario or expected")
        if runnable not in {"0", "1"}:
            raise ValueError(f"{section} runnable must be 0 or 1")
        if runnable == "0" and not blocked_reason:
            raise ValueError(f"{section} is blocked without blocked_reason")
        scenario_data = {
            "expected": expected,
            "allowed_actual": allowed_actual,
            "runnable": runnable,
            "blocked_reason": blocked_reason,
        }
        if scenario in scenarios and scenarios[scenario] != scenario_data:
            raise ValueError(f"{scenario} has conflicting expected results")
        scenarios[scenario] = scenario_data
    if not scenarios:
        raise ValueError("catalog has no fault scenarios")
    return scenarios


def write_scenario_config(
    catalog_path: Path, scenario: str, output: Path, enabled: bool
) -> None:
    catalog = read_catalog(catalog_path)
    catalog["FaultInjection"]["enabled"] = "1" if enabled else "0"
    catalog["FaultInjection"]["scenario"] = scenario
    with output.open("w", encoding="utf-8") as stream:
        catalog.write(stream, space_around_delimiters=False)


def command_for(binary: Path, ini: Path, extra_args: list[str]) -> list[str]:
    return [str(binary), "-f", str(ini), *extra_args]


def execute(
    command: list[str], log: Path, timeout: int, cwd: Path
) -> tuple[int, str]:
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            cwd=cwd,
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


def actual_is_allowed(actual: str, allowed_actual: str) -> bool:
    if not allowed_actual:
        return True
    return actual in {item.strip() for item in allowed_actual.split(",")}


def test_case_passed(output: str, test_case: str) -> bool:
    pattern = re.compile(rf"TestCase\s+{re.escape(test_case)}:\s+pass(?:\s|$)")
    return pattern.search(output) is not None


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
    parser.add_argument("--topology", type=int, default=1)
    parser.add_argument("--configuration", type=int, default=1)
    parser.add_argument("--driver", default="EndSessionAck.1")
    parser.add_argument("--recovery-driver", default="EndSessionAck.1")
    parser.add_argument("--no-recovery", action="store_true")
    parser.add_argument(
        "--standalone",
        action="store_true",
        help="run without a Git checkout; provenance is limited to file hashes",
    )
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
        expected_libspdm = catalog["FaultInjection"].get("libspdm_revision", "")
        if not expected_libspdm:
            raise ValueError("catalog has no libspdm_revision")
        if args.standalone:
            repo = None
            actual_libspdm = expected_libspdm
        else:
            repo = find_repo(Path(__file__).resolve().parent)
            actual_libspdm = run_git(repo / "spdm-emu" / "libspdm", "rev-parse", "HEAD")
            if actual_libspdm != expected_libspdm:
                raise ValueError(
                    f"catalog requires libspdm {expected_libspdm}, "
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
    artifacts = output / "artifacts"
    recovery_configs = output / "recovery-configs"
    recovery_logs = output / "recovery-logs"
    recovery_artifacts = output / "recovery-artifacts"
    configs.mkdir()
    logs.mkdir()
    artifacts.mkdir()
    recovery_configs.mkdir()
    recovery_logs.mkdir()
    recovery_artifacts.mkdir()

    run_args = [
        "-t", str(args.topology),
        "-c", str(args.configuration),
        "-s", args.driver,
        *extra_args,
    ]
    recovery_args = [
        "-t", str(args.topology),
        "-c", str(args.configuration),
        "-s", args.recovery_driver,
        *extra_args,
    ]

    manifest: dict = {
        "schema_version": 2,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.node(),
        "dry_run": args.dry_run,
        "binary": str(binary),
        "binary_sha256": sha256(binary),
        "catalog": str(catalog_path),
        "catalog_sha256": sha256(catalog_path),
        "libspdm_revision": actual_libspdm,
        "driver": args.driver,
        "recovery_driver": None if args.no_recovery else args.recovery_driver,
        "scenario_count": len(selected),
        "scenario_names": selected,
        "extra_args": extra_args,
        "positive": None,
        "scenarios": [],
    }
    manifest.update(source_metadata(repo))

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
            positive_artifacts = artifacts / "positive"
            positive_artifacts.mkdir()
            returncode, _ = execute(
                command, logs / "positive.log", args.timeout, positive_artifacts
            )
            positive["returncode"] = returncode
            positive["artifacts"] = str(positive_artifacts.relative_to(output))
            positive["status"] = "pass" if returncode == 0 else "fail"
            campaign_failed |= returncode != 0
        manifest["positive"] = positive

    for scenario in selected:
        scenario_data = available[scenario]
        record: dict = {
            "scenario": scenario,
            "expected": scenario_data["expected"],
            "allowed_actual": scenario_data["allowed_actual"],
            "status": "planned" if args.dry_run else "not_run",
        }
        if scenario_data["runnable"] == "0":
            record["status"] = "blocked"
            record["blocked_reason"] = scenario_data["blocked_reason"]
            manifest["scenarios"].append(record)
            if not args.dry_run:
                campaign_failed = True
            continue

        scenario_config = configs / f"{scenario}.ini"
        write_scenario_config(catalog_path, scenario, scenario_config, True)
        command = command_for(binary, scenario_config, run_args)
        record["command"] = command
        record["config_sha256"] = sha256(scenario_config)
        if not args.dry_run:
            scenario_artifacts = artifacts / scenario
            scenario_artifacts.mkdir()
            returncode, run_output = execute(
                command,
                logs / f"{scenario}.log",
                args.timeout,
                scenario_artifacts,
            )
            result = extract_result(run_output)
            record["returncode"] = returncode
            record["result"] = result
            record["artifacts"] = str(scenario_artifacts.relative_to(output))
            fired = (
                result is not None
                and result.get("scenario") == scenario
                and result.get("fired") is True
            )
            outcome_recorded = fired and result.get("actual") != "not_recorded"
            actual_allowed = (
                outcome_recorded
                and actual_is_allowed(
                    str(result.get("actual", "")),
                    scenario_data["allowed_actual"],
                )
            )
            record["status"] = (
                "pass" if actual_allowed else
                "unexpected_actual" if outcome_recorded else
                "no_outcome" if fired else
                "not_executed"
            )
            campaign_failed |= not actual_allowed

            if not args.no_recovery:
                recovery_config = recovery_configs / f"{scenario}.ini"
                write_scenario_config(catalog_path, scenario, recovery_config, False)
                recovery_command = command_for(binary, recovery_config, recovery_args)
                scenario_recovery_artifacts = recovery_artifacts / scenario
                scenario_recovery_artifacts.mkdir()
                recovery_returncode, recovery_output = execute(
                    recovery_command,
                    recovery_logs / f"{scenario}.log",
                    args.timeout,
                    scenario_recovery_artifacts,
                )
                recovery_passed = (
                    recovery_returncode == 0
                    and test_case_passed(recovery_output, args.recovery_driver)
                )
                record["recovery"] = {
                    "command": recovery_command,
                    "returncode": recovery_returncode,
                    "status": "pass" if recovery_passed else "fail",
                    "artifacts": str(
                        scenario_recovery_artifacts.relative_to(output)
                    ),
                }
                campaign_failed |= not recovery_passed
        manifest["scenarios"].append(record)

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    manifest["result"] = (
        "planned" if args.dry_run else "fail" if campaign_failed else "pass"
    )
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    summary_lines = [
        "scenario\texpected\texecution\tactual\tfire_count\trecovery\treturncode\tlog\treason"
    ]
    for record in manifest["scenarios"]:
        result = record.get("result") or {}
        recovery = record.get("recovery") or {}
        summary_lines.append(
            "\t".join(
                [
                    record["scenario"],
                    record["expected"],
                    record["status"],
                    str(result.get("actual", "")),
                    str(result.get("fire_count", "")),
                    recovery.get("status", "not_run"),
                    str(record.get("returncode", "")),
                    f"logs/{record['scenario']}.log" if "command" in record else "",
                    record.get("blocked_reason", ""),
                ]
            )
        )
    (output / "summary.tsv").write_text(
        "\n".join(summary_lines) + "\n", encoding="utf-8"
    )
    status_counts: dict[str, int] = {}
    for record in manifest["scenarios"]:
        status = record["status"]
        status_counts[status] = status_counts.get(status, 0) + 1
    status_summary = " ".join(
        f"{status}={count}" for status, count in sorted(status_counts.items())
    )
    print(
        f"Campaign {manifest['result']}: selected={len(selected)} "
        f"{status_summary}; "
        f"manifest={output / 'manifest.json'}"
    )
    return 1 if campaign_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())