#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T7"

DESC="Graceful stop: sigctl stop causes clean drain and supervisor exit 0"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T7" "${DESC}" "Supervisor did not start"
fi

./sigctl stop

# Wait for supervisor to exit
wait "${SUP_PID}" 2>/dev/null
EXIT_CODE=$?

if [ "${EXIT_CODE}" -ne 0 ]; then
    report_fail "T7" "${DESC}" "Expected exit code 0, got ${EXIT_CODE}"
fi

# Verify no leftover workers
REMAINDER=$(pgrep -f "./worker" || true)
if [ -n "${REMAINDER}" ]; then
    report_fail "T7" "${DESC}" "Leftover workers found after stop: ${REMAINDER}"
fi

report_pass "T7" "${DESC}"
