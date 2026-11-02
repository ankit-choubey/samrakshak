#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T6"

DESC="Double pause: Send pause twice, verify second is idempotent and logged"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T6" "${DESC}" "Supervisor did not start"
fi

./sigctl pause
wait_for 5 "grep -q 'STATE=SUSPENDED' sigctld.status"

# Send pause second time
./sigctl pause

# Status must remain SUSPENDED and supervisor must not crash
if ! grep -q 'STATE=SUSPENDED' sigctld.status; then
    report_fail "T6" "${DESC}" "Status changed or corrupted after second pause"
fi

if ! kill -0 "${SUP_PID}" 2>/dev/null; then
    report_fail "T6" "${DESC}" "Supervisor died after double pause"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T6" "${DESC}"
