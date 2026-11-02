#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "src/childtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/wait.h>

int main(void) {
    childtab_init(4);
    assert(childtab_capacity() == 4);
    assert(childtab_active_count() == 0);

    /* Allocate slot 0 */
    child_record_t *r0 = childtab_alloc_slot();
    assert(r0 != NULL);
    assert(r0->slot_index == 0);
    r0->pid = 1001;
    r0->pgid = 1001;
    r0->state = WORKER_STATE_RUNNING;
    assert(childtab_active_count() == 1);

    /* Allocate slot 1 */
    child_record_t *r1 = childtab_alloc_slot();
    assert(r1 != NULL);
    assert(r1->slot_index == 1);
    r1->pid = 1002;
    r1->pgid = 1002;
    r1->state = WORKER_STATE_RUNNING;
    assert(childtab_active_count() == 2);

    /* Find by PID */
    assert(childtab_find_by_pid(1001) == r0);
    assert(childtab_find_by_pid(1002) == r1);
    assert(childtab_find_by_pid(9999) == NULL);

    /* Update state */
    assert(childtab_set_state(1001, WORKER_STATE_STOPPED) == 0);
    assert(r0->state == WORKER_STATE_STOPPED);

    /* Record exit */
    assert(childtab_record_exit(1001, 0) == 0);
    assert(r0->state == WORKER_STATE_DEAD);
    assert(r0->exit_status == 0);
    assert(childtab_active_count() == 1);

    /* Test exponential backoff restart calculation */
    uint64_t now_ms = 10000;
    assert(childtab_record_restart(0, now_ms, 5) == 0);
    assert(r0->restart_count == 1);
    assert(r0->backoff_delay_ms == 100);

    assert(childtab_record_restart(0, now_ms + 100, 5) == 0);
    assert(r0->restart_count == 2);
    assert(r0->backoff_delay_ms == 200);

    assert(childtab_record_restart(0, now_ms + 300, 5) == 0);
    assert(r0->restart_count == 3);
    assert(r0->backoff_delay_ms == 400);

    assert(childtab_record_restart(0, now_ms + 700, 5) == 0);
    assert(r0->restart_count == 4);
    assert(r0->backoff_delay_ms == 800);

    assert(childtab_record_restart(0, now_ms + 1500, 5) == 0);
    assert(r0->restart_count == 5);
    assert(r0->backoff_delay_ms == 1600);

    /* Exceeds max restarts (5) -> FAILED */
    assert(childtab_record_restart(0, now_ms + 3100, 5) == -1);
    assert(r0->state == WORKER_STATE_FAILED);

    /* Free slot */
    childtab_free_slot(0);
    assert(r0->state == WORKER_STATE_EMPTY);

    printf("PASS: test_childtab completed successfully\n");
    return 0;
}
