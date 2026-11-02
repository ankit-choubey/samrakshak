#ifndef SIGFW_CHILDTAB_H
#define SIGFW_CHILDTAB_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "include/sigfw.h"
#include <sys/types.h>
#include <time.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    pid_t pid;
    pid_t pgid;
    worker_state_t state;
    size_t slot_index;
    int restart_count;
    time_t first_restart_time;
    uint64_t last_restart_mono_ms;
    uint64_t next_retry_mono_ms;
    uint64_t backoff_delay_ms;
    time_t start_time;
    int exit_status;
    int term_sig;
} child_record_t;

void childtab_init(size_t capacity);
size_t childtab_capacity(void);
void childtab_set_capacity(size_t new_cap);
size_t childtab_active_count(void);
child_record_t *childtab_get(size_t index);
child_record_t *childtab_find_by_pid(pid_t pid);
child_record_t *childtab_alloc_slot(void);
void childtab_free_slot(size_t index);
int childtab_set_state(pid_t pid, worker_state_t new_state);
int childtab_record_exit(pid_t pid, int status);
int childtab_record_restart(size_t index, uint64_t now_mono_ms, int max_restarts);

#endif /* SIGFW_CHILDTAB_H */
