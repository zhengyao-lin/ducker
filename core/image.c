#include <stdio.h>

#include "pub/string.h"

#include "image.h"

static struct {
    const char *suf;
    const char *param;
} param_map[] = {
    { ".tar.gz", "-xzf" },
    { ".tar.bz", "-xjf" },
    { ".tar.xz", "-xJf" },

    { ".tgz", "-xzf" },
    { ".tbz", "-xjf" },
    { ".txz", "-xJf" }
};

int
decompress_image(const char *path, const char *target)
{
    size_t i;
    char *cmd;

    for (i = 0; i < sizeof(param_map) / sizeof(*param_map); i++) {
        if (string_endswith(path, param_map[i].suf)) {
            asprintf(&cmd, "tar %s '%s' -C '%s'", param_map[i].param, path, target);

            if (system(cmd)) {
                LOG("failed to decompress image '%s'", path);
                free(cmd);
                return -1;
            }

            free(cmd);
            return 0;
        }
    }

    LOG("unable to recognize image format for '%s'", path);

    return -1;
}
