#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "src/selfpipe.h"
#include "src/sigreg.h"
#include "include/sigfw.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

static void handler(int sig) {
    if (sig == SIGUSR1) {
        selfpipe_write_signal(SIGCODE_USR1);
    } else if (sig == SIGUSR2) {
        selfpipe_write_signal(SIGCODE_USR2);
    }
}

int main(void) {
    assert(selfpipe_init() == 0);
    assert(selfpipe_get_read_fd() >= 0);
    assert(selfpipe_get_write_fd() >= 0);

    /* Verify non-blocking read returns 0 on empty pipe */
    uint8_t buf[16];
    assert(selfpipe_read_signals(buf, sizeof(buf)) == 0);

    /* Install signal handler */
    assert(sigreg_install(SIGUSR1, handler, 0, NULL) == 0);
    assert(sigreg_install(SIGUSR2, handler, 0, NULL) == 0);

    /* Dispatch signals */
    raise(SIGUSR1);
    raise(SIGUSR2);
    raise(SIGUSR1);

    /* Read signals */
    ssize_t n = selfpipe_read_signals(buf, sizeof(buf));
    assert(n == 3);
    assert(buf[0] == SIGCODE_USR1);
    assert(buf[1] == SIGCODE_USR2);
    assert(buf[2] == SIGCODE_USR1);

    /* Pipe should now be empty */
    assert(selfpipe_read_signals(buf, sizeof(buf)) == 0);

    selfpipe_close();
    printf("PASS: test_selfpipe completed successfully\n");
    return 0;
}
