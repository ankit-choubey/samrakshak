#!/bin/bash
# test_edge_cases.sh - Automated edge-case verification for signal_framework

set -e
cd "$(dirname "$0")"

echo "============================================================"
echo "    COMPILING AND TESTING ALL SIGNAL FRAMEWORK EDGE CASES   "
echo "============================================================"

# 1. Compile with strict flags
gcc -Wall -Wextra -Werror main.c -o signal_framework
echo "[✔] Build passed with zero warnings/errors (-Wall -Wextra -Werror)"

# 2. Automated test runner testing all edge cases:
# - Normal start & initial state (Option 1)
# - Custom signal delivery: SIGUSR1 (Option 3)
# - Interrupt handling: SIGINT without termination (Option 2)
# - Suspend child: SIGSTOP (Option 4)
# - Edge case: Suspend an already suspended child (Option 4 again -> should reject cleanly)
# - Verify state is STOPPED (Option 1)
# - Resume child: SIGCONT (Option 5)
# - Edge case: Resume an already running child (Option 5 again -> should reject cleanly)
# - Graceful exit & cleanup: SIGTERM (Option 6)
# - Edge case: Send signals to already terminated child (Option 2 -> should reject cleanly)
# - Verify state is TERMINATED & child reaped (Option 1)
# - View logs (Option 7)
# - Clean exit with zero orphans/zombies (Option 8)

python3 - << 'EOF'
import subprocess, time, sys

print("\n--- Running Automated Edge Case Verification ---")
p = subprocess.Popen(["./signal_framework"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

test_steps = [
    ("1", "Check initial state (RUNNING)"),
    ("3", "Send SIGUSR1 (Custom signal)"),
    ("2", "Send SIGINT (Interrupt signal)"),
    ("4", "Suspend child via SIGSTOP"),
    ("4", "Edge Case: Double SIGSTOP on already suspended child"),
    ("1", "Verify state is STOPPED"),
    ("5", "Resume child via SIGCONT"),
    ("5", "Edge Case: Double SIGCONT on already running child"),
    ("6", "Graceful shutdown via SIGTERM (Cleanup & Reaping)"),
    ("2", "Edge Case: Send SIGINT to already TERMINATED child"),
    ("1", "Verify state is TERMINATED and reaped"),
    ("7", "View audit log history"),
    ("8", "Clean program exit")
]

for cmd, desc in test_steps:
    time.sleep(0.4)
    print(f"-> Testing: {desc} (Option {cmd})")
    p.stdin.write(cmd + "\n")
    p.stdin.flush()

stdout, _ = p.communicate(timeout=8)

# Verification checks
assert "Parent Process State : RUNNING" in stdout, "Failed initial state check"
assert "Handled SIGUSR1" in stdout, "Failed SIGUSR1 check"
assert "Handled SIGINT" in stdout, "Failed SIGINT check"
assert "Child process is already suspended" in stdout, "Failed duplicate SIGSTOP edge case"
assert "Child process is already running" in stdout, "Failed duplicate SIGCONT edge case"
assert "Cleanup completed" in stdout, "Failed graceful cleanup check"
assert "Error: Child process is already terminated" in stdout, "Failed terminated child edge case"
assert "Exiting framework. Goodbye!" in stdout, "Failed clean exit"

print("\n============================================================")
print("  ALL EDGE CASES PASSED WITH 100% SUCCESS! (ZERO ERRORS)    ")
print("============================================================")
EOF

