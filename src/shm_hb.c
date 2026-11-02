#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "shm_hb.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define SHM_SIZE (sizeof(worker_heartbeat_t) * MAX_WORKERS)

int shm_hb_create(const char *name, worker_heartbeat_t **out_addr) {
    if (name == NULL || out_addr == NULL) return -1;

    /* If leftover from previous run, unlink */
    shm_unlink(name);

    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return -1;
    }

    if (ftruncate(fd, SHM_SIZE) < 0) {
        close(fd);
        shm_unlink(name);
        return -1;
    }

    void *addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        shm_unlink(name);
        return -1;
    }

    memset(addr, 0, SHM_SIZE);
    *out_addr = (worker_heartbeat_t *)addr;
    return 0;
}

int shm_hb_attach(const char *name, worker_heartbeat_t **out_addr) {
    if (name == NULL || out_addr == NULL) return -1;

    int fd = shm_open(name, O_RDWR, 0600);
    if (fd < 0) {
        return -1;
    }

    void *addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        return -1;
    }

    *out_addr = (worker_heartbeat_t *)addr;
    return 0;
}

void shm_hb_detach(worker_heartbeat_t *addr) {
    if (addr != NULL) {
        munmap(addr, SHM_SIZE);
    }
}

void shm_hb_destroy(const char *name, worker_heartbeat_t *addr) {
    if (addr != NULL) {
        munmap(addr, SHM_SIZE);
    }
    if (name != NULL) {
        shm_unlink(name);
    }
}
