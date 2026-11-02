#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T9"

DESC="Config reload: change worker count in sigctl.conf and send reload, pool rescales"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T9" "${DESC}" "Supervisor did not start"
fi

INITIAL_COUNT=$(grep -c "WORKER\[" sigctld.status || true)
if [ "${INITIAL_COUNT}" -ne 4 ]; then
    report_fail "T9" "${DESC}" "Expected 4 initial workers, found ${INITIAL_COUNT}"
fi

# 1. Scale UP from 4 to 6 workers
sed -i 's/worker_count = 4/worker_count = 6/' sigctl.conf
./sigctl reload

if ! wait_for 5 "[ \$(grep -c 'WORKER\[' sigctld.status || true) -eq 6 ]"; then
    report_fail "T9" "${DESC}" "Pool did not scale up to 6 workers"
fi

# 2. Scale DOWN from 6 to 3 workers
sed -i 's/worker_count = 6/worker_count = 3/' sigctl.conf
./sigctl reload

if ! wait_for 5 "[ \$(grep -c 'WORKER\[' sigctld.status || true) -eq 3 ]"; then
    report_fail "T9" "${DESC}" "Pool did not scale down to 3 workers"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T9" "${DESC}"
