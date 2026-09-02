#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
validator_dir=$(cd -- "${script_dir}/.." && pwd)
tool="${validator_dir}/tools/teeio_regression.py"
baseline="${validator_dir}/test/regression/tdisp_ecp384_v10.json"
sample="${validator_dir}/test/regression/tdisp_synthetic.log"
work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT

python3 "$tool" "$sample" "$baseline" --output "${work_dir}/pass.json"
grep -q '"result": "pass"' "${work_dir}/pass.json"
grep -q '"case_count": 22' "${work_dir}/pass.json"

sed 's/TestCase DeviceState.3: pass/TestCase DeviceState.3: fail/' \
    "$sample" > "${work_dir}/seeded.log"
if python3 "$tool" "${work_dir}/seeded.log" "$baseline" \
    --output "${work_dir}/seeded.json"; then
    printf 'Seeded regression was not detected\n' >&2
    exit 1
fi
grep -q 'DeviceState.3: status fail != pass' "${work_dir}/seeded.json"

sed '/TestCase StopInterface.2:/d' "$sample" > "${work_dir}/missing.log"
if python3 "$tool" "${work_dir}/missing.log" "$baseline" \
    --output "${work_dir}/missing.json"; then
    printf 'Missing test case was not detected\n' >&2
    exit 1
fi
grep -q 'StopInterface.2: missing from log' "${work_dir}/missing.json"

python3 "$tool" "$sample" "$baseline" \
    --update "${work_dir}/updated-baseline.json"
grep -q '"assertion_counts_complete": true' \
    "${work_dir}/updated-baseline.json"

printf 'Regression parser self-test passed\n'