#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T11"

DESC="Watchdog: detect hung worker via stale heartbeat and restart it"

cat << 'EOF' > sigctl.conf
worker_count = 2
shutdown_timeout_s = 5
heartbeat_timeout_s = 2
max_restarts = 5
verbose = 1
EOF

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T11" "${DESC}" "Supervisor did not start"
fi

OLD_PID=$(grep "WORKER\[0\]" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')
if [ -z "${OLD_PID}" ]; then
    report_fail "T11" "${DESC}" "Could not find worker 0 PID"
fi

# Freeze worker 0 to stop its heartbeat
kill -STOP "${OLD_PID}"

# Watchdog should detect stale heartbeat within ~3-4 seconds and restart it
if ! wait_for 6 "grep 'WORKER\[0\]' sigctld.status | grep -q 'RESTARTS=1'"; then
    report_fail "T11" "${DESC}" "Watchdog did not restart hung worker within timeout"
fi

NEW_PID=$(grep "WORKER\[0\]" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')
if [ "${NEW_PID}" = "${OLD_PID}" ]; then
    report_fail "T11" "${DESC}" "Worker was not assigned a new PID"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "T11" "${DESC}"
