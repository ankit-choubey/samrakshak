#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T15"

DESC="PDEATHSIG orphan protection: kill supervisor with SIGKILL, workers exit within 2s"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T15" "${DESC}" "Supervisor did not start"
fi

WORKER_PIDS=$(grep "WORKER\[" sigctld.status | sed -n 's/.*PID=\([0-9]*\).*/\1/p')

# Instantly kill supervisor with SIGKILL
kill -9 "${SUP_PID}" 2>/dev/null || true

# Workers must detect parent death and exit via PR_SET_PDEATHSIG within 2 seconds
all_dead=0
for i in {1..20}; do
    any_alive=0
    for wpid in ${WORKER_PIDS}; do
        if kill -0 "${wpid}" 2>/dev/null; then
            any_alive=1
            break
        fi
    done
    if [ "${any_alive}" -eq 0 ]; then
        all_dead=1
        break
    fi
    sleep 0.1
done

if [ "${all_dead}" -eq 0 ]; then
    report_fail "T15" "${DESC}" "Workers did not exit within 2s after supervisor was killed"
fi

report_pass "T15" "${DESC}"
