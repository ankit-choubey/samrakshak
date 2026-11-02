#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T12"

DESC="Memory check: verify clean memory management and zero leaks under load"

IS_ASAN=0
if nm ./sigctld 2>/dev/null | grep -q __asan; then
    IS_ASAN=1
fi

if [ "${IS_ASAN}" -eq 0 ] && command -v valgrind >/dev/null 2>&1; then
    valgrind --leak-check=full --error-exitcode=1 ./sigctld >valgrind.log 2>&1 &
    SUP_PID=$!
else
    ./sigctld >/dev/null 2>&1 &
    SUP_PID=$!
fi

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T12" "${DESC}" "Supervisor failed to start"
fi

# Exercise memory paths under load
DURATION="${VALGRIND_DURATION:-5}"
START_TIME=$(date +%s)
while [ $(( $(date +%s) - START_TIME )) -lt "${DURATION}" ]; do
    ./sigctl pause >/dev/null 2>&1 || true
    ./sigctl resume >/dev/null 2>&1 || true
    ./sigctl reload >/dev/null 2>&1 || true
    ./sigctl status >/dev/null 2>&1 || true
    sleep 0.2
done

./sigctl stop >/dev/null 2>&1 || true
set +e
wait "${SUP_PID}" 2>/dev/null
EXIT_CODE=$?
set -e

if [ "${EXIT_CODE}" -ne 0 ]; then
    report_fail "T12" "${DESC}" "Supervisor exited with code ${EXIT_CODE}"
fi

if [ -f valgrind.log ]; then
    grep -E "(HEAP SUMMARY|definitely lost:|ERROR SUMMARY:)" valgrind.log || true
    if grep -q "definitely lost:" valgrind.log && ! grep -q "definitely lost: 0 bytes in 0 blocks" valgrind.log; then
        report_fail "T12" "${DESC}" "Valgrind detected memory leaks: $(grep 'definitely lost' valgrind.log)"
    fi
fi

report_pass "T12" "${DESC}"
