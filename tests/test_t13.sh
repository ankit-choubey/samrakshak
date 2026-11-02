#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T13"

DESC="High-intensity signal burst: 200 SIGUSR1 in tight loop, verify supervisor responds and 0 workers lost"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T13" "${DESC}" "Supervisor did not reach RUNNING"
fi

# Send 200 SIGUSR1 signals in a rapid tight loop
for i in {1..200}; do
    kill -USR1 "${SUP_PID}" 2>/dev/null || true
done

# Verify supervisor still responds to status
if ! wait_for 3 "./sigctl status >/dev/null 2>&1"; then
    report_fail "T13" "${DESC}" "Supervisor became unresponsive after 200 signals"
fi

# Verify all 4 workers are still alive and running
ACTIVE_COUNT=$(grep -c "STATE=RUNNING" sigctld.status || true)
if [ "${ACTIVE_COUNT}" -lt 4 ]; then
    report_fail "T13" "${DESC}" "Lost workers during signal flood: found only ${ACTIVE_COUNT}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T13" "${DESC}"
