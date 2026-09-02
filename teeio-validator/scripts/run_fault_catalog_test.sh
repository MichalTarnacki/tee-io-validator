#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s <teeio_validator>\n' "$0" >&2
    exit 2
fi

binary=$(realpath "$1")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/../.." && pwd)
catalog="${repo_dir}/doc/sample_ini/spdm_negative_cases.ini"
work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT

mapfile -t scenarios < <(
    awk '
        /^\[FaultRule_[0-9]+\]$/ { in_rule = 1; next }
        /^\[/ { in_rule = 0 }
        in_rule && /^scenario=/ { sub(/^scenario=/, ""); print }
    ' "$catalog"
)

if [[ ${#scenarios[@]} -ne 34 ]]; then
    printf 'Expected 34 scenarios, found %s\n' "${#scenarios[@]}" >&2
    exit 1
fi

for group in B1 B2 B3 B4 B5 B6; do
    if ! printf '%s\n' "${scenarios[@]}" | grep -q "^${group}_"; then
        printf 'Catalog has no %s scenario\n' "$group" >&2
        exit 1
    fi
done

for scenario in "${scenarios[@]}"; do
    selected_ini="${work_dir}/${scenario}.ini"
    awk -v selected="$scenario" '
        /^\[FaultInjection\]$/ { in_fault = 1; print; next }
        /^\[/ { in_fault = 0 }
        in_fault && /^enabled=/ { print "enabled=1"; next }
        in_fault && /^scenario=/ { print "scenario=" selected; next }
        { print }
    ' "$catalog" > "$selected_ini"

    output=$(
        cd "$work_dir"
        "$binary" -f "$selected_ini" -t 1 -c 1 -s Version.1 -l verbose 2>&1 || true
    )
    if grep -q 'Parse .* failed' <<<"$output"; then
        printf 'Parser rejected scenario %s\n%s\n' "$scenario" "$output" >&2
        exit 1
    fi
    if ! grep -q "scenario=${scenario}" <<<"$output"; then
        printf 'Scenario %s was not selected\n' "$scenario" >&2
        exit 1
    fi
done

missing_scenario_ini="${work_dir}/missing_scenario.ini"
awk '
    /^\[FaultInjection\]$/ { in_fault = 1; print; next }
    /^\[/ { in_fault = 0 }
    in_fault && /^enabled=/ { print "enabled=1"; next }
    in_fault && /^scenario=/ { print "scenario=does_not_exist"; next }
    { print }
' "$catalog" > "$missing_scenario_ini"
output=$(
    cd "$work_dir"
    "$binary" -f "$missing_scenario_ini" -t 1 -c 1 -s Version.1 -l verbose 2>&1 || true
)
if ! grep -q 'no enabled rule matches scenario does_not_exist' <<<"$output"; then
    printf 'Parser accepted a missing selected scenario\n' >&2
    exit 1
fi

invalid_action_ini="${work_dir}/invalid_action.ini"
sed '0,/^action=.*/s//action=does_not_exist/' "$catalog" > "$invalid_action_ini"
output=$(
    cd "$work_dir"
    "$binary" -f "$invalid_action_ini" -t 1 -c 1 -s Version.1 -l verbose 2>&1 || true
)
if ! grep -q 'action is missing or invalid' <<<"$output"; then
    printf 'Parser accepted an invalid action\n' >&2
    exit 1
fi

printf 'Validated all %s B1-B6 fault scenarios\n' "${#scenarios[@]}"