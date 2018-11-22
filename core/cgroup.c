#include <stdio.h>
#include <errno.h>

#include "pub/type.h"
#include "pub/fd.h"
#include "pub/limit.h"
#include "pub/mount.h"

#include "cgroup.h"

#define CGROUP_NAME_PREFIX "ducker.cgroup."
#define CGROUP_MODE 0700

cgroup_entry_t *
cgroup_entry_copy(cgroup_entry_t *conf, size_t n)
{
    cgroup_entry_t *copy = malloc(sizeof(*conf) * n);
    size_t i;

    ASSERT(copy, "out of mem");

    for (i = 0; i < n; i++) {
        copy[i].resrc = strdup(conf[i].resrc);
        copy[i].var = strdup(conf[i].var);
        copy[i].val = strdup(conf[i].val);
    }

    return copy;
}

void
cgroup_entry_free(cgroup_entry_t *conf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        free(conf[i].resrc);
        free(conf[i].var);
        free(conf[i].val);
    }

    free(conf);
}

int
cgroup_init(cgroup_entry_t *conf, size_t n_conf, pid_t pid)
{
    char path[PATH_MAX];
    char var[PATH_MAX];
    int fd;

    char pid_str[16];

    size_t i;

    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    for (i = 0; i < n_conf; i++) {
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/" CGROUP_NAME_PREFIX "%d", conf[i].resrc, pid);

        if (mkdir(path, CGROUP_MODE) && errno != EEXIST) {
            perror("create cgroup namespace");
            return -1;
        } else {
            // created new directory, add pid to it
            snprintf(var, sizeof(var), "%s/tasks", path);

            fd = open(var, O_WRONLY);

            if (fd == -1) {
                perror("failed to open tasks");
                return -1;
            }

            // add process to cgroup
            if (write(fd, pid_str, strlen(pid_str)) == -1) {
                perror("failed to add task");
                close(fd);
                return -1;
            }

            close(fd);
        }

        // set variable

        LOG("cgroup setting %s = %s", conf[i].var, conf[i].val);

        snprintf(var, sizeof(var), "%s/%s", path, conf[i].var);

        fd = open(var, O_WRONLY);

        if (fd == -1) {
            perror("failed to open setting");
            return -1;
        }

        if (write(fd, conf[i].val, strlen(conf[i].val)) == -1) {
            perror("failed to set variable");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

int
cgroup_clean(cgroup_entry_t *conf, size_t n_conf, pid_t pid)
{
    char path[PATH_MAX];
    char tasks[PATH_MAX];
    char pid_str[16];
    int fd;

    struct stat buf;

    size_t i;

    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    for (i = 0; i < n_conf; i++) {
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/" CGROUP_NAME_PREFIX "%d", conf[i].resrc, pid);

        // cgroup exists
        if (stat(path, &buf) == 0) {
            snprintf(tasks, sizeof(tasks), "/sys/fs/cgroup/%s/tasks", conf[i].resrc);

            fd = open(tasks, O_WRONLY);

            if (fd == -1) {
                perror("failed to open tasks");
                return -1;
            }

            if (write(fd, pid_str, strlen(pid_str)) == -1) {
                perror("failed to switch tasks");
                close(fd);
                return -1;
            }

            close(fd);

            if (rmdir(path)) {
                perror("failed to remove cgroup");
                return -1;
            }
        }
    }

    return 0;
}
