#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "config.h"
#include "include/sigfw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void config_set_defaults(config_t *cfg) {
    if (cfg == NULL) return;
    cfg->worker_count = DEFAULT_WORKER_COUNT;
    cfg->shutdown_timeout_s = DEFAULT_SHUTDOWN_TIMEOUT_S;
    cfg->heartbeat_timeout_s = DEFAULT_HEARTBEAT_TIMEOUT_S;
    cfg->max_restarts = DEFAULT_MAX_RESTARTS;
    cfg->verbose = 0;
}

int config_validate(const config_t *cfg) {
    if (cfg == NULL) return -1;
    if (cfg->worker_count < 1 || cfg->worker_count > MAX_WORKERS) return -1;
    if (cfg->shutdown_timeout_s < 1 || cfg->shutdown_timeout_s > 60) return -1;
    if (cfg->heartbeat_timeout_s < 1 || cfg->heartbeat_timeout_s > 60) return -1;
    if (cfg->max_restarts < 0 || cfg->max_restarts > 100) return -1;
    return 0;
}

static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

int config_load(const char *filepath, config_t *cfg) {
    if (cfg == NULL) return -1;

    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        return -1;
    }

    config_t tmp = *cfg;
    char line[256];

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = trim_whitespace(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (eq == NULL) {
            fclose(fp);
            return -1;
        }

        *eq = '\0';
        char *key = trim_whitespace(p);
        char *val = trim_whitespace(eq + 1);

        if (strcmp(key, "worker_count") == 0) {
            tmp.worker_count = atoi(val);
        } else if (strcmp(key, "shutdown_timeout_s") == 0) {
            tmp.shutdown_timeout_s = atoi(val);
        } else if (strcmp(key, "heartbeat_timeout_s") == 0) {
            tmp.heartbeat_timeout_s = atoi(val);
        } else if (strcmp(key, "max_restarts") == 0) {
            tmp.max_restarts = atoi(val);
        } else if (strcmp(key, "verbose") == 0) {
            tmp.verbose = atoi(val);
        } else {
            /* Unknown key */
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    if (config_validate(&tmp) != 0) {
        return -1;
    }

    *cfg = tmp;
    return 0;
}
