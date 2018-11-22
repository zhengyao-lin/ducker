#ifndef _CORE_BRIDGE_H_
#define _CORE_BRIDGE_H_

#include "pub/type.h"
#include "pub/mount.h"
#include "pub/clone.h"

typedef struct {
    char *host_ip;
    char *cont_ip;
    bool use_physical;
} bridge_config_t;

bridge_config_t *
bridge_config_copy(const bridge_config_t *conf);

void
bridge_config_free(bridge_config_t *conf);

int bridge_set_up(const bridge_config_t *conf, pid_t pid);

int bridge_clean(pid_t pid);

#endif
