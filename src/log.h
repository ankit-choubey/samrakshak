#ifndef SIGFW_LOG_H
#define SIGFW_LOG_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdarg.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

void log_init(const char *filepath, int verbose);
void log_close(void);
void log_set_verbose(int verbose);
int log_get_verbose(void);
void log_msg(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* SIGFW_LOG_H */
