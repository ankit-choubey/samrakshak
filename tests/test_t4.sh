#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T4"

DESC="Pause: sigctl pause suspends pool, status becomes SUSPENDED, workers stop"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T4" "${DESC}" "Supervisor did not start"
fi

./sigctl pause

if ! wait_for 5 "grep -q 'STATE=SUSPENDED' sigctld.status"; then
    report_fail "T4" "${DESC}" "Status file did not transition to SUSPENDED"
fi

# Verify workers are stopped (STATE=STOPPED in status file)
STOPPED_COUNT=$(grep -c "STATE=STOPPED" sigctld.status || true)
if [ "${STOPPED_COUNT}" -lt 4 ]; then
    report_fail "T4" "${DESC}" "Expected 4 STOPPED workers, found ${STOPPED_COUNT}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T4" "${DESC}"
