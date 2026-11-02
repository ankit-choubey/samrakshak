#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T5"

DESC="Resume: sigctl resume after pause, status returns to RUNNING, workers resume"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T5" "${DESC}" "Supervisor did not start"
fi

./sigctl pause
wait_for 5 "grep -q 'STATE=SUSPENDED' sigctld.status"

./sigctl resume

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T5" "${DESC}" "Status did not return to RUNNING after resume"
fi

RUNNING_COUNT=$(grep -c "STATE=RUNNING" sigctld.status || true)
if [ "${RUNNING_COUNT}" -lt 4 ]; then
    report_fail "T5" "${DESC}" "Expected 4 RUNNING workers after resume, found ${RUNNING_COUNT}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T5" "${DESC}"
