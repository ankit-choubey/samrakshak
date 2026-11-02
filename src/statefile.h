#ifndef SIGFW_STATEFILE_H
#define SIGFW_STATEFILE_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "include/sigfw.h"
#include "childtab.h"
#include <sys/types.h>
#include <stdint.h>

int statefile_lock_pidfile(const char *pid_filepath);
void statefile_unlock_pidfile(int lock_fd, const char *pid_filepath);
pid_t statefile_read_pidfile(const char *pid_filepath);

int statefile_write_status(const char *filepath,
                           supervisor_state_t state,
                           size_t count,
                           const child_record_t *records,
                           const worker_heartbeat_t *heartbeats,
                           uint64_t now_mono_ms);

int statefile_read_status(const char *filepath, char *buf, size_t max_len);

#endif /* SIGFW_STATEFILE_H */
