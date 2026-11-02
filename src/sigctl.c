#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "include/sigfw.h"
#include "src/statefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>
#include <errno.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <status|pause|resume|reload|stop> [--json]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    int json_mode = (argc >= 3 && strcmp(argv[2], "--json") == 0);

    /* 1. Read supervisor PID */
    pid_t sup_pid = statefile_read_pidfile(PID_FILE_DEFAULT);
    if (sup_pid <= 0 || kill(sup_pid, 0) == -1) {
        fprintf(stderr, "Error: Supervisor is not running (PID file missing or process dead)\n");
        return SIGFW_EXIT_CLIENT_NOT_RUNNING; /* 2 */
    }

    /* 2. Execute command */
    if (strcmp(cmd, "pause") == 0) {
        if (kill(sup_pid, SIGTSTP) == -1) {
            perror("kill SIGTSTP failed");
            return 1;
        }
        printf("Sent SIGTSTP (pause) to supervisor (PID %d)\n", (int)sup_pid);
        return SIGFW_EXIT_CLEAN;
    } else if (strcmp(cmd, "resume") == 0) {
        if (kill(sup_pid, SIGCONT) == -1) {
            perror("kill SIGCONT failed");
            return 1;
        }
        printf("Sent SIGCONT (resume) to supervisor (PID %d)\n", (int)sup_pid);
        return SIGFW_EXIT_CLEAN;
    } else if (strcmp(cmd, "reload") == 0) {
        if (kill(sup_pid, SIGHUP) == -1) {
            perror("kill SIGHUP failed");
            return 1;
        }
        printf("Sent SIGHUP (reload) to supervisor (PID %d)\n", (int)sup_pid);
        return SIGFW_EXIT_CLEAN;
    } else if (strcmp(cmd, "stop") == 0) {
        if (kill(sup_pid, SIGTERM) == -1) {
            perror("kill SIGTERM failed");
            return 1;
        }
        printf("Sent SIGTERM (stop) to supervisor (PID %d)\n", (int)sup_pid);
        return SIGFW_EXIT_CLEAN;
    } else if (strcmp(cmd, "status") == 0) {
        char reply_fifo[128];
        snprintf(reply_fifo, sizeof(reply_fifo), "/tmp/sigctl_reply_%d.fifo", (int)getpid());
        unlink(reply_fifo);

        if (mkfifo(reply_fifo, 0600) < 0) {
            perror("mkfifo reply fifo failed");
            return 1;
        }

        /* Open reply fifo O_RDWR so poll doesn't get EOF before supervisor writes */
        int reply_fd = open(reply_fifo, O_RDWR | O_NONBLOCK);
        if (reply_fd < 0) {
            perror("open reply fifo failed");
            unlink(reply_fifo);
            return 1;
        }

        /* Send status request to command FIFO */
        int cmd_fd = open(FIFO_FILE_DEFAULT, O_WRONLY | O_NONBLOCK);
        if (cmd_fd < 0) {
            /* Fallback directly to status file if FIFO is busy */
            char status_buf[2048];
            if (statefile_read_status(STATUS_FILE_DEFAULT, status_buf, sizeof(status_buf)) == 0) {
                printf("%s", status_buf);
                close(reply_fd);
                unlink(reply_fifo);
                return SIGFW_EXIT_CLEAN;
            }
            fprintf(stderr, "Error opening command FIFO %s: %s\n", FIFO_FILE_DEFAULT, strerror(errno));
            close(reply_fd);
            unlink(reply_fifo);
            return 1;
        }

        char req[256];
        snprintf(req, sizeof(req), "status %s\n", reply_fifo);
        ssize_t w = write(cmd_fd, req, strlen(req));
        close(cmd_fd);
        (void)w;

        /* Poll reply FIFO with 2000 ms timeout */
        struct pollfd pfd;
        pfd.fd = reply_fd;
        pfd.events = POLLIN;

        int pret = poll(&pfd, 1, 2000);
        if (pret < 0) {
            perror("poll reply fifo failed");
            close(reply_fd);
            unlink(reply_fifo);
            return 1;
        } else if (pret == 0) {
            fprintf(stderr, "Timeout waiting for supervisor status response (2000ms)\n");
            close(reply_fd);
            unlink(reply_fifo);
            return SIGFW_EXIT_CLIENT_TIMEOUT; /* 4 */
        }

        char resp_buf[4096];
        ssize_t n = read(reply_fd, resp_buf, sizeof(resp_buf) - 1);
        close(reply_fd);
        unlink(reply_fifo);

        if (n <= 0) {
            fprintf(stderr, "Empty response from supervisor\n");
            return 1;
        }
        resp_buf[n] = '\0';

        if (json_mode) {
            /* Simple JSON format */
            printf("{\n  \"raw_status\": \"%s\"\n}\n", resp_buf);
        } else {
            printf("%s", resp_buf);
        }
        return SIGFW_EXIT_CLEAN;
    } else {
        fprintf(stderr, "Unknown command: '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
