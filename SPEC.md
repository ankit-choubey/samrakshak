# SigCtl: A Linux Signal Handling and Process Control Framework

## 1. Overview
SigCtl is a command-line process supervisor written in C for Linux. It launches a pool of worker processes, handles POSIX signals safely and asynchronously, and lets an operator control the pool at runtime. The operator can suspend all workers, resume them, reload configuration, or shut everything down gracefully. A separate CLI tool sends commands to the supervisor.

The project demonstrates how a long-running Unix program receives, queues, and reacts to signals without corrupting its state, how it reaps child processes, and how it coordinates a clean shutdown.

## 2. Objectives
- Register handlers for a defined set of signals using `sigaction()`, with explicit control over flags and masks.
- Process signals asynchronously in a way that is safe. Handlers do only async-signal-safe work, and the main loop does the rest.
- Manage child processes: spawn them, track them, and reap them so no zombies remain.
- Terminate the whole system gracefully, with a timeout and escalation to forced kill.
- Suspend and resume the worker pool at runtime using signals, with the workers able to respond to these signals themselves.
- Provide an observable log and status interface so each behavior can be demonstrated and verified.

## 3. Scope
### In scope
- Single-host Linux, targeting glibc and POSIX.1-2008 interfaces
- Supervisor process, worker processes, and a control CLI
- Signal handling for standard (non-real-time) signals
- Process groups for bulk signal delivery
- A status file and a command FIFO for richer control than signals alone allow
- Structured event logging with timestamps

### Out of scope
- Networking, remote control, or authentication
- Real-time signals (SIGRTMIN to SIGRTMAX), except as an optional extension
- Daemonization, systemd integration, or privilege dropping
- A graphical interface
- Persistent checkpointing of worker memory

## 4. Functional Requirements
- **FR1: Signal registration.** The supervisor installs handlers for SIGINT, SIGTERM, SIGHUP, SIGCHLD, SIGUSR1, SIGUSR2, SIGTSTP, SIGCONT, and SIGALRM. SIGPIPE is ignored. Each registration uses `sigaction` with `SA_RESTART` where appropriate and `SA_NOCLDSTOP` for SIGCHLD.
- **FR2: Asynchronous event handling.** Signal handlers record only the fact that a signal arrived. The main loop consumes those records and performs the actual work.
- **FR3: Child lifecycle.** The supervisor starts N workers (configurable, default 4), records each PID with its state, reaps exited children on SIGCHLD, and restarts any worker that exits unexpectedly, up to a retry limit.
- **FR4: Graceful termination.** On SIGINT or SIGTERM, the supervisor stops accepting new work, sends SIGTERM to every worker’s process group, waits up to a configurable timeout (default 5 seconds), sends SIGKILL to any survivors, reaps all children, removes its temporary files, and exits with a defined status code.
- **FR5: Suspension and resumption.** SIGTSTP (or the CLI pause command) suspends the pool. Workers receive SIGTSTP, run a handler that logs their state, and stop. SIGCONT (or resume) continues them. The supervisor itself keeps running during suspension so it can still respond to commands.
- **FR6: Configuration reload.** SIGHUP re-reads the configuration file and adjusts the worker count: it adds workers if the count increased and gracefully stops extras if it decreased.
- **FR7: Watchdog.** Each worker writes a heartbeat timestamp. A periodic watchdog timer checks heartbeats and marks any worker whose heartbeat is stale as hung, then restarts it.
- **FR8: Runtime control.** The `sigctl` CLI supports `status`, `pause`, `resume`, `reload`, and `stop`. Signal-based commands go through `kill()`. Commands that need data, such as status, use the status file and the command FIFO.
- **FR9: Logging.** Every signal received, state transition, spawn, reap, and forced kill is written to a log with a monotonic timestamp and the PID that produced it.

## 5. Non-Functional Requirements
- **Safety**: No handler calls a non-async-signal-safe function such as malloc, printf, or fprintf. The code is checked against the POSIX list of async-signal-safe functions.
- **Correctness under load**: Multiple signals in quick succession (for example, a burst of SIGCHLD) must not be lost. Each child exit must be reaped.
- **Bounded shutdown**: Shutdown completes within the timeout plus a small margin, even if workers ignore SIGTERM.
- **No resource leaks**: No zombies, no leaked file descriptors, no leaked memory, verified with valgrind and ps.
- **Portability within Linux**: Builds with `gcc -std=c11 -Wall -Wextra -Werror -pedantic` and runs on a recent Ubuntu or Debian release.

## 6. Signal Map
| Signal | Source | Supervisor action | Worker action |
|---|---|---|---|
| SIGINT | Ctrl+C | Begin graceful shutdown | Default (terminate) |
| SIGTERM | kill, sigctl stop | Begin graceful shutdown | Log, flush, exit 0 |
| SIGHUP | kill -HUP, sigctl reload | Reload config, rescale pool | Ignored |
| SIGCHLD | Kernel, on child state change | Reap children, schedule restarts | None |
| SIGTSTP | Ctrl+Z, sigctl pause | Mark pool suspended | Log state, reset SIGTSTP to SIG_DFL, raise SIGTSTP |
| SIGCONT | sigctl resume | Mark pool running | Resume, log resumption |
| SIGUSR1 | Operator | Dump status to log | Dump heartbeat info |
| SIGUSR2 | Operator | Toggle verbose logging | Toggle verbose logging |
| SIGALRM | Timer / timerfd | Run watchdog check | None |
| SIGPIPE | Kernel | Ignored | Ignored |

SIGKILL and SIGSTOP cannot be caught or ignored. The framework uses SIGSTOP only as a fallback, and only through the kernel’s job-control path, so the supervisor never depends on handling them.

## 7. Architecture
Three executables and one static library:
- `sigctld` (supervisor)
- `worker` (worker binary, started with exec)
- `sigctl` (CLI client)
- `libsigfw.a` (shared library: sigreg, selfpipe, childtab, log, statefile, config)

### 7.1 Asynchronous Handling: Self-Pipe Design
Handlers write a 1-byte signal code into a non-blocking pipe. The supervisor main loop blocks in `poll()` on the self-pipe read end, the command FIFO, and a timerfd descriptor.

### 7.2 Signal Masking
Signals are blocked during critical sections using `sigprocmask()`. Forked children reset signal masks and dispositions.

### 7.3 State Machine
Four states:
- `RUNNING`: normal operation. Workers execute, watchdog runs, crashed workers restarted.
- `SUSPENDED`: workers stopped via process group. Watchdog paused. Commands still work.
- `DRAINING`: shutdown started. No restarts. Escalates to SIGKILL after timeout.
- `TERMINATED`: all children reaped, temporary files cleaned, process exits.

### 7.4 Process Groups
`setpgid(0, 0)` in worker after fork, parent calls `setpgid(pid, pid)`. Supervisor sends group signals with `kill(-pgid, sig)`.

## 8. Refinements & Fixes in Master Prompt
1. **Single Timer**: Use a single `timerfd` for watchdog ticks and shutdown escalation deadlines. Do not call `alarm()` which clashes with watchdog.
2. **Heartbeats via Anonymous Shared Memory**: Use `mmap(MAP_SHARED | MAP_ANONYMOUS)` array of timestamps for worker heartbeats rather than loose files.
3. **Command FIFO**: Open FIFO with `O_RDWR` so `poll()` does not trigger EOF upon client disconnect.
4. **Worker Orphan Protection**: `prctl(PR_SET_PDEATHSIG, SIGTERM)` so workers terminate if supervisor is killed.
