# Linux Signal Handling and Process Control Framework

A lightweight, beginner-friendly Linux application written in C demonstrating process creation, signal registration, asynchronous signal handling, process suspension/resumption, and graceful process termination using standard POSIX system calls.

---

## 1. Project Overview

This project implements a **Parent-Child Process Architecture** to demonstrate how an operating system controls process execution and asynchronous events via signals:

- **Parent Process**: Forks the child, displays an interactive console menu, sends signals (`SIGINT`, `SIGUSR1`, `SIGSTOP`, `SIGCONT`, `SIGTERM`), monitors child execution states via `waitpid()`, and reaps the child on termination.
- **Child Process**: Runs a continuous counter task, registers signal handlers with `sigaction()`, safely handles asynchronous signals using `volatile sig_atomic_t` flags, performs cleanup operations, and handles process suspension and resumption.
- **Event Logger**: Automatically logs all process lifecycle events, signals sent, signals handled, state transitions, and termination timestamps to `signal_logs.txt`.

---

## 2. Project Structure

```
Linux-Signal-Framework/
|-- main.c              # Complete C source code
|-- Makefile            # Build configuration (make, make run, make clean)
|-- signal_logs.txt     # Log file recording all runtime signal events
|-- README.md           # Project documentation and guide
```

---

## 3. POSIX Signals Implemented

| Signal | Number | Purpose | Handled By | Action Taken |
| :--- | :--- | :--- | :--- | :--- |
| **`SIGINT`** | 2 | Interrupt Signal | Child (`sigaction`) | Caught asynchronously; prints message and counter value without terminating. |
| **`SIGUSR1`** | 10 | User-Defined Signal 1 | Child (`sigaction`) | Custom user signal; logs and acknowledges receipt. |
| **`SIGSTOP`** | 19 | Process Suspension | Linux Kernel (Uncatchable) | Immediately halts child execution; verified by parent using `WIFSTOPPED`. |
| **`SIGCONT`** | 18 | Process Resumption | Linux Kernel (Uncatchable) | Resumes suspended child; verified by parent using `WIFCONTINUED`. |
| **`SIGTERM`** | 15 | Graceful Termination | Child (`sigaction`) | Triggers simulated cleanup (releasing resources) and calls `exit(0)`. |

> **Note on Async-Signal-Safety**: In adherence to POSIX safety standards, signal handlers in this project do not perform unsafe I/O operations (like `printf` or `fopen`). Instead, handlers set small `volatile sig_atomic_t` flags, which the child process checks and acts upon safely in its primary loop.

---

## 4. POSIX System Calls Used

1. **`fork()`**: Creates a duplicate child process. Returns `0` to child and child PID to parent.
2. **`getpid()` / `getppid()`**: Retrieves the current process ID and parent process ID.
3. **`sigaction()`**: Registers custom signal handling dispositions with flags and signal masks.
4. **`kill(pid, sig)`**: Dispatches POSIX signals from parent to child.
5. **`waitpid(pid, status, options)`**: Monitors state transitions of child without blocking (`WNOHANG | WUNTRACED | WCONTINUED`).
6. **`sleep()` / `usleep()`**: Pauses execution; interrupted immediately upon signal arrival (`EINTR`).

---

## 5. Compilation and Execution

### Compilation
Compile using `make` (recommended) or `gcc` on any Linux/Ubuntu or POSIX-compliant system:
```bash
# Using Makefile
make

# Or using gcc directly
gcc -Wall -Wextra main.c -o signal_framework
```

### Execution
Run the compiled executable directly or via `make`:
```bash
./signal_framework

# Or:
make run
```

---

## 6. Menu Options

```
=============================================
  LINUX SIGNAL HANDLING AND PROCESS CONTROL
=============================================
Parent PID: [PID] | Child PID: [PID] | Child Status: [RUNNING/STOPPED/TERMINATED]
---------------------------------------------
1. Display Process Information
2. Send SIGINT
3. Send SIGUSR1
4. Suspend Child Process (SIGSTOP)
5. Resume Child Process (SIGCONT)
6. Gracefully Terminate (SIGTERM)
7. View Signal Event Logs
8. Exit
---------------------------------------------
```

---

## 7. Step-by-Step Demonstration Workflow

Follow these steps during testing or lab evaluation:

1. **Launch Framework**: Run `./signal_framework`. Notice parent and child PIDs displayed, child counter starts (`1, 2, 3...`).
2. **Send SIGUSR1 (Option 3)**: Dispatches user-defined signal. Child prints `Handled SIGUSR1!`.
3. **Send SIGINT (Option 2)**: Dispatches interrupt signal. Child prints `Handled SIGINT!` and continues running.
4. **Suspend Child Process (Option 4)**: Sends `SIGSTOP`. Notice child counter stops printing immediately.
5. **Verify Stopped State (Option 1)**: Select option 1. Status displays `Child Process State: STOPPED`.
6. **Resume Child Process (Option 5)**: Sends `SIGCONT`. Notice child counter immediately resumes incrementing.
7. **Gracefully Terminate Child (Option 6)**: Sends `SIGTERM`. Child performs cleanup routine and exits cleanly. Parent reaps child using `waitpid()`.
8. **Verify Terminated State (Option 1)**: Select option 1. Status displays `Child Process State: TERMINATED`. Attempting to send further signals displays a safe error message.
9. **View Logs (Option 7)**: Displays all recorded events with timestamps from `signal_logs.txt`.
10. **Exit (Option 8)**: Cleans up active child (if any) and exits parent cleanly.

---

## 8. Common Errors and Solutions

- **Error: `kill failed: No such process`**
  - *Cause*: Child process already exited and parent attempted to send signal.
  - *Fix*: The framework automatically verifies status before sending signals and blocks operations on terminated processes.
- **Zombie Process Creation**
  - *Cause*: Child terminates but parent does not read its exit status with `wait()` / `waitpid()`.
  - *Fix*: Framework uses `waitpid()` in non-blocking and blocking reap routines, ensuring no zombie processes remain.
- **Orphan Process Creation**
  - *Cause*: Parent exits while child is still active.
  - *Fix*: Option 8 sends `SIGTERM` and reaps the child before parent process termination.
