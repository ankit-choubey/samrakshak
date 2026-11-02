#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T10"

DESC="Signal burst: send 50 SIGUSR1 signals in a tight loop, supervisor stays responsive"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T10" "${DESC}" "Supervisor did not start"
fi

# Send 50 SIGUSR1 in a tight loop
for i in {1..50}; do
    kill -USR1 "${SUP_PID}" 2>/dev/null || true
done

# Verify supervisor is alive and status responds immediately
if ! wait_for 3 "./sigctl status >/dev/null 2>&1"; then
    report_fail "T10" "${DESC}" "Supervisor failed to respond to status after signal burst"
fi

# Verify all 4 workers are still running
WORKER_COUNT=$(grep -c "STATE=RUNNING" sigctld.status || true)
if [ "${WORKER_COUNT}" -lt 4 ]; then
    report_fail "T10" "${DESC}" "Expected 4 workers running, found ${WORKER_COUNT}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T10" "${DESC}"
