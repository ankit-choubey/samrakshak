#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "selfpipe.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int s_pipe_fds[2] = { -1, -1 };

int selfpipe_init(void) {
    if (s_pipe_fds[0] != -1) {
        return 0; /* already initialized */
    }

    if (pipe2(s_pipe_fds, O_NONBLOCK | O_CLOEXEC) == -1) {
        return -1;
    }
    return 0;
}

int selfpipe_get_read_fd(void) {
    return s_pipe_fds[0];
}

int selfpipe_get_write_fd(void) {
    return s_pipe_fds[1];
}

void selfpipe_write_signal(uint8_t sigcode) {
    int saved_errno = errno;
    if (s_pipe_fds[1] != -1) {
        /* Write single byte into non-blocking pipe. If pipe is full, ignore error. */
        ssize_t ret = write(s_pipe_fds[1], &sigcode, 1);
        (void)ret;
    }
    errno = saved_errno;
}

ssize_t selfpipe_read_signals(uint8_t *buf, size_t max_count) {
    if (s_pipe_fds[0] == -1 || buf == NULL || max_count == 0) {
        return -1;
    }

    ssize_t n = read(s_pipe_fds[0], buf, max_count);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; /* No data currently available */
        }
        return -1;
    }
    return n;
}

void selfpipe_close(void) {
    if (s_pipe_fds[0] != -1) {
        close(s_pipe_fds[0]);
        s_pipe_fds[0] = -1;
    }
    if (s_pipe_fds[1] != -1) {
        close(s_pipe_fds[1]);
        s_pipe_fds[1] = -1;
    }
}
