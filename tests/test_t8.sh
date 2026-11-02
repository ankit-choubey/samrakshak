#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
setup_test_env "T8"

DESC="Forced stop: worker ignores SIGTERM, supervisor escalates to SIGKILL after timeout, exits code 3"

# Set short shutdown timeout (2 seconds) and 1 worker
cat << 'EOF' > sigctl.conf
worker_count = 1
shutdown_timeout_s = 2
heartbeat_timeout_s = 5
max_restarts = 1
verbose = 0
EOF

# Create a worker script that notifies sync pipe but ignores SIGTERM
cat << 'EOF' > worker
#!/usr/bin/env bash
slot="$1"
sync_fd="$2"
if [ -n "${sync_fd}" ] && [ "${sync_fd}" -ge 0 ]; then
    echo -n "1" >&"${sync_fd}" 2>/dev/null || true
    exec {sync_fd}>&- 2>/dev/null || true
fi
trap '' TERM
while true; do
    sleep 0.5
done
EOF
chmod +x worker

./sigctld >/dev/null 2>&1 &
SUP_PID=$!

if ! wait_for 5 "grep -q 'STATE=RUNNING' sigctld.status"; then
    report_fail "T8" "${DESC}" "Supervisor did not reach RUNNING"
fi

./sigctl stop

# Supervisor should wait 2s, send SIGKILL, and exit with code 3
set +e
wait "${SUP_PID}" 2>/dev/null
EXIT_CODE=$?
set -e

if [ "${EXIT_CODE}" -ne 3 ]; then
    report_fail "T8" "${DESC}" "Expected forced kill exit code 3, got ${EXIT_CODE}"
fi

report_pass "T8" "${DESC}"
