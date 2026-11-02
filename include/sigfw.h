#ifndef SIGFW_H
#define SIGFW_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>

/* Exit codes defined by specification */
#define SIGFW_EXIT_CLEAN              0
#define SIGFW_EXIT_STARTUP_ERR        1
#define SIGFW_EXIT_CLIENT_NOT_RUNNING 2
#define SIGFW_EXIT_FORCED             3
#define SIGFW_EXIT_CLIENT_TIMEOUT     4

/* System limits & defaults */
#define DEFAULT_WORKER_COUNT          4
#define MAX_WORKERS                   32
#define DEFAULT_SHUTDOWN_TIMEOUT_S    5
#define DEFAULT_HEARTBEAT_TIMEOUT_S   5
#define DEFAULT_HEARTBEAT_INTERVAL_MS 500
#define DEFAULT_MAX_RESTARTS          5
#define RESTART_WINDOW_SEC            60

/* File paths */
#define PID_FILE_DEFAULT              "sigctld.pid"
#define STATUS_FILE_DEFAULT           "sigctld.status"
#define FIFO_FILE_DEFAULT             "sigctl.fifo"
#define CONFIG_FILE_DEFAULT           "sigctl.conf"
#define LOG_FILE_DEFAULT              "sigctl.log"

/* Supervisor State Machine Enum */
typedef enum {
    STATE_RUNNING = 0,
    STATE_SUSPENDED = 1,
    STATE_DRAINING = 2,
    STATE_TERMINATED = 3
} supervisor_state_t;

/* Worker Process State Enum */
typedef enum {
    WORKER_STATE_EMPTY = 0,
    WORKER_STATE_STARTING = 1,
    WORKER_STATE_RUNNING = 2,
    WORKER_STATE_STOPPED = 3,
    WORKER_STATE_DEAD = 4,
    WORKER_STATE_FAILED = 5
} worker_state_t;

/* Self-pipe 1-byte signal codes */
typedef enum {
    SIGCODE_INT  = 1,
    SIGCODE_TERM = 2,
    SIGCODE_HUP  = 3,
    SIGCODE_CHLD = 4,
    SIGCODE_TSTP = 5,
    SIGCODE_CONT = 6,
    SIGCODE_USR1 = 7,
    SIGCODE_USR2 = 8,
    SIGCODE_ALRM = 9
} sigcode_t;

/* Shared Memory Heartbeat slot */
typedef struct {
    uint64_t last_monotonic_ms;
    pid_t pid;
    int32_t active;
} worker_heartbeat_t;

/* Helper string convertors */
static inline const char *supervisor_state_str(supervisor_state_t s) {
    switch (s) {
        case STATE_RUNNING:    return "RUNNING";
        case STATE_SUSPENDED:  return "SUSPENDED";
        case STATE_DRAINING:   return "DRAINING";
        case STATE_TERMINATED: return "TERMINATED";
        default:               return "UNKNOWN";
    }
}

static inline const char *worker_state_str(worker_state_t s) {
    switch (s) {
        case WORKER_STATE_EMPTY:    return "EMPTY";
        case WORKER_STATE_STARTING: return "STARTING";
        case WORKER_STATE_RUNNING:  return "RUNNING";
        case WORKER_STATE_STOPPED:  return "STOPPED";
        case WORKER_STATE_DEAD:     return "DEAD";
        case WORKER_STATE_FAILED:   return "FAILED";
        default:                    return "UNKNOWN";
    }
}

/* Validates state machine transition */
static inline int is_valid_transition(supervisor_state_t from, supervisor_state_t to) {
    if (from == to) return 1; /* idempotent */
    if (from == STATE_RUNNING && to == STATE_SUSPENDED) return 1;
    if (from == STATE_SUSPENDED && to == STATE_RUNNING) return 1;
    if (from == STATE_RUNNING && to == STATE_DRAINING) return 1;
    if (from == STATE_SUSPENDED && to == STATE_DRAINING) return 1;
    if (from == STATE_DRAINING && to == STATE_TERMINATED) return 1;
    return 0;
}

#endif /* SIGFW_H */
