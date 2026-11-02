#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "childtab.h"
#include <string.h>

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
