#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "sigreg.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

int sigreg_install(int signum, void (*handler)(int), int flags, const sigset_t *mask) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = flags;

    if (mask != NULL) {
        sa.sa_mask = *mask;
    } else {
        sigemptyset(&sa.sa_mask);
    }

    if (sigaction(signum, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_ignore(int signum) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(signum, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_restore_default(int signum) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(signum, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_block(const sigset_t *set, sigset_t *oldset) {
    if (sigprocmask(SIG_BLOCK, set, oldset) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_unblock(const sigset_t *set, sigset_t *oldset) {
    if (sigprocmask(SIG_UNBLOCK, set, oldset) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_set_mask(const sigset_t *set, sigset_t *oldset) {
    if (sigprocmask(SIG_SETMASK, set, oldset) == -1) {
        return -1;
    }
    return 0;
}

int sigreg_reset_all_to_default(void) {
    int sigs[] = {
        SIGINT, SIGTERM, SIGHUP, SIGCHLD,
        SIGTSTP, SIGCONT, SIGUSR1, SIGUSR2,
        SIGALRM, SIGPIPE
    };
    size_t count = sizeof(sigs) / sizeof(sigs[0]);

    for (size_t i = 0; i < count; i++) {
        if (sigreg_restore_default(sigs[i]) == -1) {
            return -1;
        }
    }
    return 0;
}

int sigreg_unblock_all(void) {
    sigset_t empty;
    sigemptyset(&empty);
    return sigreg_set_mask(&empty, NULL);
}
