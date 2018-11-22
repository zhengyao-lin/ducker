#ifndef _CORE_CGROUP_H_
#define _CORE_CGROUP_H_

#include "pub/clone.h"

typedef struct {
    char *resrc; // resource used(memory, cpu, etc.)
    char *var; // name of the variable
    char *val;
} cgroup_entry_t;

cgroup_entry_t *
cgroup_entry_copy(cgroup_entry_t *conf, size_t n);

void
cgroup_entry_free(cgroup_entry_t *conf, size_t n);

int
cgroup_init(cgroup_entry_t *conf, size_t n_conf, pid_t child);

int
cgroup_clean(cgroup_entry_t *conf, size_t n_conf, pid_t pid);

#endif
