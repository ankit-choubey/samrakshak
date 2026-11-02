#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T14"

DESC="Ctrl+C equivalent: SIGINT to supervisor forwards shutdown to workers, no orphans remain"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T14" "${DESC}" "Supervisor did not start"
fi

# Capture worker PIDs
WORKER_PIDS=$(grep "WORKER\[" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')

# Dispatch SIGINT to supervisor only (emulating terminal Ctrl+C)
kill -INT "${SUP_PID}"

# Wait for supervisor to finish drain and exit 0
wait "${SUP_PID}" 2>/dev/null
EXIT_CODE=$?

if [ "${EXIT_CODE}" -ne 0 ]; then
    report_fail "T14" "${DESC}" "Supervisor exited with non-zero code ${EXIT_CODE}"
fi

# Verify every captured worker PID is completely terminated
for wpid in ${WORKER_PIDS}; do
    if kill -0 "${wpid}" 2>/dev/null; then
        report_fail "T14" "${DESC}" "Worker PID ${wpid} is still running after supervisor exit"
    fi
done

report_pass "T14" "${DESC}"
