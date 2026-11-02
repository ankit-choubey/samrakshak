#ifndef SIGFW_SELFPIPE_H
#define SIGFW_SELFPIPE_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

int selfpipe_init(void);
int selfpipe_get_read_fd(void);
int selfpipe_get_write_fd(void);
void selfpipe_write_signal(uint8_t sigcode);
ssize_t selfpipe_read_signals(uint8_t *buf, size_t max_count);
void selfpipe_close(void);

#endif /* SIGFW_SELFPIPE_H */
