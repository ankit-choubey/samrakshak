#ifndef SIGFW_CONFIG_H
#define SIGFW_CONFIG_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

typedef struct {
    int worker_count;
    int shutdown_timeout_s;
    int heartbeat_timeout_s;
    int max_restarts;
    int verbose;
} config_t;

void config_set_defaults(config_t *cfg);
int config_load(const char *filepath, config_t *cfg);
int config_validate(const config_t *cfg);

#endif /* SIGFW_CONFIG_H */
