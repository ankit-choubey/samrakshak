#!/usr/bin/env bash
# Common test harness helper for SigCtl test suite

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/tests/logs"
mkdir -p "${LOG_DIR}"

if [ -x "${HOME}/.local/usr/bin/valgrind" ]; then
    export PATH="${HOME}/.local/usr/bin:${PATH}"
    export VALGRIND_LIB="${HOME}/.local/usr/libexec/valgrind"
fi

wait_for() {
    local timeout_secs="$1"
    local condition_cmd="$2"
    local elapsed=0
    local interval=0.1

    while true; do
        if eval "${condition_cmd}" >/dev/null 2>&1; then
            return 0
        fi
        sleep "${interval}"
        elapsed=$(echo "${elapsed} + ${interval}" | bc)
        if (( $(echo "${elapsed} >= ${timeout_secs}" | bc -l) )); then
            return 1
        fi
    done
}

setup_test_env() {
    local test_id="$1"
    TEST_DIR="${ROOT_DIR}/tests/tmp/${test_id}"
    rm -rf "${TEST_DIR}"
    mkdir -p "${TEST_DIR}"

    # Copy binaries and default config
    cp "${ROOT_DIR}/sigctld" "${TEST_DIR}/"
    cp "${ROOT_DIR}/worker" "${TEST_DIR}/"
    cp "${ROOT_DIR}/sigctl" "${TEST_DIR}/"
    cp "${ROOT_DIR}/sigctl.conf" "${TEST_DIR}/"

    cd "${TEST_DIR}"
    TEST_LOG="${LOG_DIR}/${test_id}.log"
    exec > >(tee "${TEST_LOG}") 2>&1

    trap cleanup_test_env EXIT INT TERM
}

cleanup_test_env() {
    set +e
    if [ -f sigctld.pid ]; then
        local pid
        pid=$(cat sigctld.pid 2>/dev/null || true)
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            kill -TERM "${pid}" 2>/dev/null || true
            sleep 0.2
            kill -KILL "${pid}" 2>/dev/null || true
        fi
    fi
    pkill -9 -f "./worker" 2>/dev/null || true
    pkill -9 -f "./sigctld" 2>/dev/null || true
    rm -f *.pid *.status *.fifo *.log /tmp/sigctl_* 2>/dev/null || true
}

report_pass() {
    local id="$1"
    local desc="$2"
    echo "PASS ${id}: ${desc}"
}

report_fail() {
    local id="$1"
    local desc="$2"
    local reason="${3:-unknown}"
    echo "FAIL ${id}: ${desc} - Reason: ${reason}"
    exit 1
}
