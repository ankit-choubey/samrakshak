#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "statefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/file.h>
#include <time.h>

int statefile_lock_pidfile(const char *pid_filepath) {
    if (pid_filepath == NULL) return -1;

    int fd = open(pid_filepath, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }

    if (ftruncate(fd, 0) < 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    if (write(fd, buf, (size_t)len) != len) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    return fd;
}

void statefile_unlock_pidfile(int lock_fd, const char *pid_filepath) {
    if (pid_filepath != NULL) {
        unlink(pid_filepath);
    }
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
}

pid_t statefile_read_pidfile(const char *pid_filepath) {
    if (pid_filepath == NULL) return -1;

    FILE *fp = fopen(pid_filepath, "r");
    if (fp == NULL) return -1;

    int pid_val = 0;
    if (fscanf(fp, "%d", &pid_val) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return (pid_t)pid_val;
}

int statefile_write_status(const char *filepath,
                           supervisor_state_t state,
                           size_t count,
                           const child_record_t *records,
                           const worker_heartbeat_t *heartbeats,
                           uint64_t now_mono_ms) {
    if (filepath == NULL) return -1;

    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", filepath, (int)getpid());

    FILE *fp = fopen(tmp_path, "w");
    if (fp == NULL) {
        return -1;
    }

    time_t now_real = time(NULL);
    struct tm tm_buf;
    localtime_r(&now_real, &tm_buf);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(fp, "STATE=%s\n", supervisor_state_str(state));
    fprintf(fp, "SUPERVISOR_PID=%d\n", (int)getpid());
    fprintf(fp, "TIMESTAMP=%s\n", time_str);
    fprintf(fp, "WORKER_COUNT=%zu\n", count);

    for (size_t i = 0; i < count; i++) {
        if (records != NULL) {
            const child_record_t *rec = &records[i];
            uint64_t hb_age = 0;
            if (heartbeats != NULL && heartbeats[i].active && heartbeats[i].last_monotonic_ms > 0) {
                if (now_mono_ms >= heartbeats[i].last_monotonic_ms) {
                    hb_age = now_mono_ms - heartbeats[i].last_monotonic_ms;
                }
            }
            fprintf(fp, "WORKER[%zu]: PID=%d PGID=%d STATE=%s RESTARTS=%d HB_AGE_MS=%lu\n",
                    i, (int)rec->pid, (int)rec->pgid,
                    worker_state_str(rec->state),
                    rec->restart_count,
                    (unsigned long)hb_age);
        }
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        unlink(tmp_path);
        return -1;
    }
    fclose(fp);

    /* Atomic rename */
    if (rename(tmp_path, filepath) < 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int statefile_read_status(const char *filepath, char *buf, size_t max_len) {
    if (filepath == NULL || buf == NULL || max_len == 0) return -1;

    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) return -1;

    size_t total_read = fread(buf, 1, max_len - 1, fp);
    buf[total_read] = '\0';
    fclose(fp);
    return 0;
}
