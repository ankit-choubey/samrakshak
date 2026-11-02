#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T2"

DESC="Crash recovery: kill -9 one worker, verify supervisor restarts it"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T2" "${DESC}" "Supervisor did not reach RUNNING"
fi

# Extract PID of worker 0
OLD_PID=$(grep "WORKER\[0\]" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')
if [ -z "${OLD_PID}" ]; then
    report_fail "T2" "${DESC}" "Could not find worker 0 PID"
fi

# Kill worker 0 with SIGKILL
kill -9 "${OLD_PID}"

# Wait for supervisor to log exit and increment restart count
if ! wait_for 5 "grep 'WORKER\[0\]' sigctld.status | grep -q 'RESTARTS=1'"; then
    report_fail "T2" "${DESC}" "Worker 0 was not restarted (RESTARTS=1 not found)"
fi

NEW_PID=$(grep "WORKER\[0\]" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')
if [ "${NEW_PID}" = "${OLD_PID}" ]; then
    report_fail "T2" "${DESC}" "New PID is identical to dead PID"
fi

if ! kill -0 "${NEW_PID}" 2>/dev/null; then
    report_fail "T2" "${DESC}" "New worker PID ${NEW_PID} is not running"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T2" "${DESC}"
