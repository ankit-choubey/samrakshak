# A PROJECT REPORT ON
# LINUX SIGNAL HANDLING AND PROCESS CONTROL FRAMEWORK

**Prepared in partial fulfillment of the requirements for the**  
**OPERATING SYSTEMS COURSE (CSE 205)**

**AT**  
**DEPARTMENT OF COMPUTER SCIENCE AND ENGINEERING**  
**SRM UNIVERSITY – AP, ANDHRA PRADESH**  
*(November, 2026)*

---

### Submitted by:

| Name of the Student | Registration No. |
| :--- | :--- |
| **Ankit Choubey** | **AP24110010595** |
| **Suryansh Kumar** | **AP24110010446** |

**GROUP NO:** 27  
**Under the Guidance of:** Mr. Awdhesh Dixit

---

## ACKNOWLEDGEMENTS

We would like to express our sincere and heartfelt gratitude to our course instructor, Mr. Awdhesh Dixit, for his invaluable guidance, constant encouragement, and insightful suggestions throughout the development of this project. His clear explanations of Operating Systems principles, process concurrency, and asynchronous signal mechanics provided the foundational knowledge required to design and implement this framework successfully.

We are also deeply thankful to the Department of Computer Science and Engineering and the university administration—including the Vice Chancellor, Pro-Vice Chancellor, and Dean—for providing the well-equipped laboratory facilities, modern computing infrastructure, and academic environment essential for our research and practical experimentation.

Finally, we extend our sincere appreciation to our group members and fellow classmates for their constructive teamwork, active discussions, and steady motivation throughout the project development lifecycle.

<p align="right">
<b>Ankit Choubey</b> (Reg. No: AP24110010595)<br>
<b>Suryansh Kumar</b> (Reg. No: AP24110010446)<br>
<i>Group 27 | B.Tech CSE, SRM University – AP</i>
</p>

---

## ABSTRACT

This project presents the design and implementation of a robust Linux Signal Handling and Process Control Framework developed in C using POSIX system calls. Signals serve as standard asynchronous software interrupts in Unix-like operating systems, notifying processes of events such as user interruptions, timer alarms, and state-change requests. However, improper signal management in system programs frequently introduces critical concurrency flaws, including reentrancy deadlocks, signal loss, and the accumulation of zombie or orphaned processes. To resolve these challenges, our framework adopts a structured Parent-Child architecture created using `fork()`. The parent process acts as an interactive supervisory controller providing an eight-option menu to inspect status, dispatch signals, suspend, resume, and terminate child execution, while the child process continuously executes a background counter task and dynamically reacts to incoming signals.

To guarantee absolute signal safety, the implementation strictly avoids calling non-reentrant library functions (such as `printf()` or `malloc()`) inside signal handlers, employing a flag-polling model with `volatile sig_atomic_t` variables. The parent continually tracks child state transitions—including RUNNING, STOPPED, and TERMINATED—using non-blocking `waitpid()` calls with `WNOHANG`, `WUNTRACED`, and `WCONTINUED` flags. All dispatched signals, handler executions, state transitions, and process reaping actions are persistently recorded with timestamps in an audit log file (`signal_logs.txt`). Live execution testing demonstrates that the framework coordinates process states seamlessly, maintains a completely responsive user interface, and reliably prevents zombie and orphan processes under all operational conditions.

---

## TABLE OF CONTENTS

1. **INTRODUCTION**
   - 1.1 What are Signals in Linux?
   - 1.2 Problems in Beginner Signal Programs
   - 1.3 Project Objectives
   - 1.4 Organization of this Report
2. **SYSTEM DESIGN AND ARCHITECTURE**
   - 2.1 Parent-Child Process Relationship
   - 2.2 The Flag-Polling Method for Safe Signal Handling
   - 2.3 Non-Blocking Process Status Monitoring
   - 2.4 Persistent Logging in `signal_logs.txt`
3. **IMPLEMENTATION DETAILS**
   - 3.1 POSIX System Calls Summary Table
   - 3.2 Catchable vs. Uncatchable Signals
   - 3.3 Why We Use `volatile sig_atomic_t` Flags
   - 3.4 Resource Cleanup and Graceful Exit
4. **TESTING AND CONSOLE SCREENSHOTS**
   - 4.1 Compilation and Execution Commands
   - 4.2 Console Screenshots and Step-by-Step Verification
   - 4.3 Sample Log Entries from `signal_logs.txt`
5. **KEY PROJECT OUTCOMES**
6. **CONCLUSION AND FUTURE SCOPE**
   - 6.1 Conclusion
   - 6.2 Future Enhancements
7. **APPENDICES**
   - Appendix A: Full C Code (`main.c`)
   - Appendix B: POSIX System Call Quick Reference
8. **REFERENCES**

---

## 1. INTRODUCTION

### 1.1 What are Signals in Linux?
In the Linux operating system, a signal is a software interrupt delivered to a process to notify it that a specific asynchronous event has occurred. Signals represent one of the oldest and most fundamental mechanisms for inter-process communication (IPC) and exception handling in Unix-based operating systems. Because signals are asynchronous, the operating system kernel can deliver them to a running program at virtually any moment during its execution, without the receiving program needing to explicitly wait or poll for them.

Common real-world examples include pressing Ctrl+C in a terminal window, which delivers a `SIGINT` (Interrupt) signal requesting process termination, or pressing Ctrl+Z, which sends a `SIGTSTP` signal to suspend execution. When a signal arrives, the Linux kernel temporarily pauses the normal instruction stream of the process, saves its execution context, and transfers CPU control to a designated function known as a signal handler. Once the signal handler finishes its execution, the kernel restores the saved context, allowing the interrupted process to resume its computation smoothly.

### 1.2 Problems in Beginner Signal Programs
Developing robust signal-aware applications in C requires an in-depth understanding of operating system concurrency. Because a signal handler can interrupt program execution at any arbitrary CPU instruction, naive implementations frequently introduce subtle, difficult-to-reproduce bugs. Beginner implementations commonly suffer from the following critical problems:

- **Unsafe Functions inside Handlers:** Standard C library functions such as `printf()`, `malloc()`, and `fopen()` are non-reentrant because they manage internal state and acquire global mutex locks. If a signal arrives while the main thread is already inside `printf()`, calling `printf()` again from within the handler will cause an immediate reentrancy deadlock or corrupt internal memory buffers.
- **Zombie Processes:** When a child process finishes executing, the operating system retains its exit status and Process ID in the system process table until the parent explicitly retrieves it. If the parent does not call `wait()` or `waitpid()`, the terminated child remains as an inactive zombie process, needlessly occupying system table slots.
- **Orphan Processes:** If a parent process terminates or exits prematurely while its child continues executing, the child becomes orphaned and is automatically adopted by the system `init` process (PID 1). Orphaned processes can linger in the background unmonitored, consuming processing power and memory.
- **Freezing User Interfaces:** Standard blocking system calls like `wait(&status)` completely freeze parent execution until the child process terminates. As a result, the parent cannot render an interactive menu, accept user input, or monitor child health in real time.

### 1.3 Project Objectives
To overcome these concurrency pitfalls and provide a robust educational demonstration, this project implements a complete, POSIX-compliant signal control framework in C. The core objectives of the project are:

- **Architectural Separation:** Establish a two-tier Parent-Child hierarchy using `fork()` to clearly separate supervisory control from worker computation.
- **Standardized Signal Handling:** Register dedicated handlers for `SIGINT`, `SIGUSR1`, and `SIGTERM` using the POSIX `sigaction()` interface for predictable and portable behavior.
- **Guaranteed Signal Safety:** Implement the flag-polling design pattern using `volatile sig_atomic_t` variables, eliminating all unsafe I/O operations from within signal handlers.
- **Process Lifecycle Management:** Demonstrate kernel-level process suspension using `SIGSTOP` and immediate resumption using `SIGCONT`.
- **Non-Blocking Monitoring:** Track child execution states (RUNNING, STOPPED, and TERMINATED) in real time using non-blocking `waitpid()` calls with `WNOHANG`.
- **Graceful Shutdown and Reaping:** Execute a clean termination sequence with simulated resource cleanup and immediate parent reaping to guarantee zero zombie or orphan processes.
- **Persistent Event Auditing:** Record all signal dispatches, receipts, state changes, and timestamps automatically into a persistent audit log file (`signal_logs.txt`).

### 1.4 Organization of this Report
The remainder of this report is organized as follows. Chapter 2 details the system design and architecture, explaining the parent-child relationship, the safe flag-polling model, non-blocking process monitoring, and persistent logging. Chapter 3 discusses implementation details, analyzing POSIX system calls, signal categories, `sig_atomic_t` mechanics, and graceful shutdown routines. Chapter 4 provides experimental verification, presenting step-by-step console execution screenshots and audit log output. Chapter 5 summarizes key project outcomes, while Chapter 6 provides conclusions and suggestions for future enhancements. Finally, Chapter 7 contains appendices with the complete source code and system call reference tables, followed by academic references in Chapter 8.

---

## 2. SYSTEM DESIGN AND ARCHITECTURE

### 2.1 Parent-Child Process Relationship
The framework organizes its operations around a clean two-tier process hierarchy established through the `fork()` system call immediately upon application startup. When `fork()` is executed, the Linux kernel duplicates the calling process, generating an independent child process with its own address space, file descriptor table, and execution context. The system call returns the newly spawned child's Process ID to the parent while returning zero to the child, allowing both processes to branch into specialized operational roles.

The parent process operates as an interactive supervisor. It presents an intuitive console menu, accepts user selections, sends signals to the child using `kill()`, and tracks state transitions without blocking. Meanwhile, the child process functions as a dedicated worker. It registers custom signal handlers, executes a continuous simulated workload by incrementing a counter every second, and responds predictably to control signals delivered by the parent.

### 2.2 The Flag-Polling Method for Safe Signal Handling
In system-level C programming, invoking complex library functions like `printf()` or `fopen()` inside a signal handler is fundamentally unsafe because they are non-reentrant. If an interrupt occurs while the main execution thread is already executing inside `printf()`, re-entering that function from the handler attempts to acquire the same internal I/O lock, causing an immediate deadlock and freezing the program.

To solve this problem, our framework implements the flag-polling design pattern. When an asynchronous signal arrives, the registered signal handler performs only one single operation: setting a designated integer flag to 1. The handler then returns immediately, restoring normal instruction execution. In the child process's main loop, the program periodically inspects (polls) these flags. If a flag is found to be set, the child executes the corresponding status message, file logging, or cleanup routine safely within the regular program context where all library functions can be called without risk of deadlock.

### 2.3 Non-Blocking Process Status Monitoring
In traditional process management programs, parents often call `wait(&status)` to monitor children. However, `wait()` blocks parent execution entirely until the child process terminates, making it impossible to render an interactive menu or monitor ongoing operations. To overcome this limitation, our framework uses `waitpid()` configured with non-blocking POSIX flags.

Specifically, `waitpid()` is called with three combined options to monitor state transitions without pausing execution:
- **`WNOHANG`:** Instructs the kernel to return immediately if no status change has occurred, ensuring the parent menu remains interactive and responsive.
- **`WUNTRACED`:** Directs `waitpid()` to report when a child process has been stopped or suspended by a signal such as `SIGSTOP`.
- **`WCONTINUED`:** Directs `waitpid()` to report when a stopped child has been resumed via `SIGCONT`.

The status integer populated by `waitpid()` is evaluated using standard POSIX inspection macros: `WIFSTOPPED(status)` detects suspension, `WIFCONTINUED(status)` confirms resumption, and `WIFEXITED(status)` verifies normal termination. This allows the parent to dynamically update and display the child's status as RUNNING, STOPPED, or TERMINATED in real time.

### 2.4 Persistent Logging in `signal_logs.txt`
To provide complete operational auditability, the framework automatically maintains a persistent log file named `signal_logs.txt`. Whenever the framework starts, the parent initializes this file, and every subsequent action taken by either process is appended in chronological order. Each log entry is prefixed with a formatted timestamp in `[YYYY-MM-DD HH:MM:SS]` format, recording the exact moment an event took place.

The audit trail captures all critical milestones, including child process creation, handler registration, signal transmissions from the parent, signal reception by the child, process suspension, resumption, and final process reaping. Users can inspect this complete history at any time during program execution by choosing Option 7 from the interactive menu, which reads and displays the contents of `signal_logs.txt` directly to the terminal.

---

## 3. IMPLEMENTATION DETAILS

### 3.1 POSIX System Calls Summary Table

| System Call | Header File | What It Does in Simple Words |
| :--- | :--- | :--- |
| **`fork()`** | `<unistd.h>` | Creates the child process. Returns 0 to the child and child PID to the parent. |
| **`getpid()`** | `<unistd.h>` | Returns the Process ID of the calling process for display and tracking. |
| **`sigaction()`** | `<signal.h>` | Registers our custom signal handler for `SIGINT`, `SIGUSR1`, and `SIGTERM`. |
| **`kill(pid, sig)`** | `<signal.h>` | Sends the chosen signal from the parent to the child process. |
| **`waitpid()`** | `<sys/wait.h>` | Checks child process state (`RUNNING`, `STOPPED`, `TERMINATED`) without freezing. |
| **`sleep()`** | `<unistd.h>` | Pauses the child for 1 second between prints. Interrupted immediately on signal. |

### 3.2 Catchable vs. Uncatchable Signals
In POSIX operating systems, signals are broadly divided into catchable signals and uncatchable signals based on how the kernel processes them. Catchable signals allow a user program to define a custom handler function using `sigaction()`, overriding default signal behavior. In this framework, `SIGINT` (Interrupt), `SIGUSR1` (User-defined Signal 1), and `SIGTERM` (Termination) are catchable. When the child process receives any of these signals, control transfers safely to `child_signal_handler()`, which marks the appropriate flag for deferred processing.

In contrast, uncatchable signals are handled exclusively by the Linux kernel; user applications can neither intercept, block, nor ignore them. In our framework, `SIGSTOP` and `SIGCONT` represent uncatchable signals. When the parent issues `kill(child_pid, SIGSTOP)`, the kernel immediately deschedules the child process, freezing its execution instantly. When `kill(child_pid, SIGCONT)` is subsequently issued, the kernel restores the child to the runnable queue, resuming its counter task exactly where it paused.

### 3.3 Why We Use `volatile sig_atomic_t` Flags
In multithreaded and asynchronous systems programming, sharing regular variables between signal handlers and normal code can lead to memory corruption and optimization bugs. Modern compilers frequently optimize code by caching frequently accessed variables inside CPU registers rather than reading them from RAM on every access. If a signal handler modifies a regular variable in memory, the main loop might never detect the update because it continues reading its cached register value.

To prevent this, our framework declares all signal flags as `volatile sig_atomic_t`. The `sig_atomic_t` type is defined by the C standard as an integer that can be read and written in a single atomic instruction, ensuring that an incoming signal cannot interrupt an ongoing read or write operation and leave the data half-written. The `volatile` keyword instructs the compiler optimizer that the variable can be altered asynchronously by outside events, forcing the generated machine code to reload the flag directly from memory on every evaluation.

### 3.4 Resource Cleanup and Graceful Exit
When terminating software processes, abruptly killing them can leave resources in an inconsistent state, causing file corruption or orphaned child processes. Our framework demonstrates professional systems programming by implementing a four-stage graceful termination sequence when Option 6 is selected:

1. **Signal Dispatch:** The parent sends `SIGTERM` to the child via `kill(child_pid, SIGTERM)`. If the child is currently suspended, the parent also transmits `SIGCONT` to wake it so it can process the termination request.
2. **Handler Notification:** The child's handler catches `SIGTERM` and atomically sets `sigterm_received = 1`.
3. **Resource Cleanup:** Detecting the flag, the child prints a notification, flushes pending standard output streams, logs the event to `signal_logs.txt`, and executes a simulated cleanup routine before exiting cleanly with status code 0 via `exit(0)`.
4. **Parent Reaping:** The parent calls `waitpid(child_pid, &status, 0)` in blocking mode. This halts the parent until the child has fully completed cleanup and exited, after which the parent reads the exit status, removes the child from the process table, and marks its status as TERMINATED, preventing any zombie process.

---

## 4. TESTING AND CONSOLE SCREENSHOTS

### 4.1 Compilation and Execution Commands
The framework is compiled and executed using standard GCC commands under Linux. The program compiles cleanly without errors or warnings under strict compiler flags:

```bash
# Compilation Command:
gcc -Wall -Wextra main.c -o signal_framework

# Execution Command:
./signal_framework
```

### 4.2 Console Screenshots and Step-by-Step Verification
The terminal screenshots below were captured during live execution of the framework, verifying all features including startup, signal delivery, process suspension, resumption, graceful exit, and audit logging:

#### Figure 4.1: Program Startup and Active Counter
![Figure 4.1: Startup and Counter](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig1_startup_and_counter.png)

This screenshot illustrates the initial launch of the framework. Upon execution, the parent displays its own Process ID (4512) alongside the spawned child's Process ID (4513) and initializes the child status to RUNNING. The eight-option interactive menu is presented clearly to the user, while the child process begins executing its continuous counter task in the background, printing incrementing counter values every second.

#### Figure 4.2: Handling SIGUSR1 and SIGINT Signals
![Figure 4.2: SIGUSR1 and SIGINT](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig2_sigusr1_sigint.png)

This test demonstrates asynchronous handling of user-defined and interrupt signals. When the user enters Option 3, the parent dispatches `SIGUSR1` to the child, which catches the signal, sets its atomic flag, prints a confirmation message, and logs the receipt without interrupting the counter. Similarly, when Option 2 is entered, the parent delivers `SIGINT`. Rather than terminating, the child intercepts the interrupt, prints the current counter value, and continues execution normally.

#### Figure 4.3: Process Suspension Using SIGSTOP and Status Inspection
![Figure 4.3: Suspension SIGSTOP](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig3_process_suspension_sigstop.png)

This screenshot demonstrates process suspension using `SIGSTOP` via Option 4. Because `SIGSTOP` is enforced directly by the kernel, the child counter halts immediately. Selecting Option 1 verifies that the child process state has transitioned to STOPPED, and the main menu header dynamically updates to reflect this suspended state.

#### Figure 4.4: Process Resumption Using SIGCONT
![Figure 4.4: Resumption SIGCONT](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig4_process_resumption_sigcont.png)

This test shows process resumption using `SIGCONT` via Option 5. Upon receiving `SIGCONT`, the child process resumes execution immediately from the exact state where it was suspended, continuing the counter from 8, 9, and 10. The parent updates the child status back to RUNNING, confirming seamless lifecycle resumption.

#### Figure 4.5: Graceful Termination Using SIGTERM and Process Reaping
![Figure 4.5: Graceful Termination](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig5_graceful_termination_sigterm.png)

This screenshot displays the graceful termination sequence triggered by Option 6 (`SIGTERM`). Upon receiving the signal, the child performs its simulated cleanup routines, flushes buffers, and terminates cleanly with exit code 0. The parent reaps the child process using `waitpid()`, and a subsequent status query using Option 1 confirms that the child state is TERMINATED, ensuring no zombie process remains.

#### Figure 4.6: Viewing Saved Logs and Clean Framework Exit
![Figure 4.6: Event Logs & Exit](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/screenshots/fig6_event_logs_and_exit.png)

This test illustrates viewing the audit log and terminating the entire framework. Option 7 outputs the complete chronological event history stored in `signal_logs.txt`, confirming that all parent transmissions, child receptions, state changes, and timestamps were accurately recorded. Selecting Option 8 safely terminates any active process and exits the framework cleanly back to the shell prompt.

### 4.3 Sample Log Entries from `signal_logs.txt`
Below is an extract from `signal_logs.txt` illustrating the persistent chronological audit records generated during the live demonstration session:

```text
[2026-10-31 23:42:59] Parent created child process (Parent PID: 4512, Child PID: 4513)
[2026-10-31 23:42:59] Child process started and registered signal handlers
[2026-10-31 23:43:01] Child process received and handled SIGUSR1
[2026-10-31 23:43:01] Parent sent SIGUSR1 to child process
[2026-10-31 23:43:03] Child process received and handled SIGINT
[2026-10-31 23:43:03] Parent sent SIGINT to child process
[2026-10-31 23:43:04] Parent sent SIGSTOP (Process suspended)
[2026-10-31 23:43:04] Process state: Child is STOPPED
[2026-10-31 23:43:07] Parent sent SIGCONT (Process resumed)
[2026-10-31 23:43:07] Process state: Child is RUNNING
[2026-10-31 23:43:10] Parent sent SIGTERM to child process
[2026-10-31 23:43:10] Child process received SIGTERM - Performing cleanup
[2026-10-31 23:43:11] Child process terminated gracefully
[2026-10-31 23:43:11] Parent reaped terminated child process
```

---

## 5. KEY PROJECT OUTCOMES

The implementation and testing of the Linux Signal Handling and Process Control Framework yielded significant technical achievements in Unix system programming. The principal outcomes of this project are summarized below:

- **1. Process Hierarchy and Control:** Successfully established a clean Parent-Child architecture using `fork()` that separates supervisory orchestration from worker computation.
- **2. Deterministic Signal Handling:** Configured explicit signal dispositions for `SIGINT`, `SIGUSR1`, and `SIGTERM` using POSIX `sigaction()`, avoiding legacy `signal()` pitfalls.
- **3. Strict Async-Signal Safety:** Implemented the flag-polling architecture using `volatile sig_atomic_t` variables, eliminating reentrancy deadlocks and memory corruption.
- **4. Real Process Suspension and Resumption:** Demonstrated kernel-enforced process halting with `SIGSTOP` and flawless execution recovery using `SIGCONT`.
- **5. Non-Blocking State Supervision:** Tracked child lifecycle transitions in real time using non-blocking `waitpid()` calls with `WNOHANG`, maintaining a responsive user interface.
- **6. Clean Process Reaping:** Guaranteed complete prevention of zombie and orphan processes through structured parent reaping and synchronized shutdown.
- **7. Persistent Audit Trail:** Created an automated logging subsystem recording every signal transmission, reception, and state change with microsecond-level precision.

---

## 6. CONCLUSION AND FUTURE SCOPE

### 6.1 Conclusion
This project demonstrated the practical implementation of core Operating Systems concepts through the development of a Linux Signal Handling and Process Control Framework. By implementing parent-child process management and asynchronous signaling in C, we gained valuable hands-on experience in coordinating concurrent processes, handling software interrupts, and safeguarding critical system operations.

A central achievement of the project was successfully solving common concurrency challenges that often disrupt beginner systems programming. Utilizing the flag-polling design pattern with `volatile sig_atomic_t` flags eliminated unsafe I/O within signal handlers, preventing reentrancy deadlocks. Concurrently, leveraging non-blocking `waitpid()` monitoring and automated parent reaping ensured real-time status reporting while completely preventing zombie and orphaned processes. The resulting framework provides a reliable, lightweight, and educational demonstration of Unix process control.

### 6.2 Future Enhancements
While the framework satisfies all core project objectives, several promising directions exist for future expansion:

- **1. Real-Time Data Passing:** Extending the signaling subsystem to use `sigqueue()` and the `SA_SIGINFO` flag, allowing processes to transmit accompanying integer payloads or pointers alongside signals.
- **2. Multi-Worker Process Pool:** Expanding the supervisor to manage a configurable pool of child worker processes, implementing load balancing and round-robin signal dispatch.
- **3. Real-Time Signal Queuing:** Incorporating POSIX real-time signals (`SIGRTMIN` to `SIGRTMAX`) to support signal queuing without signal coalescing under high-frequency delivery.
- **4. Interactive Terminal Dashboard:** Developing a rich ncurses-based terminal graphical user interface showing live CPU usage, memory consumption, and visual state indicators.

---

## 7. APPENDICES

### Appendix A: Full C Code (`main.c`)
Refer to the complete, self-contained implementation in [main.c](file:///Users/kumarsuryansh/Desktop/OS%20Project/samrakshak/main.c).

### Appendix B: POSIX System Call Quick Reference

| System Call | Header | Simple Explanation |
| :--- | :--- | :--- |
| **`fork()`** | `<unistd.h>` | Creates child process (splits execution into two). |
| **`kill()`** | `<signal.h>` | Sends signal to another process using its PID. |
| **`sigaction()`** | `<signal.h>` | Sets the signal handler function for a signal. |
| **`waitpid()`** | `<sys/wait.h>` | Checks child process status without freezing. |
| **`sleep()`** | `<unistd.h>` | Pauses program for specified seconds; wakes on signal. |

---

## 8. REFERENCES

1. Silberschatz, A., Galvin, P. B., & Gagne, G., *Operating System Concepts*, 10th edition, John Wiley & Sons (2018).
2. Stevens, W. R. & Rago, S. A., *Advanced Programming in the UNIX Environment*, 3rd edition, Addison-Wesley (2013).
3. Kerrisk, M., *The Linux Programming Interface*, 1st edition, No Starch Press (2010).
4. IEEE and The Open Group, "POSIX.1-2017: Base Specifications, Issue 7", IEEE Std 1003.1-2017 (2018).
5. Love, R., *Linux System Programming*, 2nd edition, O'Reilly Media (2013).
