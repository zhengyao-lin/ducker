#include "core/container.h"

int main(int argc, char **argv)
{
    cgroup_entry_t cg_conf[] = {
        (cgroup_entry_t) {
            .resrc = "memory",
            .var = "memory.limit_in_bytes",
            .val = "512M"
        }
    };

    bridge_config_t bridge_conf = {
        .host_ip = "10.200.1.1",
        .cont_ip = "10.200.1.2",
        .use_physical = true
    };

    container_config_t conf = {
        .tmp_dir = "ducker-tmp-XXXXXX",
        .host_name = "ducker",
        .nameserver = "1.1.1.1",
        .bridge_conf = &bridge_conf,

        .cg_conf = cg_conf,
        .cg_n_conf = sizeof(cg_conf) / sizeof(*cg_conf)
    };

    container_t *cont;

    if (argc < 2) {
        fprintf(stderr, "need one argument\n");
        return -1;
    }

    cont = container_new(&conf);

    if (container_run_image(cont, argv[1])) {
        fprintf(stderr, "failed to run image\n");
    }

    container_free(cont);

    return 0;
}
