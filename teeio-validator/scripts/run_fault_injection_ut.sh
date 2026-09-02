#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
validator_dir=$(cd -- "${script_dir}/.." && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf "${build_dir}"' EXIT

"${CC:-cc}" \
    -std=c99 -Wall -Wextra -Werror \
    -I"${validator_dir}/include" \
    "${validator_dir}/library/helperlib/fault_injection.c" \
    "${validator_dir}/test/host_fault_injection/fault_injection_test.c" \
    -o "${build_dir}/fault_injection_test"

"${build_dir}/fault_injection_test"