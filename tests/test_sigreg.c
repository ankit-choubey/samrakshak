#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "src/sigreg.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

static volatile sig_atomic_t s_got_sig = 0;

static void test_handler(int sig) {
    if (sig == SIGUSR1) {
        s_got_sig = 1;
    }
}

int main(void) {
    /* 1. Install handler */
    assert(sigreg_install(SIGUSR1, test_handler, 0, NULL) == 0);
    raise(SIGUSR1);
    assert(s_got_sig == 1);
    s_got_sig = 0;

    /* 2. Block signal */
    sigset_t set, oldset;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    assert(sigreg_block(&set, &oldset) == 0);

    raise(SIGUSR1);
    assert(s_got_sig == 0); /* signal is blocked */

    /* 3. Unblock signal */
    assert(sigreg_unblock(&set, NULL) == 0);
    assert(s_got_sig == 1); /* delivered upon unblock */
    s_got_sig = 0;

    /* 4. Ignore signal */
    assert(sigreg_ignore(SIGUSR1) == 0);
    raise(SIGUSR1);
    assert(s_got_sig == 0);

    /* 5. Reset all */
    assert(sigreg_reset_all_to_default() == 0);
    assert(sigreg_unblock_all() == 0);

    printf("PASS: test_sigreg completed successfully\n");
    return 0;
}
