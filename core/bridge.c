#include <stdlib.h>

#include "pub/type.h"
#include "pub/limit.h"
#include "pub/fd.h"

#include "bridge.h"

#define BRIDGE_VETH_PREFIX "dveth"
#define BRIDGE_VPEER_PREFIX "dvpeer"

#define SYSTEM(clean, action, ...) \
    do { \
        char *cmd; \
        asprintf(&cmd, __VA_ARGS__); \
        LOG("+ %s", cmd); \
        if (system(cmd)) { \
            LOG("failed to " action); \
            free(cmd); \
            clean; \
            return -1; \
        } \
        free(cmd); \
    } while (0);

bridge_config_t *
bridge_config_copy(const bridge_config_t *conf)
{
    bridge_config_t *copy = malloc(sizeof(*copy));
    ASSERT(copy, "out of mem");

    copy->host_ip = strdup(conf->host_ip);
    copy->cont_ip = strdup(conf->cont_ip);
    copy->use_physical = conf->use_physical;

    return copy;
}

void
bridge_config_free(bridge_config_t *conf)
{
    if (conf) {
        free(conf->host_ip);
        free(conf->cont_ip);
        free(conf);
    }
}

// get current physical device
static char *get_physical_dev()
{
    FILE *fp = popen("ip route list default", "r");
    char dev[16];

    if (!fp) {
        perror("failed to execute command");
        return NULL;
    }

    if (fscanf(fp, "default via %*s dev %16s", dev) != 1) {
        LOG("failed to get device name");
        return NULL;
    }

    pclose(fp);

    return strdup(dev);
}

int bridge_set_up(const bridge_config_t *conf, pid_t pid)
{
    char *veth = NULL, *vpeer = NULL, *phy = NULL;
    char p1[PATH_MAX], p2[PATH_MAX];
    int fd;

    snprintf(p1, sizeof(p1), "/proc/%d/ns/net", pid);
    snprintf(p2, sizeof(p2), "/var/run/netns/%d", pid);

    // make dir first if not exist
    mkdir("/var/run/netns", 0755);

    fd = open(p2, O_RDONLY | O_CREAT, 0444); // read only

    if (fd == -1) {
        perror("open namespace");
        return -1;
    }

    close(fd);

    if (mount(p1, p2, "bind", MS_BIND, NULL)) {
        perror("bind netns name");
        return -1;
    }

    asprintf(&veth, BRIDGE_VETH_PREFIX "%d", pid);
    asprintf(&vpeer, BRIDGE_VPEER_PREFIX "%d", pid);

#define CLEAN \
    do { \
        free(veth); \
        free(vpeer); \
        free(phy); \
    } while (0)

    SYSTEM(CLEAN, "create veth pair", "ip link add %s type veth peer name %s", veth, vpeer);
    SYSTEM(CLEAN, "add vpeer to the new namespace", "ip link set %s netns %d", vpeer, pid);

    SYSTEM(CLEAN, "assign host ip", "ip addr add %s/24 dev %s", conf->host_ip, veth);
    SYSTEM(CLEAN, "start veth", "ip link set %s up", veth);

    SYSTEM(CLEAN, "assign container ip", "ip netns exec %d ip addr add %s/24 dev %s", pid, conf->cont_ip, vpeer);
    SYSTEM(CLEAN, "activate lo", "ip netns exec %d ip link set lo up", pid);
    SYSTEM(CLEAN, "start vpeer", "ip netns exec %d ip link set %s up", pid, vpeer);

    SYSTEM(CLEAN, "activate routing", "ip netns exec %d ip route add default via %s", pid, conf->host_ip);

    if (umount(p2)) {
        perror("umount tmp netns");
        CLEAN;
        return -1;
    }

    if (unlink(p2)) {
        perror("unlink tmp netns");
        CLEAN;
        return -1;
    }

    phy = get_physical_dev();

    if (phy) {
        // set access to internet
        LOG("forwarding between %s and %s", phy, veth);

        fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY | O_TRUNC);

        if (fd == -1) {
            perror("enable ip forward");
            CLEAN;
            return -1;
        }

        if (write(fd, "1", 1) != 1) {
            perror("failed to enable ip forward");
            CLEAN;
            return -1;
        }

        close(fd);

        // SYSTEM(CLEAN, "clean forward rules", "iptables -P FORWARD DROP");
        // SYSTEM(CLEAN, "clean forward rules", "iptables -F FORWARD");
        // SYSTEM(CLEAN, "clean nat rules", "iptables -t nat -F");

        SYSTEM(CLEAN, "enable host routing", "iptables -t nat -A POSTROUTING -s %s/24 -o %s -j MASQUERADE", conf->cont_ip, phy);
        SYSTEM(CLEAN, "enable host routing", "iptables -A FORWARD -i %s -o %s -j ACCEPT", phy, veth);
        SYSTEM(CLEAN, "enable host routing", "iptables -A FORWARD -o %s -i %s -j ACCEPT", phy, veth);
    }

    CLEAN;

#undef CLEAN

    return 0;
}

int bridge_clean(pid_t pid)
{
    char *veth;

    asprintf(&veth, BRIDGE_VETH_PREFIX "%d", pid);

    // ignore any failures
    SYSTEM({ free(veth); return 0; }, "remove veth", "ip link del %s", veth);

    free(veth);

    return 0;
}
