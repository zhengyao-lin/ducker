#include <stdlib.h>
#include <stdio.h>

#include "fs.h"

int root_mount(const char *root,
               const char *lower,
               const char *upper,
               const char *work)
{
    char *param;

    asprintf(&param, "lowerdir=%s,upperdir=%s,workdir=%s", lower, upper, work);

    if (mount("overlay", root, "overlay", MS_MGC_VAL, param)) {
        perror("mount root");
        free(param);
        return -1;
    }

    free(param);

    return 0;
}

int root_umount(const char *root)
{
    if (umount(root)) {
        perror("umount root");
        return -1;
    }

    return 0;
}

int vfs_mount()
{
    // mount proc vfs
    if (mount("proc", "/proc", "proc", 0, NULL)) {
        perror("mount proc");
        return -1;
    }

    // mount sys vfs
    if (mount("sys", "/sys", "sysfs", 0, NULL)) {
        perror("mount sys");
        return -1;
    }

    // mount tmpfs
    if (mount("tmp", "/tmp", "tmpfs", 0, NULL)) {
        perror("mount tmpfs");
        return -1;
    }

    return 0;
}
