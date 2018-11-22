#ifndef _CORE_CONTAINER_H_
#define _CORE_CONTAINER_H_

#include "bridge.h"
#include "cgroup.h"

typedef struct {
    char *tmp_dir; // template ending with XXXXXX
    char *host_name;
    char *nameserver;
    bridge_config_t *bridge_conf;

    cgroup_entry_t *cg_conf;
    size_t cg_n_conf;
} container_config_t;

typedef struct {
    char stack[4096];
} clone_stack_t;

typedef struct {
    clone_stack_t stack;
    
    container_config_t *conf;
    
    int pipe[2];

    char *tmp_dir;
} container_t;

container_config_t *
container_config_copy(const container_config_t *conf);

void
container_config_free(container_config_t *conf);

container_t *
container_new(const container_config_t *conf);

void
container_free(container_t *cont);

void
container_close_read(container_t *cont);

void
container_close_write(container_t *cont);

ssize_t
container_pipe_write(container_t *cont, const char *buf, size_t size);

ssize_t
container_pipe_read(container_t *cont, char *buf, size_t size);

int
container_run_image(container_t *cont, const char *img);

#endif
