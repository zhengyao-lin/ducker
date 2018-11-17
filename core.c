#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

/*

container image in tar.bz:
    - upper: overlay fs upper dir
    - config.toml

*/

#define DEFAULT_MODE 0777
#define IMAGE_DIR "image"
#define UPPER_DIR "upper"
#define WORK_DIR "work"
#define ROOT_DIR "root"
#define HOST_DIR "host"

#define DEFAULT_HOST_IP "10.200.3.1"
#define DEFAULT_CONT_IP "10.200.3.2"
#define DEFAULT_NAMESERVER "1.1.1.1"

#define HOST_NAME "ducker"

typedef struct {
    char stack[4096];
} clone_stack_t;

typedef struct {
    clone_stack_t stack;
    int pipe[2];
} container_t;

container_t *
container_new()
{
    container_t *ret = malloc(sizeof(*ret));
    pipe(ret->pipe);
    return ret;
}

void
container_free(container_t *cont)
{
    free(cont);
}

int set_up_tmp_dir(const char *img)
{
    char tmp_dir[] = "ducker-tmpXXXXXX";
    char buf[PATH_MAX];

    if (!mkdtemp(tmp_dir)) {
        perror("mkdtemp");
        return -1;
    }

    if (chmod(tmp_dir, DEFAULT_MODE)) {
        perror("chmod");
        return -1;
    }

#define MKDIR(name) \
    do { \
        snprintf(buf, sizeof(buf), "%s/" name, tmp_dir); \
        if (mkdir(buf, DEFAULT_MODE)) { \
            perror("mkdir"); \
            return -1; \
        } \
    } while (0)

    MKDIR(IMAGE_DIR);
    MKDIR(UPPER_DIR); // upper dir stores the changes in the file system
    MKDIR(WORK_DIR); // work dir is overlay fs's word directory
    MKDIR(ROOT_DIR); // actual root of the container
    // MKDIR(UPPER_DIR "/dev");

    // temporal implementation for decompression
    // copy image to upper dir
    snprintf(buf, sizeof(buf), "tar -xzf \'%s\' -C %s/" IMAGE_DIR, img, tmp_dir);

    if (system(buf)) {
        fprintf(stderr, "failed to load image '%s'\n", img);
        return -1;
    }

    if (chdir(tmp_dir)) {
        perror("chdir");
        return -1;
    }

    return 0;
}

int clean_tmp_dir()
{
    char cwd[PATH_MAX];
    char cmd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "rm -r '%s'", cwd);

    if (system(cmd)) {
        fprintf(stderr, "rm -r failed\n");
        return -1;
    }

    return 0;
}

// assuming already in tmp dir
// mount / overlay
// /dev and /proc
int prepare_fs()
{
    // mount root
    if (mount("overlay", ROOT_DIR, "overlay", MS_MGC_VAL,
        // "lowerdir=" IMAGE_DIR ",upperdir=" UPPER_DIR ",workdir=" WORK_DIR
        "lowerdir=/,upperdir=" IMAGE_DIR ",workdir=" WORK_DIR
        )) {
        perror("mount root");
        return -1;
    }

    // umount host tmp
    // if (umount2(ROOT_DIR "/tmp", MNT_DETACH)) {
    //     perror("umount tmp");
    //     return -1;
    // }

    // bind host /dev
    // if (mount("/dev", ROOT_DIR "/dev", NULL, MS_BIND, NULL)) {
    //     perror("mount dev");
    //     return -1;
    // }

    return 0;
}

// umount mount points created by prepare_fs
int umount_fs()
{
    // if (umount(ROOT_DIR "/dev")) {
    //     perror("umount dev");
    //     return -1;
    // }

    if (umount(ROOT_DIR)) {
        perror("umount root");
        return -1;
    }

    return 0;
}

static int
pivot_root(char *new, char *old)
{
    return syscall(SYS_pivot_root, new, old);
}

int load_container()
{
    if (mount(ROOT_DIR, ROOT_DIR, "bind", MS_BIND | MS_REC, "")) {
        perror("bind mount root");
        return -1;
    }

    if (mkdir(ROOT_DIR "/" HOST_DIR, DEFAULT_MODE)) {
        perror("mkdir");
        return -1;
    }

    if (pivot_root(ROOT_DIR, ROOT_DIR "/" HOST_DIR)) {
        perror("pivot_root");
        return -1;
    }

    if (chdir("/")) {
        perror("chdir");
        return -1;
    }

    // mount devtmpfs with no extra devices
    // if (mount(NULL, "/dev", "devtmpfs", 0, NULL)) {
    //     perror("mount dev");
    //     return -1;
    // }

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

int set_up_env()
{
    int fd;

    if (sethostname(HOST_NAME, sizeof(HOST_NAME) - 1)) {
        perror("sethostname");
        // return -1;
    }

    fd = open("/etc/resolv.conf", O_WRONLY | O_TRUNC);

    // set dns server
    if (fd == -1) {
        perror("open /etc/resolv.conf");
    } else {
        dprintf(fd, "nameserver " DEFAULT_NAMESERVER "\n");
        close(fd);
    }

    return -1;
}

int init(void *arg)
{
    container_t *cont = arg;
    char buf[1];
    int ret;

    close(cont->pipe[1]);
    read(cont->pipe[0], buf, 1);
    close(cont->pipe[0]);

    fprintf(stderr, "init is up\n");

    if (load_container()) {
        fprintf(stderr, "failed to load container\n");
        return -1;
    }

    if (set_up_env()) {
        // do nothing
    }

    ret = system("/bin/bash");

    return ret;
}

int set_up_id_map(pid_t child)
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

#define BRIDGE_VETH_PREFIX "dveth"
#define BRIDGE_VPEER_PREFIX "dvpeer"

#define SYSTEM(clean, action, ...) \
    do { \
        char *cmd; \
        asprintf(&cmd, __VA_ARGS__); \
        fprintf(stderr, "+ %s\n", cmd); \
        if (system(cmd)) { \
            fprintf(stderr, "failed to " action "\n"); \
            free(cmd); \
            clean; \
            return -1; \
        } \
        free(cmd); \
    } while (0);

// get current physical device
char *get_physical_dev()
{
    FILE *fp = popen("ip route list default", "r");
    char dev[16];

    if (!fp) {
        perror("failed to execute command");
        return NULL;
    }

    if (fscanf(fp, "default via %*s dev %16s", dev) != 1) {
        fprintf(stderr, "failed to get device name\n");
        return NULL;
    }

    pclose(fp);

    return strdup(dev);
}

int set_up_bridge(pid_t pid, const char *host_ip, const char *cont_ip)
{
    char *veth = NULL, *vpeer = NULL, *phy = NULL;
    char p1[PATH_MAX], p2[PATH_MAX];
    int fd;

    snprintf(p1, sizeof(p1), "/proc/%d/ns/net", pid);
    snprintf(p2, sizeof(p2), "/var/run/netns/%d", pid);

    fd = open(p2, O_RDONLY | O_CREAT, DEFAULT_MODE);    

    if (fd == -1) {
        perror("open");
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

    SYSTEM(CLEAN, "assign host ip", "ip addr add %s/24 dev %s", host_ip, veth);
    SYSTEM(CLEAN, "start veth", "ip link set %s up", veth);

    SYSTEM(CLEAN, "assign container ip", "ip netns exec %d ip addr add %s/24 dev %s", pid, cont_ip, vpeer);
    SYSTEM(CLEAN, "activate lo", "ip netns exec %d ip link set lo up", pid);
    SYSTEM(CLEAN, "start vpeer", "ip netns exec %d ip link set %s up", pid, vpeer);

    SYSTEM(CLEAN, "activate routing", "ip netns exec %d ip route add default via %s", pid, host_ip);

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
        fprintf(stderr, "forwarding between %s and %s\n", phy, veth);

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

        SYSTEM(CLEAN, "enable host routing", "iptables -t nat -A POSTROUTING -s %s/24 -o %s -j MASQUERADE", cont_ip, phy);
        SYSTEM(CLEAN, "enable host routing", "iptables -A FORWARD -i %s -o %s -j ACCEPT", phy, veth);
        SYSTEM(CLEAN, "enable host routing", "iptables -A FORWARD -o %s -i %s -j ACCEPT", phy, veth);
    }

    CLEAN;

#undef CLEAN

    return 0;
}

int clean_bridge(pid_t pid)
{
    char *veth, *vpeer;

    asprintf(&veth, BRIDGE_VETH_PREFIX "%d", pid);
    asprintf(&vpeer, BRIDGE_VPEER_PREFIX "%d", pid);

    // SYSTEM("remove veth", "ip link del %s", veth);

    free(veth);
    free(vpeer);

    return 0;
}

int main(int argc, char **argv)
{
    pid_t child;
    container_t *cont = container_new();

    if (argc < 2) {
        fprintf(stderr, "no enough argument\n");
        return 1;
    }

    if (set_up_tmp_dir(argv[1])) {
        fprintf(stderr, "failed to set up tmp dir\n");
        return -1;
    }

    if (prepare_fs()) {
        fprintf(stderr, "failed to prepare file system\n");
        goto CLEAN;
    }

    child = clone(init, cont->stack.stack + sizeof(cont->stack),
                  CLONE_NEWPID | CLONE_NEWNS |
                  CLONE_NEWUTS | CLONE_NEWUSER | CLONE_NEWNET |
                  CLONE_NEWIPC | SIGCHLD, cont, NULL);

    printf("child: %d\n", child);

    if (set_up_id_map(child)) {
        fprintf(stderr, "failed to set up id map\n");
    }

    if (set_up_bridge(child, DEFAULT_HOST_IP, DEFAULT_CONT_IP)) {
        fprintf(stderr, "failed to set up bridge\n");
    }

    // wake init
    close(cont->pipe[0]);
    write(cont->pipe[1], "", 1);
    close(cont->pipe[1]);

    if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
        return -1;
    }

CLEAN:
    if (umount_fs()) {
        fprintf(stderr, "failed to umount file system\n");
        // return -1;
    }

    if (clean_bridge(child)) {
        fprintf(stderr, "failed to clean up bridge\n");
    }

    if (clean_tmp_dir()) {
        fprintf(stderr, "failed to clean tmp dir\n");
        // return -1;
    }

    container_free(cont);

    return 0;
}

/*

TODOs

1. create user
2. network(use veth)
3. save image

*/
