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
#include <sys/wait.h>
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
static int s_running_loop = 1;
static uint64_t s_drain_deadline_ms = 0;
static int s_forced_shutdown = 0;
static const char *s_cfg_path = CONFIG_FILE_DEFAULT;

static char s_fifo_buf[512] = {0};
static size_t s_fifo_buf_len = 0;

static void reload_configuration(void);

static void sup_sig_handler(int sig) {
    uint8_t code = 0;
    switch (sig) {
        case SIGINT:  code = SIGCODE_INT;  break;
        case SIGTERM: code = SIGCODE_TERM; break;
        case SIGHUP:  code = SIGCODE_HUP;  break;
        case SIGCHLD: code = SIGCODE_CHLD; break;
        case SIGTSTP: code = SIGCODE_TSTP; break;
        case SIGCONT: code = SIGCODE_CONT; break;
        case SIGUSR1: code = SIGCODE_USR1; break;
        case SIGUSR2: code = SIGCODE_USR2; break;
        default: return;
    }
    selfpipe_write_signal(code);
}

static int setup_supervisor_signals(void) {
    sigreg_install(SIGINT, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGTERM, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGHUP, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGCHLD, sup_sig_handler, SA_RESTART | SA_NOCLDSTOP, NULL);
    sigreg_install(SIGTSTP, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGCONT, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGUSR1, sup_sig_handler, SA_RESTART, NULL);
    sigreg_install(SIGUSR2, sup_sig_handler, SA_RESTART, NULL);
    sigreg_ignore(SIGPIPE);
    return 0;
}

static int setup_ipc_and_timers(void) {
    unlink(FIFO_FILE_DEFAULT);
    if (mkfifo(FIFO_FILE_DEFAULT, 0666) < 0) {
        log_msg(LOG_LEVEL_ERROR, "mkfifo %s failed: %s", FIFO_FILE_DEFAULT, strerror(errno));
        return -1;
    }
    s_fifo_fd = open(FIFO_FILE_DEFAULT, O_RDWR | O_NONBLOCK);
    if (s_fifo_fd < 0) {
        log_msg(LOG_LEVEL_ERROR, "open FIFO %s failed: %s", FIFO_FILE_DEFAULT, strerror(errno));
        return -1;
    }

    s_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (s_timer_fd < 0) {
        log_msg(LOG_LEVEL_ERROR, "timerfd_create failed: %s", strerror(errno));
        return -1;
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_interval.tv_sec = 1;
    its.it_value.tv_sec = 1;
    if (timerfd_settime(s_timer_fd, 0, &its, NULL) < 0) {
        log_msg(LOG_LEVEL_ERROR, "timerfd_settime failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

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
        supervisor_state_t old_state = s_state;
        log_msg(LOG_LEVEL_INFO, "State transition: %s -> %s (reason: %s)",
                supervisor_state_str(s_state), supervisor_state_str(to), reason ? reason : "none");
        s_state = to;

        /* Write atomic status update */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

        if (to == STATE_SUSPENDED) {
            log_msg(LOG_LEVEL_INFO, "Entering SUSPENDED: sending SIGTSTP to all worker process groups");
            size_t cap = childtab_capacity();
            for (size_t i = 0; i < cap; i++) {
                child_record_t *rec = childtab_get(i);
                if (rec != NULL && rec->pid > 0 && rec->state == WORKER_STATE_RUNNING) {
                    kill(-rec->pgid, SIGTSTP);
                    rec->state = WORKER_STATE_STOPPED;
                }
            }
        } else if (to == STATE_RUNNING && old_state == STATE_SUSPENDED) {
            log_msg(LOG_LEVEL_INFO, "Resuming pool: sending SIGCONT to all stopped worker process groups");
            size_t cap = childtab_capacity();
            for (size_t i = 0; i < cap; i++) {
                child_record_t *rec = childtab_get(i);
                if (rec != NULL && rec->pid > 0 && rec->state == WORKER_STATE_STOPPED) {
                    kill(-rec->pgid, SIGCONT);
                    rec->state = WORKER_STATE_RUNNING;
                }
            }
        } else if (to == STATE_DRAINING) {
            s_drain_deadline_ms = now_ms + ((uint64_t)s_config.shutdown_timeout_s * 1000ULL);
            log_msg(LOG_LEVEL_INFO, "Entering DRAINING: dispatching SIGTERM to all worker process groups (deadline: %ds)",
                    s_config.shutdown_timeout_s);

            size_t cap = childtab_capacity();
            for (size_t i = 0; i < cap; i++) {
                child_record_t *rec = childtab_get(i);
                if (rec != NULL && rec->pid > 0 &&
                    (rec->state == WORKER_STATE_RUNNING || rec->state == WORKER_STATE_STOPPED)) {
                    if (rec->state == WORKER_STATE_STOPPED) {
                        kill(-rec->pgid, SIGCONT);
                    }
                    kill(-rec->pgid, SIGTERM);
                }
            }
        }

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

static void reap_children(void) {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child_record_t *rec = childtab_find_by_pid(pid);
        if (rec == NULL) {
            log_msg(LOG_LEVEL_WARN, "Reaped untracked child process PID:%d", (int)pid);
            continue;
        }

        childtab_record_exit(pid, status);

        if (WIFEXITED(status)) {
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%zu, PID:%d] exited with code %d",
                    rec->slot_index, (int)pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            log_msg(LOG_LEVEL_WARN, "Worker [slot:%zu, PID:%d] terminated by signal %d (%s)",
                    rec->slot_index, (int)pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
        }

        if (s_heartbeats != NULL && rec->slot_index < MAX_WORKERS) {
            s_heartbeats[rec->slot_index].active = 0;
        }

        if (s_state == STATE_RUNNING) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

            if (childtab_record_restart(rec->slot_index, now_ms, s_config.max_restarts) == 0) {
                log_msg(LOG_LEVEL_INFO, "Worker [slot:%zu] restart %d/%d scheduled in %lums",
                        rec->slot_index, rec->restart_count, s_config.max_restarts,
                        (unsigned long)rec->backoff_delay_ms);
                if (rec->backoff_delay_ms <= 100) {
                    spawn_worker(rec->slot_index);
                }
            } else {
                log_msg(LOG_LEVEL_ERROR, "Worker [slot:%zu] exceeded max restarts (%d), marked FAILED",
                        rec->slot_index, s_config.max_restarts);
            }
        } else if (s_state == STATE_DRAINING) {
            log_msg(LOG_LEVEL_INFO, "Worker [slot:%zu, PID:%d] reaped during DRAINING (%zu active remaining)",
                    rec->slot_index, (int)pid, childtab_active_count());
            if (childtab_active_count() == 0) {
                log_msg(LOG_LEVEL_INFO, "All workers reaped, entering TERMINATED");
                transition(STATE_TERMINATED, "all workers reaped");
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
        statefile_write_status(STATUS_FILE_DEFAULT, s_state,
                               childtab_capacity(), childtab_get(0),
                               s_heartbeats, now_ms);
    }
}

static void handle_timer_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

    /* Check any pending restarts under exponential backoff */
    if (s_state == STATE_RUNNING) {
        size_t cap = childtab_capacity();
        for (size_t i = 0; i < cap; i++) {
            child_record_t *rec = childtab_get(i);
            if (rec != NULL && rec->state == WORKER_STATE_DEAD) {
                if (rec->next_retry_mono_ms > 0 && now_ms >= rec->next_retry_mono_ms) {
                    rec->next_retry_mono_ms = 0;
                    log_msg(LOG_LEVEL_INFO, "Spawning delayed restart for worker [slot:%zu]", i);
                    spawn_worker(i);
                }
            }
        }
    } else if (s_state == STATE_DRAINING) {
        if (now_ms >= s_drain_deadline_ms) {
            log_msg(LOG_LEVEL_WARN, "Shutdown timeout (%ds) expired: escalating to SIGKILL", s_config.shutdown_timeout_s);
            s_forced_shutdown = 1;
            size_t cap = childtab_capacity();
            for (size_t i = 0; i < cap; i++) {
                child_record_t *rec = childtab_get(i);
                if (rec != NULL && rec->pid > 0 &&
                    (rec->state == WORKER_STATE_RUNNING || rec->state == WORKER_STATE_STOPPED)) {
                    log_msg(LOG_LEVEL_WARN, "Escalating SIGKILL to worker [slot:%zu, PID:%d, PGID:%d]",
                            i, (int)rec->pid, (int)rec->pgid);
                    kill(-rec->pgid, SIGKILL);
                }
            }
            reap_children();
            if (childtab_active_count() == 0) {
                transition(STATE_TERMINATED, "all workers reaped on deadline escalation");
            }
        }
    }
}

static void handle_dispatched_signal(uint8_t sigcode) {
    switch (sigcode) {
        case SIGCODE_INT:
        case SIGCODE_TERM:
            log_msg(LOG_LEVEL_INFO, "Supervisor received %s, initiating graceful shutdown",
                    sigcode == SIGCODE_INT ? "SIGINT" : "SIGTERM");
            transition(STATE_DRAINING, "termination signal received");
            break;
        case SIGCODE_TSTP:
            if (s_state != STATE_DRAINING) {
                log_msg(LOG_LEVEL_INFO, "Supervisor received SIGTSTP, suspending pool");
                transition(STATE_SUSPENDED, "SIGTSTP received");
            }
            break;
        case SIGCODE_CONT:
            if (s_state == STATE_SUSPENDED) {
                log_msg(LOG_LEVEL_INFO, "Supervisor received SIGCONT, resuming pool");
                transition(STATE_RUNNING, "SIGCONT received");
            }
            break;
        case SIGCODE_HUP:
            log_msg(LOG_LEVEL_INFO, "Supervisor received SIGHUP, reloading configuration");
            reload_configuration();
            break;
        case SIGCODE_CHLD:
            reap_children();
            break;
        case SIGCODE_USR1:
            log_msg(LOG_LEVEL_INFO, "Supervisor status dump (SIGUSR1): State=%s, Active Workers=%zu",
                    supervisor_state_str(s_state), childtab_active_count());
            break;
        case SIGCODE_USR2: {
            int new_v = !log_get_verbose();
            log_set_verbose(new_v);
            log_msg(LOG_LEVEL_INFO, "Supervisor toggled verbose logging to %d (SIGUSR2)", new_v);
            break;
        }
        default:
            break;
    }
}

static void reload_configuration(void) {
    config_t new_cfg = s_config;
    if (config_load(s_cfg_path, &new_cfg) != 0) {
        log_msg(LOG_LEVEL_ERROR, "Failed to reload config from %s: keeping existing configuration", s_cfg_path);
        return;
    }

    log_msg(LOG_LEVEL_INFO, "Configuration reloaded from %s (workers: %d -> %d)",
            s_cfg_path, s_config.worker_count, new_cfg.worker_count);

    if (s_state == STATE_RUNNING || s_state == STATE_SUSPENDED) {
        if (new_cfg.worker_count > s_config.worker_count) {
            size_t old_cap = (size_t)s_config.worker_count;
            size_t new_cap = (size_t)new_cfg.worker_count;
            childtab_set_capacity(new_cap);
            log_msg(LOG_LEVEL_INFO, "Scaling UP pool from %zu to %zu workers", old_cap, new_cap);
            for (size_t i = old_cap; i < new_cap; i++) {
                spawn_worker(i);
                if (s_state == STATE_SUSPENDED) {
                    child_record_t *rec = childtab_get(i);
                    if (rec && rec->pid > 0) {
                        kill(-rec->pgid, SIGTSTP);
                        rec->state = WORKER_STATE_STOPPED;
                    }
                }
            }
        } else if (new_cfg.worker_count < s_config.worker_count) {
            size_t old_cap = (size_t)s_config.worker_count;
            size_t new_cap = (size_t)new_cfg.worker_count;
            log_msg(LOG_LEVEL_INFO, "Scaling DOWN pool from %zu to %zu workers (terminating slots %zu to %zu)",
                    old_cap, new_cap, new_cap, old_cap - 1);
            for (size_t i = new_cap; i < old_cap; i++) {
                child_record_t *rec = childtab_get(i);
                if (rec != NULL && rec->pid > 0 &&
                    (rec->state == WORKER_STATE_RUNNING || rec->state == WORKER_STATE_STOPPED)) {
                    if (rec->state == WORKER_STATE_STOPPED) {
                        kill(-rec->pgid, SIGCONT);
                    }
                    kill(-rec->pgid, SIGTERM);
                }
            }
            childtab_set_capacity(new_cap);
        }
    }

    s_config = new_cfg;
    log_set_verbose(s_config.verbose);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    statefile_write_status(STATUS_FILE_DEFAULT, s_state,
                           childtab_capacity(), childtab_get(0),
                           s_heartbeats, now_ms);
}

static void process_fifo_command(const char *cmd_line) {
    char cmd[64] = {0};
    char arg[256] = {0};
    if (sscanf(cmd_line, "%63s %255s", cmd, arg) < 1) {
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        log_msg(LOG_LEVEL_INFO, "FIFO command: status (reply fifo: %s)", arg);
        if (strlen(arg) > 0) {
            int reply_fd = open(arg, O_WRONLY | O_NONBLOCK);
            if (reply_fd >= 0) {
                char status_buf[2048];
                if (statefile_read_status(STATUS_FILE_DEFAULT, status_buf, sizeof(status_buf)) == 0) {
                    ssize_t w = write(reply_fd, status_buf, strlen(status_buf));
                    (void)w;
                } else {
                    const char *err = "ERROR: Failed to read status file\n";
                    ssize_t w = write(reply_fd, err, strlen(err));
                    (void)w;
                }
                close(reply_fd);
            } else {
                log_msg(LOG_LEVEL_WARN, "Failed to open reply FIFO %s: %s", arg, strerror(errno));
            }
        }
    } else if (strcmp(cmd, "pause") == 0) {
        log_msg(LOG_LEVEL_INFO, "FIFO command: pause");
        transition(STATE_SUSPENDED, "FIFO pause command");
    } else if (strcmp(cmd, "resume") == 0) {
        log_msg(LOG_LEVEL_INFO, "FIFO command: resume");
        transition(STATE_RUNNING, "FIFO resume command");
    } else if (strcmp(cmd, "reload") == 0) {
        log_msg(LOG_LEVEL_INFO, "FIFO command: reload");
        reload_configuration();
    } else if (strcmp(cmd, "stop") == 0) {
        log_msg(LOG_LEVEL_INFO, "FIFO command: stop");
        transition(STATE_DRAINING, "FIFO stop command");
    } else {
        log_msg(LOG_LEVEL_WARN, "Unknown FIFO command: '%s'", cmd_line);
    }
}

static void handle_fifo_commands(void) {
    char read_buf[256];
    ssize_t n = read(s_fifo_fd, read_buf, sizeof(read_buf) - 1);
    if (n <= 0) return;

    for (ssize_t i = 0; i < n; i++) {
        char ch = read_buf[i];
        if (ch == '\n' || ch == '\r') {
            if (s_fifo_buf_len > 0) {
                s_fifo_buf[s_fifo_buf_len] = '\0';
                process_fifo_command(s_fifo_buf);
                s_fifo_buf_len = 0;
            }
        } else {
            if (s_fifo_buf_len < sizeof(s_fifo_buf) - 1) {
                s_fifo_buf[s_fifo_buf_len++] = ch;
            } else {
                s_fifo_buf_len = 0;
            }
        }
    }
}

static void run_event_loop(void) {
    struct pollfd fds[3];
    fds[0].fd = selfpipe_get_read_fd();
    fds[0].events = POLLIN;

    fds[1].fd = s_fifo_fd;
    fds[1].events = POLLIN;

    fds[2].fd = s_timer_fd;
    fds[2].events = POLLIN;

    while (s_running_loop) {
        int ret = poll(fds, 3, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            log_msg(LOG_LEVEL_ERROR, "poll failed: %s", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN) {
            uint8_t sig_buf[64];
            ssize_t n = selfpipe_read_signals(sig_buf, sizeof(sig_buf));
            for (ssize_t i = 0; i < n; i++) {
                handle_dispatched_signal(sig_buf[i]);
            }
        }

        if (fds[1].revents & POLLIN) {
            handle_fifo_commands();
        }

        if (fds[2].revents & POLLIN) {
            uint64_t exp = 0;
            ssize_t r = read(s_timer_fd, &exp, sizeof(exp));
            (void)r;
            handle_timer_tick();
        }

        if (s_state == STATE_TERMINATED) {
            s_running_loop = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc >= 2) {
        s_cfg_path = argv[1];
    }

    /* 1. Initialize Logging */
    log_init(LOG_FILE_DEFAULT, 0);
    log_msg(LOG_LEVEL_INFO, "=== SigCtl Supervisor starting (PID:%d) ===", (int)getpid());

    /* 2. Load Configuration */
    config_set_defaults(&s_config);
    if (config_load(s_cfg_path, &s_config) == 0) {
        log_msg(LOG_LEVEL_INFO, "Loaded config from %s: workers=%d shutdown_timeout=%ds hb_timeout=%ds max_restarts=%d verbose=%d",
                s_cfg_path, s_config.worker_count, s_config.shutdown_timeout_s,
                s_config.heartbeat_timeout_s, s_config.max_restarts, s_config.verbose);
    } else {
        log_msg(LOG_LEVEL_WARN, "Config load failed or not found for %s, using defaults", s_cfg_path);
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

    /* 7. Setup Signals and Timers */
    setup_supervisor_signals();
    if (setup_ipc_and_timers() < 0) {
        log_msg(LOG_LEVEL_ERROR, "Failed to setup IPC and timers");
        cleanup_resources();
        return SIGFW_EXIT_STARTUP_ERR;
    }

    /* 8. Spawn initial worker pool */
    spawn_initial_pool();

    log_msg(LOG_LEVEL_INFO, "Supervisor initialized successfully. Running event loop.");
    transition(STATE_RUNNING, "initial pool ready");

    /* Run main event loop */
    run_event_loop();

    cleanup_resources();
    return (s_forced_shutdown ? SIGFW_EXIT_FORCED : SIGFW_EXIT_CLEAN);
}
