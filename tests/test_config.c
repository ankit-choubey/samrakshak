#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "src/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int main(void) {
    config_t cfg;
    config_set_defaults(&cfg);

    assert(cfg.worker_count == 4);
    assert(cfg.shutdown_timeout_s == 5);
    assert(cfg.heartbeat_timeout_s == 5);
    assert(cfg.max_restarts == 5);
    assert(cfg.verbose == 0);

    /* 1. Valid config file test */
    const char *valid_cfg = "tests/test_valid.conf";
    FILE *fp = fopen(valid_cfg, "w");
    assert(fp != NULL);
    fprintf(fp, "# Test config\n");
    fprintf(fp, "  worker_count = 6 \n");
    fprintf(fp, "shutdown_timeout_s=10\n");
    fprintf(fp, "heartbeat_timeout_s = 8\n");
    fprintf(fp, "max_restarts = 3\n");
    fprintf(fp, "verbose = 1\n");
    fclose(fp);

    assert(config_load(valid_cfg, &cfg) == 0);
    assert(cfg.worker_count == 6);
    assert(cfg.shutdown_timeout_s == 10);
    assert(cfg.heartbeat_timeout_s == 8);
    assert(cfg.max_restarts == 3);
    assert(cfg.verbose == 1);
    unlink(valid_cfg);

    /* 2. Invalid range test */
    const char *invalid_cfg = "tests/test_invalid.conf";
    fp = fopen(invalid_cfg, "w");
    assert(fp != NULL);
    fprintf(fp, "worker_count = 100\n"); /* exceeds MAX_WORKERS (32) */
    fclose(fp);

    config_t old_cfg = cfg;
    assert(config_load(invalid_cfg, &cfg) == -1);
    /* Ensure old config was kept unchanged */
    assert(cfg.worker_count == old_cfg.worker_count);
    unlink(invalid_cfg);

    /* 3. Malformed syntax test */
    fp = fopen(invalid_cfg, "w");
    assert(fp != NULL);
    fprintf(fp, "worker_count_without_equals 4\n");
    fclose(fp);

    assert(config_load(invalid_cfg, &cfg) == -1);
    unlink(invalid_cfg);

    printf("PASS: test_config completed successfully\n");
    return 0;
}
