#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "include/sigfw.h"
#include "src/log.h"
#include "src/sigreg.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <errno.h>

static volatile sig_atomic_t s_term_requested = 0;
static volatile sig_atomic_t s_tstp_received = 0;
static volatile sig_atomic_t s_cont_received = 0;
static volatile sig_atomic_t s_usr1_received = 0;
static volatile sig_atomic_t s_usr2_received = 0;

static int s_slot_index = -1;
static int s_sync_fd = -1;
static char s_shm_name[64] = {0};
static worker_heartbeat_t *s_heartbeats = NULL;

static void worker_term_handler(int sig) {
    (void)sig;
    s_term_requested = 1;
}

static void worker_tstp_handler(int sig) {
    (void)sig;
    s_tstp_received = 1;
}

static void worker_cont_handler(int sig) {
    (void)sig;
    s_cont_received = 1;
}

static void worker_usr1_handler(int sig) {
    (void)sig;
    s_usr1_received = 1;
}

static void worker_usr2_handler(int sig) {
    (void)sig;
    s_usr2_received = 1;
}

static uint64_t get_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void update_heartbeat(void) {
    if (s_heartbeats != NULL && s_slot_index >= 0 && s_slot_index < MAX_WORKERS) {
        s_heartbeats[s_slot_index].last_monotonic_ms = get_monotonic_ms();
        s_heartbeats[s_slot_index].pid = getpid();
        s_heartbeats[s_slot_index].active = 1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <slot_index> <sync_fd> [shm_name]\n", argv[0]);
        return 1;
    }

    s_slot_index = atoi(argv[1]);
    s_sync_fd = atoi(argv[2]);

    if (argc >= 4) {
        strncpy(s_shm_name, argv[3], sizeof(s_shm_name) - 1);
    }

    /* 1. Redirect stdin from /dev/null so workers never trigger SIGTTIN */
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }

    /* 2. Orphan protection: PR_SET_PDEATHSIG */
    pid_t parent_before = getppid();
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
        perror("prctl PR_SET_PDEATHSIG failed");
    }
    if (getppid() != parent_before) {
        /* Parent died before or during prctl */
        return 0;
    }

    /* 3. Attach shared memory heartbeat if name provided */
    if (strlen(s_shm_name) > 0) {
        int shm_fd = shm_open(s_shm_name, O_RDWR, 0600);
        if (shm_fd >= 0) {
            void *addr = mmap(NULL, sizeof(worker_heartbeat_t) * MAX_WORKERS,
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            if (addr != MAP_FAILED) {
                s_heartbeats = (worker_heartbeat_t *)addr;
            }
            close(shm_fd);
        }
    }

    /* 4. Reset signal mask and install async-signal-safe handlers */
    sigreg_unblock_all();

    sigreg_install(SIGTERM, worker_term_handler, 0, NULL);
    sigreg_install(SIGINT, worker_term_handler, 0, NULL);
    sigreg_install(SIGTSTP, worker_tstp_handler, 0, NULL);
    sigreg_install(SIGCONT, worker_cont_handler, 0, NULL);
    sigreg_install(SIGUSR1, worker_usr1_handler, 0, NULL);
    sigreg_install(SIGUSR2, worker_usr2_handler, 0, NULL);
    sigreg_ignore(SIGHUP);
    sigreg_ignore(SIGPIPE);

    update_heartbeat();

    /* 5. Signal parent via startup sync pipe */
    if (s_sync_fd >= 0) {
        char ready = '1';
        ssize_t ret = write(s_sync_fd, &ready, 1);
        (void)ret;
        close(s_sync_fd);
        s_sync_fd = -1;
    }

    log_msg(LOG_LEVEL_INFO, "Worker [slot:%d] initialized (PID:%d, PGID:%d)",
            s_slot_index, (int)getpid(), (int)getpgrp());

    int work_cycle = 0;
    while (!s_term_requested) {
        /* Check if SIGTSTP was received: log, unmask/reset to default, raise on self */
        if (s_tstp_received) {
            s_tstp_received = 0;
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] suspending on SIGTSTP",
                    s_slot_index, (int)getpid());

            sigreg_restore_default(SIGTSTP);
            raise(SIGTSTP);

            /* Resumed after SIGCONT */
            sigreg_install(SIGTSTP, worker_tstp_handler, 0, NULL);
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] un-halted after suspension",
                    s_slot_index, (int)getpid());
            update_heartbeat();
        }

        /* Check if SIGCONT was received */
        if (s_cont_received) {
            s_cont_received = 0;
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] resumed on SIGCONT",
                    s_slot_index, (int)getpid());
            update_heartbeat();
        }

        /* Check if SIGUSR1 was received (heartbeat status dump) */
        if (s_usr1_received) {
            s_usr1_received = 0;
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] heartbeat info: monotonic=%lu ms",
                    s_slot_index, (int)getpid(), (unsigned long)get_monotonic_ms());
        }

        /* Check if SIGUSR2 was received (toggle verbose) */
        if (s_usr2_received) {
            s_usr2_received = 0;
            int new_v = !log_get_verbose();
            log_set_verbose(new_v);
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] toggled verbose logging to %d",
                    s_slot_index, (int)getpid(), new_v);
        }

        update_heartbeat();

        /* Simulated work slice: 500 ms */
        struct timespec req = { 0, 500000000L };
        nanosleep(&req, NULL);
        work_cycle++;
    }

    log_msg(LOG_LEVEL_INFO, "Worker [slot:%d, PID:%d] graceful exit requested (SIGTERM)",
            s_slot_index, (int)getpid());

    if (s_heartbeats != NULL && s_slot_index >= 0 && s_slot_index < MAX_WORKERS) {
        s_heartbeats[s_slot_index].active = 0;
        munmap(s_heartbeats, sizeof(worker_heartbeat_t) * MAX_WORKERS);
    }

    return 0;
}
