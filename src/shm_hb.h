#ifndef SIGFW_SHM_HB_H
#define SIGFW_SHM_HB_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "include/sigfw.h"

int shm_hb_create(const char *name, worker_heartbeat_t **out_addr);
int shm_hb_attach(const char *name, worker_heartbeat_t **out_addr);
void shm_hb_detach(worker_heartbeat_t *addr);
void shm_hb_destroy(const char *name, worker_heartbeat_t *addr);

#endif /* SIGFW_SHM_HB_H */
