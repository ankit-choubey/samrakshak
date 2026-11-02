#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "include/sigfw.h"
#include "src/log.h"
#include "src/sigreg.h"
#include "src/selfpipe.h"
#include "src/childtab.h"
#include "src/statefile.h"
#include "src/config.h"
#include "src/shm_hb.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <poll.h>

/* Supervisor Global State */
static supervisor_state_t s_state = STATE_RUNNING;
static config_t s_config;
static int s_pid_lock_fd = -1;
static int s_fifo_fd = -1;
static int s_timer_fd = -1;
static char s_shm_name[64] = {0};
static worker_heartbeat_t *s_heartbeats = NULL;

static void cleanup_resources(void) {
    if (s_fifo_fd >= 0) {
        close(s_fifo_fd);
        s_fifo_fd = -1;
    }
    unlink(FIFO_FILE_DEFAULT);

    if (s_timer_fd >= 0) {
        close(s_timer_fd);
        s_timer_fd = -1;
    }

    selfpipe_close();

    if (s_heartbeats != NULL) {
        shm_hb_destroy(s_shm_name, s_heartbeats);
        s_heartbeats = NULL;
    }

    unlink(STATUS_FILE_DEFAULT);

    if (s_pid_lock_fd >= 0) {
        statefile_unlock_pidfile(s_pid_lock_fd, PID_FILE_DEFAULT);
        s_pid_lock_fd = -1;
    }

    log_msg(LOG_LEVEL_INFO, "Supervisor resources released cleanly");
    log_close();
}

int transition(supervisor_state_t to, const char *reason) {
    if (!is_valid_transition(s_state, to)) {
        log_msg(LOG_LEVEL_ERROR, "Illegal state transition attempted: %s -> %s (reason: %s)",
                supervisor_state_str(s_state), supervisor_state_str(to), reason ? reason : "unknown");
        return -1;
    }

    if (s_state != to) {
        log_msg(LOG_LEVEL_INFO, "State transition: %s -> %s (reason: %s)",
                supervisor_state_str(s_state), supervisor_state_str(to), reason ? reason : "none");
        s_state = to;

        /* Write atomic status update */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

        statefile_write_status(STATUS_FILE_DEFAULT, s_state,
                               childtab_capacity(), childtab_get(0),
                               s_heartbeats, now_ms);
    }
    return 0;
}

int spawn_worker(size_t slot_index) {
    if (slot_index >= childtab_capacity()) return -1;
    child_record_t *rec = childtab_get(slot_index);
    if (rec == NULL) return -1;

    int sync_pipe[2];
    if (pipe(sync_pipe) < 0) {
        log_msg(LOG_LEVEL_ERROR, "Failed to create sync pipe for worker slot %zu", slot_index);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_msg(LOG_LEVEL_ERROR, "Fork failed for worker slot %zu: %s", slot_index, strerror(errno));
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* In Child Process */
        close(sync_pipe[0]);
        setpgid(0, 0);

        char slot_buf[32];
        char sync_buf[32];
        snprintf(slot_buf, sizeof(slot_buf), "%zu", slot_index);
        snprintf(sync_buf, sizeof(sync_buf), "%d", sync_pipe[1]);

        execl("./worker", "worker", slot_buf, sync_buf, s_shm_name, (char *)NULL);
        perror("execl ./worker failed");
        _exit(127);
    }

    /* In Parent Supervisor */
    setpgid(pid, pid); /* Avoid race with child setpgid */
    close(sync_pipe[1]);

    /* Wait for child to finish signal registration */
    char ack = 0;
    ssize_t r = read(sync_pipe[0], &ack, 1);
    close(sync_pipe[0]);
    (void)r;

    rec->pid = pid;
    rec->pgid = pid;
    rec->state = WORKER_STATE_RUNNING;
    rec->start_time = time(NULL);

    log_msg(LOG_LEVEL_INFO, "Spawned worker [slot:%zu, PID:%d, PGID:%d]", slot_index, (int)pid, (int)pid);
    return 0;
}

void spawn_initial_pool(void) {
    size_t cap = childtab_capacity();
    for (size_t i = 0; i < cap; i++) {
        spawn_worker(i);
    }
}

int main(int argc, char *argv[]) {
    const char *cfg_path = CONFIG_FILE_DEFAULT;
    if (argc >= 2) {
        cfg_path = argv[1];
    }

    /* 1. Initialize Logging */
    log_init(LOG_FILE_DEFAULT, 0);
    log_msg(LOG_LEVEL_INFO, "=== SigCtl Supervisor starting (PID:%d) ===", (int)getpid());

    /* 2. Load Configuration */
    config_set_defaults(&s_config);
    if (config_load(cfg_path, &s_config) == 0) {
        log_msg(LOG_LEVEL_INFO, "Loaded config from %s: workers=%d shutdown_timeout=%ds hb_timeout=%ds max_restarts=%d verbose=%d",
                cfg_path, s_config.worker_count, s_config.shutdown_timeout_s,
                s_config.heartbeat_timeout_s, s_config.max_restarts, s_config.verbose);
    } else {
        log_msg(LOG_LEVEL_WARN, "Config load failed or not found for %s, using defaults", cfg_path);
    }
    log_set_verbose(s_config.verbose);

    /* 3. Acquire exclusive PID file lock */
    s_pid_lock_fd = statefile_lock_pidfile(PID_FILE_DEFAULT);
    if (s_pid_lock_fd < 0) {
        log_msg(LOG_LEVEL_ERROR, "Cannot acquire PID file lock on %s (another supervisor running?)", PID_FILE_DEFAULT);
        log_close();
        return SIGFW_EXIT_STARTUP_ERR;
    }

    /* 4. Initialize Child Table */
    childtab_init((size_t)s_config.worker_count);

    /* 5. Initialize Self-pipe */
    if (selfpipe_init() < 0) {
        log_msg(LOG_LEVEL_ERROR, "Failed to initialize self-pipe");
        cleanup_resources();
        return SIGFW_EXIT_STARTUP_ERR;
    }

    /* 6. Initialize Shared Memory Heartbeats */
    snprintf(s_shm_name, sizeof(s_shm_name), "/sigctl_hb_%d", (int)getpid());
    if (shm_hb_create(s_shm_name, &s_heartbeats) < 0) {
        log_msg(LOG_LEVEL_ERROR, "Failed to create shared memory for heartbeats");
        cleanup_resources();
        return SIGFW_EXIT_STARTUP_ERR;
    }

    log_msg(LOG_LEVEL_INFO, "Supervisor initialized successfully. Running pre-flight checks.");
    transition(STATE_RUNNING, "startup complete");

    cleanup_resources();
    return SIGFW_EXIT_CLEAN;
}
