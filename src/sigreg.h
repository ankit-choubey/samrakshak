#ifndef SIGFW_SIGREG_H
#define SIGFW_SIGREG_H

#define _POSIX_C_SOURCE 200809L

#include <signal.h>

int sigreg_install(int signum, void (*handler)(int), int flags, const sigset_t *mask);
int sigreg_ignore(int signum);
int sigreg_restore_default(int signum);
int sigreg_block(const sigset_t *set, sigset_t *oldset);
int sigreg_unblock(const sigset_t *set, sigset_t *oldset);
int sigreg_set_mask(const sigset_t *set, sigset_t *oldset);
int sigreg_reset_all_to_default(void);
int sigreg_unblock_all(void);

#endif /* SIGFW_SIGREG_H */
