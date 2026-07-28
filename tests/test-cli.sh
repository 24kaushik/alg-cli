#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
TEST_BINARY="${TEST_DIR}/alg-rgb"
TEST_DEVICE="${TEST_DIR}/alg_rgb"
TEST_STATE="${TEST_DIR}/animation.lock"

cleanup() {
    if [[ -x "${TEST_BINARY}" ]]; then
        "${TEST_BINARY}" stop >/dev/null 2>&1 || true
    fi
    rm -rf "${TEST_DIR}"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_contains() {
    local text="$1"
    local expected="$2"

    [[ "${text}" == *"${expected}"* ]] ||
        fail "expected '${text}' to contain '${expected}'"
}

touch "${TEST_DEVICE}" "${TEST_STATE}"

"${CC:-cc}" \
    -std=c11 \
    -O2 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    "-DDEVICE_PATH=\"${TEST_DEVICE}\"" \
    "-DANIMATION_STATE_PATH=\"${TEST_STATE}\"" \
    "${REPO_ROOT}/cli/alg-rgb.c" \
    -o "${TEST_BINARY}" \
    -lm

: >"${TEST_DEVICE}"
"${TEST_BINARY}" pink 2
[[ "$(<"${TEST_DEVICE}")" == "pink 2" ]] ||
    fail "static color command was not written exactly"

if "${TEST_BINARY}" pink 5 >/dev/null 2>&1; then
    fail "invalid brightness was accepted"
fi

start_output="$("${TEST_BINARY}" animate wave 40)"
assert_contains "${start_output}" "Started background animation: wave"

status_output="$("${TEST_BINARY}" status)"
assert_contains "${status_output}" "wave"
wave_pid="$(cut -d' ' -f1 "${TEST_STATE}")"

sleep 0.15
assert_contains "$(<"${TEST_DEVICE}")" "frame "

start_output="$("${TEST_BINARY}" animate pulse cyan 40)"
assert_contains "${start_output}" "Started background animation: pulse, cyan"

status_output="$("${TEST_BINARY}" status)"
assert_contains "${status_output}" "pulse color=cyan"
pulse_pid="$(cut -d' ' -f1 "${TEST_STATE}")"
[[ "${pulse_pid}" != "${wave_pid}" ]] ||
    fail "starting a new animation did not replace the old process"

: >"${TEST_DEVICE}"
"${TEST_BINARY}" green 3
[[ "$(<"${TEST_DEVICE}")" == "green 3" ]] ||
    fail "static command did not replace the animation"

status_output="$("${TEST_BINARY}" status)"
assert_contains "${status_output}" "No background keyboard animation"

rm -f "${TEST_STATE}"
: >"${TEST_DEVICE}"
"${TEST_BINARY}" red 1
[[ "$(<"${TEST_DEVICE}")" == "red 1" ]] ||
    fail "static command failed when the runtime state file was absent"

if "${TEST_BINARY}" animate rainbow 40 >/dev/null 2>&1; then
    fail "animation started without its managed runtime state file"
fi

echo "CLI lifecycle tests passed."
