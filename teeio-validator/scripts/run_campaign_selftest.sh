#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
validator_dir=$(cd -- "${script_dir}/.." && pwd)
repo_dir=$(cd -- "${validator_dir}/.." && pwd)
campaign="${validator_dir}/tools/teeio_fault_campaign.py"
catalog="${repo_dir}/doc/sample_ini/spdm_negative_cases.ini"
binary="${1:-${validator_dir}/build/bin/teeio_validator}"
work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT

python3 "$campaign" \
    --binary "$binary" \
    --catalog "$catalog" \
    --output "${work_dir}/campaign" \
    --dry-run

python3 - "${work_dir}/campaign/manifest.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)

assert manifest["schema_version"] == 2
assert manifest["dry_run"] is True
assert manifest["result"] == "planned"
assert len(manifest["scenarios"]) == 34
assert manifest["scenario_count"] == 34
assert len(manifest["scenario_names"]) == 34
assert sum(item["status"] == "planned" for item in manifest["scenarios"]) == 30
assert sum(item["status"] == "blocked" for item in manifest["scenarios"]) == 4
assert all(item["expected"] for item in manifest["scenarios"])
assert len(manifest["binary_sha256"]) == 64
assert len(manifest["catalog_sha256"]) == 64
assert manifest["commit"]
assert manifest["submodules"]
assert len(manifest["libspdm_revision"]) == 40
PY

cat > "${work_dir}/fake_validator.py" <<'PY'
#!/usr/bin/env python3
import configparser
import json
import sys

ini = sys.argv[sys.argv.index("-f") + 1]
config = configparser.ConfigParser(interpolation=None)
config.read(ini)
if not config["FaultInjection"].getboolean("enabled"):
    driver = sys.argv[sys.argv.index("-s") + 1]
    print(f"TestCase {driver}: pass")
    raise SystemExit(0)
scenario = config["FaultInjection"]["scenario"]
expected = next(
    config[section]["expected"]
    for section in config.sections()
    if section.startswith("FaultRule_")
    and config[section].get("scenario") == scenario
)
print(
    "Fault scenario result: "
    + json.dumps(
        {
            "scenario": scenario,
            "rule_id": 1,
            "disposition": "mutate",
            "match_count": 1,
            "expected": expected,
            "actual": "spdm_error_0x01",
            "fired": True,
            "fire_count": 1,
        }
    )
)
PY
chmod +x "${work_dir}/fake_validator.py"

python3 "$campaign" \
    --binary "${work_dir}/fake_validator.py" \
    --catalog "$catalog" \
    --output "${work_dir}/executed" \
    --scenario B1_reserved_version

python3 - "${work_dir}/executed/manifest.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)

assert manifest["result"] == "pass"
assert len(manifest["scenarios"]) == 1
assert manifest["scenarios"][0]["status"] == "pass"
assert manifest["scenarios"][0]["result"]["actual"] == "spdm_error_0x01"
assert manifest["scenarios"][0]["recovery"]["status"] == "pass"
PY
test -s "${work_dir}/executed/logs/B1_reserved_version.log"
test -s "${work_dir}/executed/recovery-logs/B1_reserved_version.log"

if python3 "$campaign" \
    --binary "$binary" \
    --catalog "$catalog" \
    --output "${work_dir}/unknown" \
    --scenario does_not_exist \
    --dry-run; then
    printf 'Campaign accepted an unknown scenario\n' >&2
    exit 1
fi

printf 'Fault campaign self-test passed\n'