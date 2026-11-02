#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "childtab.h"
#include <string.h>
#include <sys/wait.h>
#include <time.h>

static child_record_t s_child_table[MAX_WORKERS];
static size_t s_capacity = DEFAULT_WORKER_COUNT;

void childtab_init(size_t capacity) {
    if (capacity > MAX_WORKERS) {
        capacity = MAX_WORKERS;
    }
    s_capacity = capacity;
    memset(s_child_table, 0, sizeof(s_child_table));

    for (size_t i = 0; i < MAX_WORKERS; i++) {
        s_child_table[i].slot_index = i;
        s_child_table[i].state = WORKER_STATE_EMPTY;
    }
}

size_t childtab_capacity(void) {
    return s_capacity;
}

void childtab_set_capacity(size_t new_cap) {
    if (new_cap > MAX_WORKERS) {
        new_cap = MAX_WORKERS;
    }
    s_capacity = new_cap;
}

size_t childtab_active_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < s_capacity; i++) {
        if (s_child_table[i].state == WORKER_STATE_RUNNING ||
            s_child_table[i].state == WORKER_STATE_STOPPED ||
            s_child_table[i].state == WORKER_STATE_STARTING) {
            count++;
        }
    }
    return count;
}

child_record_t *childtab_get(size_t index) {
    if (index >= s_capacity) {
        return NULL;
    }
    return &s_child_table[index];
}

child_record_t *childtab_find_by_pid(pid_t pid) {
    if (pid <= 0) return NULL;
    for (size_t i = 0; i < s_capacity; i++) {
        if (s_child_table[i].pid == pid && s_child_table[i].state != WORKER_STATE_EMPTY) {
            return &s_child_table[i];
        }
    }
    return NULL;
}

child_record_t *childtab_alloc_slot(void) {
    for (size_t i = 0; i < s_capacity; i++) {
        if (s_child_table[i].state == WORKER_STATE_EMPTY ||
            s_child_table[i].state == WORKER_STATE_DEAD) {
            return &s_child_table[i];
        }
    }
    return NULL;
}

void childtab_free_slot(size_t index) {
    if (index >= s_capacity) return;
    memset(&s_child_table[index], 0, sizeof(child_record_t));
    s_child_table[index].slot_index = index;
    s_child_table[index].state = WORKER_STATE_EMPTY;
}

int childtab_set_state(pid_t pid, worker_state_t new_state) {
    child_record_t *rec = childtab_find_by_pid(pid);
    if (rec == NULL) return -1;
    rec->state = new_state;
    return 0;
}

int childtab_record_exit(pid_t pid, int status) {
    child_record_t *rec = childtab_find_by_pid(pid);
    if (rec == NULL) return -1;

    rec->state = WORKER_STATE_DEAD;
    if (WIFEXITED(status)) {
        rec->exit_status = WEXITSTATUS(status);
        rec->term_sig = 0;
    } else if (WIFSIGNALED(status)) {
        rec->exit_status = 128 + WTERMSIG(status);
        rec->term_sig = WTERMSIG(status);
    } else {
        rec->exit_status = -1;
        rec->term_sig = 0;
    }
    return 0;
}

int childtab_record_restart(size_t index, uint64_t now_mono_ms, int max_restarts) {
    if (index >= s_capacity) return -1;
    child_record_t *rec = &s_child_table[index];

    time_t now_real = time(NULL);
    if (rec->first_restart_time == 0 || (now_real - rec->first_restart_time) > RESTART_WINDOW_SEC) {
        rec->first_restart_time = now_real;
        rec->restart_count = 1;
        rec->backoff_delay_ms = 100;
    } else {
        rec->restart_count++;
        if (rec->backoff_delay_ms == 0) {
            rec->backoff_delay_ms = 100;
        } else {
            rec->backoff_delay_ms *= 2;
            if (rec->backoff_delay_ms > 2000) {
                rec->backoff_delay_ms = 2000;
            }
        }
    }

    rec->last_restart_mono_ms = now_mono_ms;
    rec->next_retry_mono_ms = now_mono_ms + rec->backoff_delay_ms;

    if (rec->restart_count > max_restarts) {
        rec->state = WORKER_STATE_FAILED;
        return -1;
    }

    return 0;
}
