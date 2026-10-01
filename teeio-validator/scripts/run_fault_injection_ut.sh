#!/usr/bin/env bash
# Build and run the fault-injection Google Tests.
#
# Usage: run_fault_injection_ut.sh [VALIDATOR_BUILD_DIR]
#
# The host suites link the static libraries and reuse the compile flags of an
# existing validator build (default: build-pqc-current). Campaign INI tests run
# when TEEIO_FAULT_CATALOG_DIR names the campaign INI directory.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
validator_dir=$(cd -- "${script_dir}/.." && pwd)
repo_dir=$(cd -- "${validator_dir}/.." && pwd)
gtest_dir="${repo_dir}/third_party/googletest/googletest"
libspdm_dir="${repo_dir}/spdm-emu/libspdm"
test_dir="${validator_dir}/test"
sample_key_dir="${libspdm_dir}/unit_test/sample_key"
validator_build="${1:-${validator_dir}/build-pqc-current}"
spdm_flags_file="${validator_build}/out/spdm_test_lib.lib/CMakeFiles/spdm_test_lib.dir/flags.make"
entry_dir="${validator_build}/teeio_validator"
CC=${CC:-cc}
CXX=${CXX:-c++}
jobs=$(nproc)
build_dir=$(mktemp -d)
trap 'rm -rf "${build_dir}"' EXIT

if [[ ! -f "${gtest_dir}/src/gtest-all.cc" ]]; then
    printf 'GoogleTest sources not found at %s\n' "${gtest_dir}" >&2
    printf 'Run git submodule update --init third_party/googletest.\n' >&2
    exit 1
fi
if [[ ! -f "${spdm_flags_file}" || ! -f "${entry_dir}/CMakeFiles/teeio_validator.dir/link.txt" ]]; then
    printf 'Validator build not found at %s\n' "${validator_build}" >&2
    printf 'Build the validator first or pass its build directory.\n' >&2
    exit 1
fi
validator_build=$(cd -- "${validator_build}" && pwd)

# CMake writes these flags shell-quoted; LTO objects cannot be mixed in here.
read_c_flags() {
    local -n result=$2
    local flag parsed=()
    eval "parsed=($(sed -n 's/^\(C_DEFINES\|C_INCLUDES\|C_FLAGS\) = //p' "$1" | tr '\n' ' '))"
    result=()
    for flag in "${parsed[@]}"; do
        [[ ${flag} == -flto || ${flag} == -DUSING_LTO ]] || result+=("${flag}")
    done
}

# compile_c OUTPUT_DIR SOURCE... with the flags in the cflags array.
compile_c() {
    local output=$1 source running=0 failed=0
    shift
    for source in "$@"; do
        "${CC}" "${cflags[@]}" -c "${source}" \
            -o "${output}/$(basename "${source%.c}").o" &
        running=$((running + 1))
        if ((running >= jobs)); then
            wait -n || failed=1
            running=$((running - 1))
        fi
    done
    while ((running > 0)); do
        wait -n || failed=1
        running=$((running - 1))
    done
    return "${failed}"
}

compile_gtest() {
    "${CXX}" -std=c++17 -Wall -Wextra -Werror -pthread \
        -I"${gtest_dir}/include" -I"${test_dir}/common" \
        -DHOST_SAMPLE_KEY_DIR="\"${sample_key_dir}\"" \
        -c "$1" -o "$2"
}

link_gtest() {
    local output=$1
    shift
    "${CXX}" -pthread "$@" "${build_dir}/gtest-all.o" "${build_dir}/gtest_main.o" \
        -o "${output}"
}

"${CXX}" -std=c++17 -pthread -I"${gtest_dir}" -I"${gtest_dir}/include" \
    -c "${gtest_dir}/src/gtest-all.cc" -o "${build_dir}/gtest-all.o" &
"${CXX}" -std=c++17 -pthread -I"${gtest_dir}" -I"${gtest_dir}/include" \
    -c "${gtest_dir}/src/gtest_main.cc" -o "${build_dir}/gtest_main.o" &
"${CC}" -std=c99 -Wall -Wextra -Werror \
    -c "${test_dir}/common/host_check.c" -o "${build_dir}/host_check.o"
wait -n && wait -n

# Fault-injection engine and DOE harness: no validator build needed.
"${CC}" \
    -std=c99 -Wall -Wextra -Werror \
    -I"${validator_dir}/include" \
    -c "${validator_dir}/library/helperlib/fault_injection.c" \
    -o "${build_dir}/fault_injection.o"

spdm_emu_dir="${repo_dir}/spdm-emu"
# Same warning policy as the product build of pci_doe.c (no -Wextra).
"${CC}" \
    -std=c99 -Wall -Werror \
    -DLIBSPDM_CONFIG="\"${validator_dir}/include/spdm_lib_config.h\"" \
    -I"${spdm_emu_dir}/libspdm/os_stub/include" \
    -I"${spdm_emu_dir}/libspdm/include" \
    -I"${spdm_emu_dir}/libspdm/include/hal" \
    -I"${spdm_emu_dir}/include" \
    -I"${validator_dir}/include" \
    -I"${validator_dir}/library/helperlib/include" \
    -c "${validator_dir}/test/host_fault_injection/pci_doe_harness.c" \
    -o "${build_dir}/pci_doe_harness.o"

"${CXX}" \
    -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"${validator_dir}/include" \
    -I"${gtest_dir}/include" \
    -c "${test_dir}/host_fault_injection/fault_injection_test.cpp" \
    -o "${build_dir}/fault_injection_test.o"
link_gtest "${build_dir}/fault_injection_test" \
    "${build_dir}/fault_injection_test.o" \
    "${build_dir}/fault_injection.o" \
    "${build_dir}/pci_doe_harness.o"

# Host suites: production sources with real libspdm, built like the validator.
declare -a spdm_flags cflags
read_c_flags "${spdm_flags_file}" spdm_flags
libs=()
for name in spdm_requester_lib spdm_transport_pcidoe_lib spdm_secured_message_lib \
    spdm_common_lib spdm_crypt_lib spdm_crypt_ext_lib cryptlib_openssl memlib \
    debuglib malloclib rnglib platform_lib_null spdm_device_secret_lib_null; do
    libs+=("${validator_build}/lib/lib${name}.a")
done
crypto=$(ls "${validator_build}"/out/openssllib.lib/openssl-install/lib*/libcrypto.a | head -n 1)

chunk="${build_dir}/chunk"
mkdir -p "${chunk}"
cflags=("${spdm_flags[@]}" -I"${validator_dir}/library/helperlib/include"
    -DHOST_SAMPLE_KEY_DIR="\"${sample_key_dir}\"")
compile_c "${chunk}" \
    "${test_dir}/host_chunk_fault/chunk_fault_test.c" \
    "${validator_dir}/library/helperlib/fault_injection.c" \
    "${libspdm_dir}/library/spdm_responder_lib/libspdm_rsp_chunk_send_ack.c" \
    "${libspdm_dir}/library/spdm_responder_lib/libspdm_rsp_error.c"
compile_gtest "${test_dir}/host_chunk_fault/chunk_fault_gtest.cpp" "${chunk}/chunk_fault_gtest.o"
link_gtest "${chunk}/chunk_fault_gtest" -Wl,--gc-sections \
    "${chunk}/chunk_fault_gtest.o" "${chunk}/chunk_fault_test.o" \
    "${chunk}/fault_injection.o" "${chunk}/libspdm_rsp_chunk_send_ack.o" \
    "${chunk}/libspdm_rsp_error.o" "${build_dir}/host_check.o" \
    -Wl,--start-group "${libs[@]}" "${crypto}" -Wl,--end-group -ldl

# Key exchange: every libspdm library compiled from source with debug asserts.
kex="${build_dir}/key_exchange"
mkdir -p "${kex}"
cflags=("${spdm_flags[@]}" -I"${libspdm_dir}/include/hal"
    -DLIBSPDM_DEBUG_LIBSPDM_ASSERT_CONFIG=3
    -DLIBSPDM_DEBUG_LEVEL_CONFIG=LIBSPDM_DEBUG_ERROR)
compile_c "${kex}" \
    "${libspdm_dir}"/library/spdm_common_lib/*.c \
    "${libspdm_dir}"/library/spdm_crypt_lib/*.c \
    "${libspdm_dir}"/library/spdm_requester_lib/*.c \
    "${libspdm_dir}"/library/spdm_responder_lib/*.c \
    "${libspdm_dir}"/os_stub/spdm_device_secret_lib_sample/*.c \
    "${libspdm_dir}/os_stub/debuglib/debuglib.c" \
    "${validator_dir}/library/helperlib/fault_injection.c" \
    "${validator_dir}/library/spdm_test_lib/test_case/test_case_fault_key_exchange.c" \
    "${test_dir}/host_key_exchange_fault/key_exchange_test.c"
compile_gtest "${test_dir}/host_key_exchange_fault/key_exchange_gtest.cpp" \
    "${build_dir}/key_exchange_gtest.o"
kex_libs=()
for name in cryptlib_openssl memlib malloclib rnglib platform_lib_null \
    spdm_crypt_ext_lib spdm_secured_message_lib spdm_transport_pcidoe_lib; do
    kex_libs+=("${validator_build}/lib/lib${name}.a")
done
link_gtest "${build_dir}/key_exchange_gtest" -Wl,--gc-sections \
    "${build_dir}/key_exchange_gtest.o" "${kex}"/*.o "${build_dir}/host_check.o" \
    -Wl,--start-group "${kex_libs[@]}" "${crypto}" -Wl,--end-group -ldl

# Integration: the validator's own link line with run() replaced.
read_c_flags "${entry_dir}/CMakeFiles/teeio_validator.dir/flags.make" cflags
"${CC}" "${cflags[@]}" -Werror -c "${test_dir}/host_fault_integration/integration_test.c" \
    -o "${build_dir}/integration_test.o"
compile_gtest "${test_dir}/host_fault_integration/integration_gtest.cpp" \
    "${build_dir}/integration_gtest.o"
declare -a link_line entry_link
eval "link_line=($(cat "${entry_dir}/CMakeFiles/teeio_validator.dir/link.txt"))"
for part in "${link_line[@]:1}"; do
    case "${part}" in
        CMakeFiles/teeio_validator.dir/teeio_validator.c.o)
            entry_link+=("${build_dir}/integration_test.o" "${build_dir}/integration_gtest.o") ;;
        CMakeFiles/teeio_validator.dir/ide_test.c.o) ;;
        *) entry_link+=("${part}") ;;
    esac
done
for i in "${!entry_link[@]}"; do
    if [[ ${entry_link[i]} == -o ]]; then
        entry_link[i + 1]="${build_dir}/integration_gtest"
    fi
done
(cd "${entry_dir}" && "${CXX}" -pthread "${entry_link[@]}" \
    "${build_dir}/gtest-all.o" "${build_dir}/gtest_main.o")

# Validator runs write their logs to the working directory.
mkdir -p "${build_dir}/run"
cd "${build_dir}/run"
for test in fault_injection_test chunk/chunk_fault_gtest key_exchange_gtest \
    integration_gtest; do
    timeout 900 "${build_dir}/${test}"
done
