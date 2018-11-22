#ifndef _CORE_FS_H_
#define _CORE_FS_H_

#include "pub/mount.h"

int root_mount(const char *root,
               const char *lower,
               const char *upper,
               const char *work);

int root_umount(const char *root);

int vfs_mount();

#endif
