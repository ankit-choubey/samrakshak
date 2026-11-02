#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T1"

DESC="Startup: Run supervisor with 4 workers, verify RUNNING state"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "[ -f sigctld.pid ]"; then
    report_fail "T1" "${DESC}" "sigctld.pid not created within 5s"
fi

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T1" "${DESC}" "sigctld.status does not report STATE=RUNNING"
fi

# Count running workers in status file
WORKER_COUNT=$(grep -c "STATE=RUNNING" sigctld.status || true)
if [ "${WORKER_COUNT}" -lt 4 ]; then
    report_fail "T1" "${DESC}" "Expected 4 RUNNING workers, found ${WORKER_COUNT}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T1" "${DESC}"
