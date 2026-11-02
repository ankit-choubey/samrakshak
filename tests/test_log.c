#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "src/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

int main(void) {
    const char *test_logfile = "tests/test_output.log";
    unlink(test_logfile);

    log_init(test_logfile, 0);

    log_msg(LOG_LEVEL_INFO, "Test INFO message %d", 42);
    log_msg(LOG_LEVEL_DEBUG, "Test DEBUG message should be suppressed");

    log_set_verbose(1);
    assert(log_get_verbose() == 1);
    log_msg(LOG_LEVEL_DEBUG, "Test DEBUG message when verbose");
    log_msg(LOG_LEVEL_WARN, "Test WARN message");
    log_msg(LOG_LEVEL_ERROR, "Test ERROR message");

    log_close();

    FILE *fp = fopen(test_logfile, "r");
    assert(fp != NULL);

    char line[512];
    int lines = 0;
    int found_pid = 0;
    int found_mono = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        lines++;
        if (strstr(line, "[PID:")) found_pid = 1;
        if (strstr(line, "s]")) found_mono = 1;
    }
    fclose(fp);
    unlink(test_logfile);

    assert(lines == 4); /* INFO, DEBUG (verbose), WARN, ERROR */
    assert(found_pid == 1);
    assert(found_mono == 1);

    printf("PASS: test_log completed successfully\n");
    return 0;
}
