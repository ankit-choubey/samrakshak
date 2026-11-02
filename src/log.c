#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>

static FILE *s_log_fp = NULL;
static int s_verbose = 0;
static struct timespec s_start_mono;

void log_init(const char *filepath, int verbose) {
    s_verbose = verbose;
    clock_gettime(CLOCK_MONOTONIC, &s_start_mono);

    if (filepath != NULL && strlen(filepath) > 0) {
        s_log_fp = fopen(filepath, "a");
        if (s_log_fp == NULL) {
            perror("fopen log file failed");
        }
    }
}

void log_close(void) {
    if (s_log_fp != NULL && s_log_fp != stdout && s_log_fp != stderr) {
        fclose(s_log_fp);
        s_log_fp = NULL;
    }
}

void log_set_verbose(int verbose) {
    s_verbose = verbose;
}

int log_get_verbose(void) {
    return s_verbose;
}

void log_msg(int level, const char *fmt, ...) {
    if (level == LOG_LEVEL_DEBUG && !s_verbose) {
        return;
    }

    struct timespec now_mono;
    clock_gettime(CLOCK_MONOTONIC, &now_mono);

    double elapsed = (double)(now_mono.tv_sec - s_start_mono.tv_sec) +
                     (double)(now_mono.tv_nsec - s_start_mono.tv_nsec) / 1e9;

    time_t now_real = time(NULL);
    struct tm tm_buf;
    localtime_r(&now_real, &tm_buf);
    char wall_str[32];
    strftime(wall_str, sizeof(wall_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *lvl_str = "INFO";
    if (level == LOG_LEVEL_DEBUG) lvl_str = "DEBUG";
    else if (level == LOG_LEVEL_WARN) lvl_str = "WARN";
    else if (level == LOG_LEVEL_ERROR) lvl_str = "ERROR";

    pid_t pid = getpid();

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (s_log_fp != NULL) {
        fprintf(s_log_fp, "[%s | +%8.3fs] [PID:%d] [%s] %s\n",
                wall_str, elapsed, (int)pid, lvl_str, buf);
        fflush(s_log_fp);
    }

    fprintf(stdout, "[%s | +%8.3fs] [PID:%d] [%s] %s\n",
            wall_str, elapsed, (int)pid, lvl_str, buf);
    fflush(stdout);
}
