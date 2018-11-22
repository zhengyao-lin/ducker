#include <stdio.h>

#include "pub/limit.h"
#include "pub/fd.h"

#include "user.h"

int user_map_set_up(pid_t child)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "/proc/%d/uid_map", child);
    int uid_map = open(path, O_WRONLY);
    snprintf(path, sizeof(path), "/proc/%d/gid_map", child);
    int gid_map = open(path, O_WRONLY);

    // map all users
    dprintf(uid_map, "0 0 65536\n");
    dprintf(gid_map, "0 0 65536\n");

    close(uid_map);
    close(gid_map);

    return 0;
}
