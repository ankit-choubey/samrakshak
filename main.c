#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define LOG_FILE "signal_logs.txt"

/* Signal flags for child process */
volatile sig_atomic_t sigint_received = 0;
volatile sig_atomic_t sigusr1_received = 0;
volatile sig_atomic_t sigterm_received = 0;

/* Simple signal handler for child process (sets flags only) */
void child_signal_handler(int sig) {
    if (sig == SIGINT) {
        sigint_received = 1;
    } else if (sig == SIGUSR1) {
        sigusr1_received = 1;
    } else if (sig == SIGTERM) {
        sigterm_received = 1;
    }
}

/* Append an event message with timestamp to signal_logs.txt */
void log_event(const char *event) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp == NULL) {
        perror("Error opening log file");
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    fprintf(fp, "[%s] %s\n", time_str, event);
    fclose(fp);
}

/* Read and display the contents of signal_logs.txt */
void view_logs(void) {
    FILE *fp = fopen(LOG_FILE, "r");
    if (fp == NULL) {
        printf("\nNo logs found or unable to open log file.\n");
        return;
    }

    printf("\n---------------------------------------------\n");
    printf("             SIGNAL EVENT LOGS\n");
    printf("---------------------------------------------\n");
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        printf("%s", line);
    }
    printf("---------------------------------------------\n");
    fclose(fp);
}

/* Child process continuous task and signal reaction loop */
void run_child_process(int sync_fd) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = child_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /* Register signal handlers for SIGINT, SIGUSR1, and SIGTERM */
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT failed");
        exit(1);
    }
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction SIGUSR1 failed");
        exit(1);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM failed");
        exit(1);
    }

    log_event("Child process started and registered signal handlers");

    /* Notify parent that signal registration is complete */
    if (sync_fd >= 0) {
        char ready = '1';
        if (write(sync_fd, &ready, 1) == -1) {
            perror("write sync pipe failed");
        }
        close(sync_fd);
    }

    int counter = 1;
    while (1) {
        /* Check if SIGINT was received */
        if (sigint_received) {
            sigint_received = 0;
            printf("\n[Child PID: %d] Handled SIGINT (Interrupt Signal)! Counter value: %d\n", getpid(), counter);
            fflush(stdout);
            log_event("Child process received and handled SIGINT");
        }

        /* Check if SIGUSR1 was received */
        if (sigusr1_received) {
            sigusr1_received = 0;
            printf("\n[Child PID: %d] Handled SIGUSR1 (Custom User Signal 1)!\n", getpid());
            fflush(stdout);
            log_event("Child process received and handled SIGUSR1");
        }

        /* Check if SIGTERM was received */
        if (sigterm_received) {
            printf("\n[Child PID: %d] Handled SIGTERM (Termination Signal).\n", getpid());
            printf("[Child PID: %d] Performing cleanup before graceful exit...\n", getpid());
            fflush(stdout);
            log_event("Child process received SIGTERM - Performing cleanup");

            /* Simulate resource cleanup */
            sleep(1);
            printf("[Child PID: %d] Cleanup completed. Terminating process.\n", getpid());
            fflush(stdout);
            log_event("Child process terminated gracefully");
            exit(0);
        }

        /* Continuous task: print counter every second */
        printf("[Child PID: %d] Counter: %d\n", getpid(), counter++);
        fflush(stdout);
        sleep(1);
    }
}

/* Update child status string using non-blocking waitpid */
void update_child_status(pid_t child_pid, char *status_str) {
    if (strcmp(status_str, "TERMINATED") == 0) {
        return;
    }

    int status;
    pid_t res = waitpid(child_pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

    if (res == child_pid) {
        if (WIFSTOPPED(status)) {
            strcpy(status_str, "STOPPED");
            log_event("Process state: Child is STOPPED");
        } else if (WIFCONTINUED(status)) {
            strcpy(status_str, "RUNNING");
            log_event("Process state: Child is RUNNING");
        } else if (WIFEXITED(status)) {
            strcpy(status_str, "TERMINATED");
            log_event("Process state: Child is TERMINATED (Normal exit)");
        } else if (WIFSIGNALED(status)) {
            strcpy(status_str, "TERMINATED");
            log_event("Process state: Child is TERMINATED (Killed by signal)");
        }
    } else if (res == -1 && errno == ECHILD) {
        strcpy(status_str, "TERMINATED");
    }
}

/* Display the interactive menu */
void display_menu(pid_t parent_pid, pid_t child_pid, const char *child_status) {
    printf("\n=============================================\n");
    printf("  LINUX SIGNAL HANDLING AND PROCESS CONTROL\n");
    printf("=============================================\n");
    printf("Parent PID: %d | Child PID: %d | Child Status: %s\n", parent_pid, child_pid, child_status);
    printf("---------------------------------------------\n");
    printf("1. Display Process Information\n");
    printf("2. Send SIGINT\n");
    printf("3. Send SIGUSR1\n");
    printf("4. Suspend Child Process (SIGSTOP)\n");
    printf("5. Resume Child Process (SIGCONT)\n");
    printf("6. Gracefully Terminate (SIGTERM)\n");
    printf("7. View Signal Event Logs\n");
    printf("8. Exit\n");
    printf("---------------------------------------------\n");
    printf("Enter your choice (1-8): ");
    fflush(stdout);
}

/* Safely read user menu choice */
int get_menu_choice(void) {
    char buffer[64];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        /* On EOF (e.g., Ctrl+D), exit cleanly instead of looping infinitely */
        return 8;
    }
    int choice = 0;
    if (sscanf(buffer, "%d", &choice) != 1) {
        return -1;
    }
    return choice;
}

int main(void) {
    /* Initialize log file */
    FILE *fp = fopen(LOG_FILE, "w");
    if (fp != NULL) {
        fprintf(fp, "=== LINUX SIGNAL HANDLING AND PROCESS CONTROL LOGS ===\n");
        fclose(fp);
    } else {
        perror("Error creating log file");
    }

    pid_t parent_pid = getpid();
    char child_status[20] = "RUNNING";

    /* Pipe for startup synchronization to ensure child completes sigaction before parent dispatches signals */
    int sync_pipe[2];
    if (pipe(sync_pipe) == -1) {
        perror("pipe failed");
        exit(1);
    }

    /* Create child process */
    pid_t child_pid = fork();

    if (child_pid < 0) {
        perror("fork failed");
        log_event("Fork failed - Unable to create child process");
        exit(1);
    }

    if (child_pid == 0) {
        /* In child process */
        close(sync_pipe[0]);
        run_child_process(sync_pipe[1]);
    } else {
        /* In parent process */
        close(sync_pipe[1]);
        char sync_buf;
        if (read(sync_pipe[0], &sync_buf, 1) == -1) {
            perror("read sync pipe failed");
        }
        close(sync_pipe[0]);

        char log_buf[128];
        snprintf(log_buf, sizeof(log_buf), "Parent created child process (Parent PID: %d, Child PID: %d)", parent_pid, child_pid);
        log_event(log_buf);

        /* Ignore SIGINT in parent using sigaction so terminal Ctrl+C does not kill the menu unexpectedly */
        struct sigaction sa_parent_int;
        memset(&sa_parent_int, 0, sizeof(sa_parent_int));
        sa_parent_int.sa_handler = SIG_IGN;
        sigemptyset(&sa_parent_int.sa_mask);
        sa_parent_int.sa_flags = 0;
        if (sigaction(SIGINT, &sa_parent_int, NULL) == -1) {
            perror("sigaction parent SIGINT ignore failed");
        }

        int choice = 0;
        while (1) {
            update_child_status(child_pid, child_status);
            display_menu(parent_pid, child_pid, child_status);
            choice = get_menu_choice();

            switch (choice) {
                case 1:
                    /* Display Process Information */
                    update_child_status(child_pid, child_status);
                    printf("\n---------------------------------------------\n");
                    printf("             PROCESS INFORMATION\n");
                    printf("---------------------------------------------\n");
                    printf("Parent PID           : %d\n", parent_pid);
                    printf("Child PID            : %d\n", child_pid);
                    printf("Parent Process State : RUNNING\n");
                    printf("Child Process State  : %s\n", child_status);
                    printf("---------------------------------------------\n");
                    break;

                case 2:
                    /* Send SIGINT */
                    if (strcmp(child_status, "TERMINATED") == 0) {
                        printf("\n[Parent] Error: Child process is already terminated.\n");
                        break;
                    }
                    if (kill(child_pid, SIGINT) == -1) {
                        perror("kill SIGINT failed");
                    } else {
                        printf("\n[Parent] Sent SIGINT to child process (PID: %d).\n", child_pid);
                        log_event("Parent sent SIGINT to child process");
                        usleep(150000);
                        update_child_status(child_pid, child_status);
                    }
                    break;

                case 3:
                    /* Send SIGUSR1 */
                    if (strcmp(child_status, "TERMINATED") == 0) {
                        printf("\n[Parent] Error: Child process is already terminated.\n");
                        break;
                    }
                    if (kill(child_pid, SIGUSR1) == -1) {
                        perror("kill SIGUSR1 failed");
                    } else {
                        printf("\n[Parent] Sent SIGUSR1 to child process (PID: %d).\n", child_pid);
                        log_event("Parent sent SIGUSR1 to child process");
                        usleep(150000);
                        update_child_status(child_pid, child_status);
                    }
                    break;

                case 4:
                    /* Suspend Child Process (SIGSTOP) */
                    if (strcmp(child_status, "TERMINATED") == 0) {
                        printf("\n[Parent] Error: Child process is already terminated.\n");
                        break;
                    }
                    if (strcmp(child_status, "STOPPED") == 0) {
                        printf("\n[Parent] Child process is already suspended.\n");
                        break;
                    }
                    if (kill(child_pid, SIGSTOP) == -1) {
                        perror("kill SIGSTOP failed");
                    } else {
                        printf("\n[Parent] Sent SIGSTOP to child process (PID: %d).\n", child_pid);
                        log_event("Parent sent SIGSTOP (Process suspended)");
                        usleep(50000);
                        update_child_status(child_pid, child_status);
                    }
                    break;

                case 5:
                    /* Resume Child Process (SIGCONT) */
                    if (strcmp(child_status, "TERMINATED") == 0) {
                        printf("\n[Parent] Error: Child process is already terminated.\n");
                        break;
                    }
                    if (strcmp(child_status, "RUNNING") == 0) {
                        printf("\n[Parent] Child process is already running.\n");
                        break;
                    }
                    if (kill(child_pid, SIGCONT) == -1) {
                        perror("kill SIGCONT failed");
                    } else {
                        printf("\n[Parent] Sent SIGCONT to child process (PID: %d).\n", child_pid);
                        log_event("Parent sent SIGCONT (Process resumed)");
                        usleep(50000);
                        update_child_status(child_pid, child_status);
                    }
                    break;

                case 6:
                    /* Gracefully Terminate (SIGTERM) */
                    if (strcmp(child_status, "TERMINATED") == 0) {
                        printf("\n[Parent] Child process is already terminated.\n");
                        break;
                    }
                    printf("\n[Parent] Sending SIGTERM to child process (PID: %d)...\n", child_pid);
                    log_event("Parent sent SIGTERM to child process");

                    if (kill(child_pid, SIGTERM) == -1) {
                        perror("kill SIGTERM failed");
                    } else {
                        /* If child was suspended, send SIGCONT so it can wake up and handle SIGTERM */
                        if (strcmp(child_status, "STOPPED") == 0) {
                            kill(child_pid, SIGCONT);
                        }
                        /* Wait for child to finish cleanup and terminate */
                        int status;
                        if (waitpid(child_pid, &status, 0) == -1) {
                            perror("waitpid failed");
                        }
                        strcpy(child_status, "TERMINATED");
                        printf("[Parent] Child process terminated gracefully and reaped.\n");
                        log_event("Parent reaped terminated child process");
                    }
                    break;

                case 7:
                    /* View Signal Event Logs */
                    view_logs();
                    break;

                case 8:
                    /* Exit */
                    if (strcmp(child_status, "TERMINATED") != 0) {
                        printf("\n[Parent] Terminating active child process (PID: %d) before exit...\n", child_pid);
                        kill(child_pid, SIGTERM);
                        if (strcmp(child_status, "STOPPED") == 0) {
                            kill(child_pid, SIGCONT);
                        }
                        waitpid(child_pid, NULL, 0);
                        log_event("Parent terminated child before exiting");
                    }
                    printf("[Parent] Exiting framework. Goodbye!\n");
                    log_event("Framework exited cleanly");
                    exit(0);

                default:
                    printf("\nInvalid choice! Please enter a number between 1 and 8.\n");
                    break;
            }
        }
    }

    return 0;
}
