#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T3"

DESC="Zombie check: kill workers repeatedly and verify no zombie processes remain"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T3" "${DESC}" "Supervisor did not start"
fi

# Kill worker 0 and worker 1
PIDS=$(grep "PID=" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p' | head -n 2)
for p in ${PIDS}; do
    kill -9 "${p}" 2>/dev/null || true
done

# Wait for restarts to happen
wait_for 5 "grep -q 'RESTARTS=' sigctld.status"

# Check ps for any zombie (<defunct> or STAT Z) children of supervisor
ZOMBIES=$(ps --ppid "${SUP_PID}" -o stat,pid,comm 2>/dev/null | grep -E '^Z' || true)
if [ -n "${ZOMBIES}" ]; then
    report_fail "T3" "${DESC}" "Found zombie processes: ${ZOMBIES}"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T3" "${DESC}"
