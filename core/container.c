#include <stdlib.h>

#include "pub/type.h"
#include "pub/clone.h"
#include "pub/limit.h"
#include "pub/fd.h"

#include "container.h"
#include "cgroup.h"
#include "bridge.h"
#include "user.h"
#include "fs.h"

#define DEFAULT_MODE 0777
#define IMAGE_DIR "image"
#define UPPER_DIR "upper"
#define WORK_DIR "work"
#define ROOT_DIR "root"
#define HOST_DIR "host"

container_config_t *
container_config_copy(const container_config_t *conf)
{
    container_config_t *copy = malloc(sizeof(*copy));
    ASSERT(copy, "out of mem");

    copy->tmp_dir = strdup(conf->tmp_dir);
    copy->host_name = strdup(conf->host_name);
    copy->nameserver = strdup(conf->nameserver);
    copy->bridge_conf = bridge_config_copy(conf->bridge_conf);

    copy->cg_conf = cgroup_entry_copy(conf->cg_conf, conf->cg_n_conf);
    copy->cg_n_conf = conf->cg_n_conf;

    return copy;
}

void
container_config_free(container_config_t *conf)
{
    if (conf) {
        free(conf->tmp_dir);
        free(conf->host_name);
        free(conf->nameserver);
        bridge_config_free(conf->bridge_conf);
        cgroup_entry_free(conf->cg_conf, conf->cg_n_conf);

        free(conf);
    }
}

container_t *
container_new(const container_config_t *conf)
{
    container_t *ret = malloc(sizeof(*ret));
    ASSERT(ret, "out of mem");

    pipe(ret->pipe);
    ret->tmp_dir = NULL;
    ret->conf = container_config_copy(conf);

    return ret;
}

void
container_free(container_t *cont)
{
    if (cont) {
        free(cont->tmp_dir);
        container_config_free(cont->conf);

        container_close_read(cont);
        container_close_write(cont);

        free(cont);
    }
}

void
container_close_read(container_t *cont)
{
    if (cont->pipe[0] != -1) {
        close(cont->pipe[0]);
        cont->pipe[0] = -1;
    }
}

void
container_close_write(container_t *cont)
{
    if (cont->pipe[1] != -1) {
        close(cont->pipe[1]);
        cont->pipe[1] = -1;
    }
}

ssize_t
container_pipe_write(container_t *cont, const char *buf, size_t size)
{
    // TODO: need retry
    ASSERT(cont->pipe[1] != -1, "write pipe closed already");
    return write(cont->pipe[1], buf, size);
}

ssize_t
container_pipe_read(container_t *cont, char *buf, size_t size)
{
    // TODO: need retry
    ASSERT(cont->pipe[0] != -1, "read pipe closed already");
    return read(cont->pipe[0], buf, size);
}

int
container_set_up_tmp_dir(container_t *cont, const char *img)
{
    char *template = strdup(cont->conf->tmp_dir);
    char buf[PATH_MAX];

    if (cont->tmp_dir) {
        // tmp dir already exists
        return 0;
    }

    if (!mkdtemp(template)) {
        perror("mkdtemp");
        return -1;
    }

    cont->tmp_dir = template;

    if (chmod(cont->tmp_dir, DEFAULT_MODE)) {
        perror("chmod tmp dir");
        return -1;
    }

#define MKDIR(name) \
    do { \
        snprintf(buf, sizeof(buf), "%s/%s", template, (name)); \
        if (mkdir(buf, DEFAULT_MODE)) { \
            perror("mkdir"); \
            return -1; \
        } \
    } while (0)

    MKDIR(IMAGE_DIR);
    MKDIR(UPPER_DIR); // upper dir stores the changes in the file system
    MKDIR(WORK_DIR); // work dir is overlay fs's word directory
    MKDIR(ROOT_DIR); // actual root of the container

    // temporal implementation for decompression
    // copy image to upper dir
    snprintf(buf, sizeof(buf), "tar -xzf \'%s\' -C %s/%s", img, template, IMAGE_DIR);

    if (system(buf)) {
        LOG("failed to load image '%s'", img);
        return -1;
    }

    return 0;
}

int
container_clean_tmp_dir(container_t *cont)
{
    char cmd[PATH_MAX];

    if (!cont->tmp_dir) {
        // no tmp dir
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "rm -r '%s'", cont->tmp_dir);

    if (system(cmd)) {
        LOG("failed to remove tmp dir");
        return -1;
    }

    return 0;
}

static int init(void *arg);

int
container_run_image(container_t *cont, const char *img)
{
    pid_t child;
    pid_t parent = getpid();

    if (container_set_up_tmp_dir(cont, img)) {
        LOG("failed to set up tmp dir");
        return -1;
    }

    if (chdir(cont->tmp_dir)) {
        LOG("failed to chdir to tmp dir");
        return -1;
    }

    // if (root_mount(ROOT_DIR, "/", IMAGE_DIR, WORK_DIR)) {
    if (root_mount(ROOT_DIR, IMAGE_DIR, UPPER_DIR, WORK_DIR)) {
        LOG("failed to mount root");
        goto CLEAN;
    }

    if (cgroup_init(cont->conf->cg_conf, cont->conf->cg_n_conf, parent)) {
        LOG("failed to set up cgroup");
        goto CLEAN;
    }

    child = clone(init, cont->stack.stack + sizeof(cont->stack),
                  CLONE_NEWPID | CLONE_NEWNS |
                  CLONE_NEWUTS | CLONE_NEWUSER | CLONE_NEWNET |
                  CLONE_NEWIPC | SIGCHLD, cont, NULL);

    if (user_map_set_up(child)) {
        LOG("failed to set up id map");
    }

    if (bridge_set_up(cont->conf->bridge_conf, child)) {
        LOG("failed to set up bridge");
    }

    // wake init
    container_close_read(cont);
    container_pipe_write(cont, "", 1);
    container_close_write(cont);

    if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
        return -1;
    }

CLEAN:
    if (cgroup_clean(cont->conf->cg_conf, cont->conf->cg_n_conf, parent)) {
        LOG("failed to clean up cgroup");
    }

    if (root_umount(ROOT_DIR)) {
        LOG("failed to umount file system");
        // return -1;
    }

    if (bridge_clean(child)) {
        LOG("failed to clean up bridge");
    }

    // exit tmp dir
    if (chdir("..")) {
        LOG("failed to chdir to parent dir");
        return -1;
    }

    if (container_clean_tmp_dir(cont)) {
        LOG("failed to clean tmp dir");
        // return -1;
    }

    return 0;
}

/* inside container */

static int
pivot_root(char *new, char *old)
{
    return syscall(SYS_pivot_root, new, old);
}

static int
init_load_container()
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

    vfs_mount();

    return 0;
}

static int
init_set_up_env(container_t *cont)
{
    int fd;

    if (sethostname(cont->conf->host_name, strlen(cont->conf->host_name))) {
        perror("sethostname");
        // return -1;
    }

    fd = open("/etc/resolv.conf", O_WRONLY | O_TRUNC);

    // set dns server
    if (fd == -1) {
        perror("open /etc/resolv.conf");
    } else {
        dprintf(fd, "nameserver %s\n", cont->conf->nameserver);
        close(fd);
    }

    return -1;
}

static int
init(void *arg)
{
    container_t *cont = arg;
    char buf[1];
    int ret;

    container_close_write(cont);
    container_pipe_read(cont, buf, 1);
    container_close_read(cont);

    LOG("init is up");

    if (init_load_container()) {
        LOG("failed to load container");
        return -1;
    }

    if (init_set_up_env(cont)) {
        // do nothing
    }

    ret = system("/bin/bash");

    return ret;
}
