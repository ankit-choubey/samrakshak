# SigCtl: Architectural Refinements and Design Decisions

This document details the architectural refinements, POSIX safety enhancements, and deviations made from the original initial design during the implementation of SigCtl.

---

## 1. Single `timerfd` Multiplexing vs. Process-Wide `alarm()`

### Original Concept
The original specification suggested using `alarm()` / `SIGALRM` for watchdog checks while also using a timer mechanism for shutdown escalation deadlines.

### Problem on Linux / POSIX
In POSIX systems, `alarm()` operates on a single process-wide timer. Calling `alarm(timeout)` automatically cancels any previously active timer and returns the remaining time. If a shutdown deadline is scheduled while a watchdog is pending, the watchdog timer is overwritten, or conversely, the watchdog tick would cancel the shutdown escalation deadline.

### Refined Architecture
SigCtl replaces all `alarm()` calls with a single Linux `timerfd` created with:
```c
timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
```
The timer descriptor is multiplexed directly in the main supervisor `poll()` loop alongside the self-pipe and command FIFO.
- Watchdog checks evaluate `CLOCK_MONOTONIC` timestamps against `heartbeat_timeout_s`.
- Shutdown deadlines evaluate `CLOCK_MONOTONIC` timestamps against `s_drain_deadline_ms`.
- There are zero timer collisions and zero signal race conditions.

---

## 2. Shared Memory Heartbeats vs. Filesystem Heartbeat Files

### Original Concept
Workers were originally suggested to write timestamps to temporary files under `/tmp/worker_<pid>.hb`.

### Problem
1. **File Descriptor & Disk Overhead**: Opening, writing, flushing, and closing files every 500 ms creates unnecessary filesystem I/O, disk writes, and inode contention.
2. **Orphan Artifacts**: If a worker or supervisor crashes, dead `.hb` files remain in `/tmp` indefinitely.
3. **Partial Reads & Races**: Reading a file while a worker is partially writing can cause torn reads.

### Refined Architecture
SigCtl implements a dedicated POSIX Shared Memory heartbeat table:
- Created by supervisor with `shm_open()` + `mmap(MAP_SHARED)`.
- Each worker updates its 64-bit `uint64_t last_monotonic_ms` slot directly in memory every 500 ms.
- Zero disk I/O, zero open descriptor leaks, atomic 64-bit stores on modern x86_64, and guaranteed cleanup on supervisor exit via `shm_unlink()`.

---

## 3. Command FIFO Opened with `O_RDWR` to Prevent Spurious EOF

### Problem with `O_RDONLY` FIFOs
When a FIFO is opened by a reader with `O_RDONLY | O_NONBLOCK`, Linux `poll()` reports `POLLIN` / `POLLHUP` (EOF) immediately whenever a client finishes writing and closes the write end. This causes `poll()` to continuously wake up in a 100% CPU spinning loop.

### Refined Architecture
The supervisor opens the command FIFO with:
```c
s_fifo_fd = open(FIFO_FILE_DEFAULT, O_RDWR | O_NONBLOCK);
```
Because the supervisor itself holds a write descriptor on the FIFO, the reference count never drops to zero when external CLI clients disconnect, allowing `poll()` to block cleanly without spurious EOF wakes.

---

## 4. Worker Orphan Protection via `PR_SET_PDEATHSIG`

### Problem
If the supervisor process is abruptly terminated by `SIGKILL`, a hardware fault, or an out-of-memory killer, child worker processes would become orphaned and reparented to PID 1 (`init` or `systemd`), remaining running in the background indefinitely.

### Refined Architecture
Every worker calls:
```c
prctl(PR_SET_PDEATHSIG, SIGTERM);
if (getppid() != parent_before) {
    _exit(0);
}
```
If the supervisor dies for any reason, the Linux kernel immediately sends `SIGTERM` to all child workers, guaranteeing zero orphan processes under all crash conditions (verified in test `T15`).

---

## 5. POSIX `SIGTSTP` Self-Stop Protocol vs `SIGSTOP`

### Problem
`SIGSTOP` is uncatchable by user code; processes halted by `SIGSTOP` cannot log their state, flush buffers, or run custom suspension logic.

### Refined Architecture
The supervisor dispatches `SIGTSTP` to worker process groups:
1. The worker's `sigaction` handler catches `SIGTSTP` and sets a volatile flag.
2. The main worker loop logs `"Worker suspending on SIGTSTP"`.
3. The worker resets `SIGTSTP` disposition to `SIG_DFL` and re-raises `SIGTSTP` on itself:
   ```c
   sigreg_restore_default(SIGTSTP);
   raise(SIGTSTP);
   ```
4. This halts the worker through standard POSIX job control.
5. When resumed via `SIGCONT`, execution picks up immediately after `raise()`, reinstalls the handler, and logs resumption.

---

## 6. Summary Comparison Table

| Component | Initial Specification | SigCtl Refined Implementation | Rationale |
|---|---|---|---|
| **Timer Mechanism** | `alarm()` / `SIGALRM` | `timerfd` (CLOCK_MONOTONIC) | Eliminates process-wide timer collisions |
| **Worker Heartbeats** | Files in `/tmp/*.hb` | POSIX Shared Memory (`shm_open`) | Zero disk overhead, lock-free, atomic timestamps |
| **Command FIFO** | `O_RDONLY` | `O_RDWR \| O_NONBLOCK` | Prevents infinite `poll()` CPU-spinning loops on client EOF |
| **Orphan Protection** | None specified | `prctl(PR_SET_PDEATHSIG, SIGTERM)` | Prevents orphaned workers if supervisor crashes |
| **Process Suspension** | `SIGSTOP` | `SIGTSTP` reset-and-reraise | Allows workers to log state prior to suspension |
| **Startup Synchronization** | Unspecified / Sleep | Pipe handshake | Prevents race condition before `sigaction` completes |
