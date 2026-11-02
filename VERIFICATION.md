# SigCtl Framework - Verification and Test Audit Log

This document records the exact raw terminal execution outputs for all verification steps, memory audits, sanitizers, and regression tests.

---

## Part 1: Verification of Current State

### 1. Build Verification (`make clean && make 2>&1`)
```
rm -f src/*.o *.o libsigfw.a sigctld worker sigctl
rm -rf tests/tmp/ tests/logs/*.log
rm -f *.pid *.status *.fifo *.log /tmp/sigctl_*
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/sigreg.c -o src/sigreg.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/selfpipe.c -o src/selfpipe.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/childtab.c -o src/childtab.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/log.c -o src/log.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/statefile.c -o src/statefile.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/config.c -o src/config.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/shm_hb.c -o src/shm_hb.o
ar rcs libsigfw.a src/sigreg.o src/selfpipe.o src/childtab.o src/log.o src/statefile.o src/config.o src/shm_hb.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/sigctld.c -o src/sigctld.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. src/sigctld.o libsigfw.a -lrt -o sigctld
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/worker.c -o src/worker.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. src/worker.o libsigfw.a -lrt -o worker
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -c src/sigctl.c -o src/sigctl.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. src/sigctl.o libsigfw.a -lrt -o sigctl
```
**Status**: CLEAN - 0 warnings, 0 errors under `-std=c11 -Wall -Wextra -Werror -pedantic`.

---

### 2. Full Test Suite (`make test 2>&1`)
```
=============================================
       RUNNING SIGCTL TEST SUITE             
=============================================
PASS T1: Startup: Run supervisor with 4 workers, verify RUNNING state
PASS T10: Signal burst: send 50 SIGUSR1 signals in a tight loop, supervisor stays responsive
PASS T11: Watchdog: detect hung worker via stale heartbeat and restart it
PASS T12: Memory check: verify clean memory management and zero leaks under load
PASS T13: High-intensity signal burst: 200 SIGUSR1 in tight loop, verify supervisor responds and 0 workers lost
PASS T14: Ctrl+C equivalent: SIGINT to supervisor forwards shutdown to workers, no orphans remain
tests/test_t15.sh: line 35: 36938 Killed                     ./sigctld > /dev/null 2>&1
PASS T15: PDEATHSIG orphan protection: kill supervisor with SIGKILL, workers exit within 2s
PASS T2: Crash recovery: kill -9 one worker, verify supervisor restarts it
tests/test_t3.sh: line 22: 37014 Killed                     ./sigctld > /dev/null 2>&1
PASS T3: Zombie check: kill workers repeatedly and verify no zombie processes remain
Sent SIGTSTP (pause) to supervisor (PID 37054)
PASS T4: Pause: sigctl pause suspends pool, status becomes SUSPENDED, workers stop
Sent SIGTSTP (pause) to supervisor (PID 37091)
Sent SIGCONT (resume) to supervisor (PID 37091)
PASS T5: Resume: sigctl resume after pause, status returns to RUNNING, workers resume
Sent SIGTSTP (pause) to supervisor (PID 37127)
Sent SIGTSTP (pause) to supervisor (PID 37127)
PASS T6: Double pause: Send pause twice, verify second is idempotent and logged
Sent SIGTERM (stop) to supervisor (PID 37163)
PASS T7: Graceful stop: sigctl stop causes clean drain and supervisor exit 0
Sent SIGTERM (stop) to supervisor (PID 37198)
PASS T8: Forced stop: worker ignores SIGTERM, supervisor escalates to SIGKILL after timeout, exits code 3
Sent SIGHUP (reload) to supervisor (PID 37243)
Sent SIGHUP (reload) to supervisor (PID 37243)
PASS T9: Config reload: change worker count in sigctl.conf and send reload, pool rescales
---------------------------------------------
Test Summary: Total: 15 | Passed: 15 | Failed: 0
---------------------------------------------
SUCCESS: All tests passed with 100% success!
```
**Status**: PASS - All 15 tests executed and passed.

---

### 3. Valgrind Tool Check (`which valgrind && valgrind --version`)
```
which valgrind && valgrind --version
[USER_LOCAL_BIN]/valgrind
valgrind-3.26.0
```
**Status**: VERIFIED - Valgrind 3.26.0 installed and verified in user environment.

---

### 4. Valgrind Memory Leak Test (`make valgrind`)
```
Running valgrind memory leak check on T12 for 60 seconds...
==38550== HEAP SUMMARY:
==38550== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
PASS T12: Memory check: verify clean memory management and zero leaks under load
```
**Direct Valgrind Output on `sigctld` under full lifecycle**:
```
==38329== HEAP SUMMARY:
==38329==     in use at exit: 0 bytes in 0 blocks
==38329==   total heap usage: 30 allocs, 30 frees, 51,523 bytes allocated
==38329== 
==38329== All heap blocks were freed -- no leaks are possible
==38329== 
==38329== For lists of detected and suppressed errors, rerun with: -s
==38329== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```
**Status**: PASS - 0 errors, 0 memory leaks in 60-second execution.

---

### 5. AddressSanitizer & UndefinedBehaviorSanitizer Build (`make asan && make test`)

**Initial Run Failure Analysis**:
When `make asan && make test` was initially run, T12 failed with `Supervisor failed to start`:
`ASan runtime does not come first in initial library list; you should either link runtime to your application or manually preload it with LD_PRELOAD.`
*Cause*: Valgrind and AddressSanitizer are fundamentally incompatible because both instrument memory accesses and intercept the same libc functions.
*Fix*: Updated `tests/test_t12.sh` to check for ASan symbols (`nm ./sigctld | grep -q __asan`). When ASan is active, it runs natively under ASan leak/address detection; when ASan is not active, it runs under Valgrind.

**Post-Fix ASan Test Suite Run**:
```
rm -f src/*.o *.o libsigfw.a sigctld worker sigctl
rm -rf tests/tmp/ tests/logs/*.log
rm -f *.pid *.status *.fifo *.log /tmp/sigctl_*
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/sigreg.c -o src/sigreg.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/selfpipe.c -o src/selfpipe.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/childtab.c -o src/childtab.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/log.c -o src/log.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/statefile.c -o src/statefile.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/config.c -o src/config.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/shm_hb.c -o src/shm_hb.o
ar rcs libsigfw.a src/sigreg.o src/selfpipe.o src/childtab.o src/log.o src/statefile.o src/config.o src/shm_hb.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/sigctld.c -o src/sigctld.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g src/sigctld.o libsigfw.a -lrt -o sigctld
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/worker.c -o src/worker.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g src/worker.o libsigfw.a -lrt -o worker
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g -c src/sigctl.c -o src/sigctl.o
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. -fsanitize=address,undefined -g src/sigctl.o libsigfw.a -lrt -o sigctl
=============================================
       RUNNING SIGCTL TEST SUITE             
=============================================
PASS T1: Startup: Run supervisor with 4 workers, verify RUNNING state
PASS T10: Signal burst: send 50 SIGUSR1 signals in a tight loop, supervisor stays responsive
PASS T11: Watchdog: detect hung worker via stale heartbeat and restart it
PASS T12: Memory check: verify clean memory management and zero leaks under load
PASS T13: High-intensity signal burst: 200 SIGUSR1 in tight loop, verify supervisor responds and 0 workers lost
PASS T14: Ctrl+C equivalent: SIGINT to supervisor forwards shutdown to workers, no orphans remain
PASS T15: PDEATHSIG orphan protection: kill supervisor with SIGKILL, workers exit within 2s
PASS T2: Crash recovery: kill -9 one worker, verify supervisor restarts it
PASS T3: Zombie check: kill workers repeatedly and verify no zombie processes remain
PASS T4: Pause: sigctl pause suspends pool, status becomes SUSPENDED, workers stop
PASS T5: Resume: sigctl resume after pause, status returns to RUNNING, workers resume
PASS T6: Double pause: Send pause twice, verify second is idempotent and logged
PASS T7: Graceful stop: sigctl stop causes clean drain and supervisor exit 0
PASS T8: Forced stop: worker ignores SIGTERM, supervisor escalates to SIGKILL after timeout, exits code 3
PASS T9: Config reload: change worker count in sigctl.conf and send reload, pool rescales
---------------------------------------------
Test Summary: Total: 15 | Passed: 15 | Failed: 0
---------------------------------------------
SUCCESS: All tests passed with 100% success!
```
**Status**: PASS - 15/15 tests passed with zero ASan / UBSan errors.

---

### 6. Test Logs Audit (`ls tests/logs/`)
```
total 72
drwxrwxr-x 2 [USER] [USER] 4096 Nov  2 17:14 .
drwxrwxr-x 4 [USER] [USER] 4096 Nov  2 17:14 ..
-rw-rw-r-- 1 [USER] [USER] 1019 Nov  2 17:16 CHAOS.log
-rw-rw-r-- 1 [USER] [USER]   70 Nov  2 17:14 T1.log
-rw-rw-r-- 1 [USER] [USER]   93 Nov  2 17:14 T10.log
-rw-rw-r-- 1 [USER] [USER]   74 Nov  2 17:14 T11.log
-rw-rw-r-- 1 [USER] [USER]   81 Nov  2 17:14 T12.log
-rw-rw-r-- 1 [USER] [USER]  112 Nov  2 17:14 T13.log
-rw-rw-r-- 1 [USER] [USER]   98 Nov  2 17:14 T14.log
-rw-rw-r-- 1 [USER] [USER]  180 Nov  2 17:14 T15.log
-rw-rw-r-- 1 [USER] [USER]   75 Nov  2 17:14 T2.log
-rw-rw-r-- 1 [USER] [USER]  172 Nov  2 17:14 T3.log
-rw-rw-r-- 1 [USER] [USER]  130 Nov  2 17:14 T4.log
-rw-rw-r-- 1 [USER] [USER]  181 Nov  2 17:14 T5.log
-rw-rw-r-- 1 [USER] [USER]  174 Nov  2 17:14 T6.log
-rw-rw-r-- 1 [USER] [USER]  123 Nov  2 17:14 T7.log
-rw-rw-r-- 1 [USER] [USER]  152 Nov  2 17:14 T8.log
-rw-rw-r-- 1 [USER] [USER]  184 Nov  2 17:14 T9.log
```
**Status**: VERIFIED - Exactly 1 log per test (T1–T15) plus CHAOS.log.

---

### 7. Clean Stop and `/dev/shm` Check
```
./sigctl stop
ls /dev/shm | grep -i sigctl
sigctl_hb_27965
sigctl_hb_28544
sigctl_hb_29881
sigctl_hb_30579
sigctl_hb_30653
sigctl_hb_34792
sigctl_hb_34877
sigctl_hb_36938
sigctl_hb_37014
sigctl_hb_41382
sigctl_hb_41487
sigctl_hb_42765
sigctl_hb_42870
```
**Finding**: The current supervisor PID cleanly unlinks its own shm segment on exit, but stale segments from previous supervisor runs killed via `kill -9` remain in `/dev/shm`.
*Required Fix in Part 2a*: Supervisor startup must scan `/dev/shm` and unlink any stale `sigctl_hb_*` whose PID is no longer alive.

---

### 8. `kill -9` Crash and `/dev/shm` Check
```
kill -9 $SUP_PID
ls /dev/shm | grep -i sigctl
sigctl_hb_27965
...
sigctl_hb_44588
```
**Finding**: When `sigctld` is killed with `SIGKILL`, the kernel does not run user-space cleanup, leaving `sigctl_hb_<pid>`.
*Required Fix*: Startup garbage collector in `src/shm_hb.c` and verification in test T15.

---

### 9. Git Repository Status and History
```
git status:
On branch main
Changes not staged for commit:
	modified:   Makefile
	modified:   tests/chaos.sh
	modified:   tests/common.sh
	modified:   tests/test_t12.sh

git remote -v:
origin	https://github.com/ankit-choubey/samrakshak.git (fetch)
origin	https://github.com/ankit-choubey/samrakshak.git (push)

git log --oneline | head -5:
8ac7237 docs: complete README.md with architecture overview, Mermaid state diagram, and usage reference
7fd05cb docs: add DESIGN_CHANGES.md explaining architectural refinements and design decisions
ab80b08 feat(chaos): add automated chaos test script for random signal injection
8562e7e feat(build): comprehensive Makefile with static libsigfw.a, all, test, valgrind, and asan targets
5015d53 test(suite): add extra edge-case tests T13 (200 USR1 burst), T14 (Ctrl+C forwarding), and T15 (PDEATHSIG orphan exit)
```

---

### 10. `.gitignore` Verification and Untracking Stale Files
```
cat .gitignore:
# Binaries and libraries
sigctld
worker
sigctl
signal_framework
libsigfw.a
*.a
*.o
*.so

# Runtime and IPC files
*.pid
*.status
*.fifo
*.log
signal_logs.txt

# Test artifacts
tests/logs/
tests/tmp/

# OS and IDE files
.DS_Store
.vscode/
.idea/
```
**Finding**: Legacy files `.DS_Store`, `signal_framework`, and `signal_logs.txt` remained tracked from early commits.
*Fix*: Removed from git cache via `git rm --cached` and updated `.gitignore` to include `vgcore.*`.
