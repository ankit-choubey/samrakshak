#!/usr/bin/env bash
# Automated Chaos Engineering Script for SigCtl
# Injects random signals, kills workers, triggers pauses/resumes, and checks pool recovery

source "$(dirname "$0")/common.sh"
setup_test_env "CHAOS"

DESC="Chaos Test: inject random signals and worker kills for 15s, verify pool recovers"

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "CHAOS" "${DESC}" "Supervisor failed to start"
fi

echo "Injecting chaos events..."
END_TIME=$((SECONDS + 15))
EVENT_COUNT=0

while [ ${SECONDS} -lt ${END_TIME} ]; do
    ACTION=$((RANDOM % 5))
    case ${ACTION} in
        0)
            # Kill a random running worker
            WPID=$(grep "PID=" sigctld.status 2>/dev/null | grep "RUNNING" | head -n 1 | sed -n 's/.*PID=\([0-9]*\).*/\1/p' || true)
            if [ -n "${WPID}" ]; then
                echo "[Chaos] Killing worker PID ${WPID}"
                kill -9 "${WPID}" 2>/dev/null || true
            fi
            ;;
        1)
            echo "[Chaos] Pausing pool"
            ./sigctl pause >/dev/null 2>&1 || true
            sleep 0.2
            ;;
        2)
            echo "[Chaos] Resuming pool"
            ./sigctl resume >/dev/null 2>&1 || true
            ;;
        3)
            echo "[Chaos] Reloading config"
            ./sigctl reload >/dev/null 2>&1 || true
            ;;
        4)
            echo "[Chaos] Querying status"
            ./sigctl status >/dev/null 2>&1 || true
            ;;
    esac
    EVENT_COUNT=$((EVENT_COUNT + 1))
    sleep 0.4
done

echo "Chaos injection completed (${EVENT_COUNT} events). Stabilizing pool..."

# Resume if suspended
./sigctl resume >/dev/null 2>&1 || true

# Wait up to 5s for supervisor to recover all workers to RUNNING
if ! wait_for 6 "grep -q 'STATE=RUNNING' sigctld.status && [ \$(grep -c 'STATE=RUNNING' sigctld.status || true) -eq 4 ]"; then
    report_fail "CHAOS" "${DESC}" "Pool failed to recover all 4 workers to RUNNING state after chaos"
fi

./sigctl stop >/dev/null 2>&1 || true
wait "${SUP_PID}" 2>/dev/null || true

report_pass "CHAOS" "${DESC} (${EVENT_COUNT} chaos events injected and survived)"
