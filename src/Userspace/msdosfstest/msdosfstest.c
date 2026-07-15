#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSDOSFS_ARGSMAGIC 0xe4eff301U

struct msdosfs_args {
    char *fspec;
    uid_t uid;
    gid_t gid;
    mode_t mask;
    uint32_t flags;
    uint32_t magic;
    int32_t secondsWest;
    uint8_t label[64];
};

static int
list_dir(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *de;

    if (!dir) {
        perror("opendir");
        return 1;
    }

    printf("directory %s:\n", path);
    while ((de = readdir(dir)) != NULL)
        printf("  %s\n", de->d_name);
    closedir(dir);
    return 0;
}

static int
write_read_unlink(const char *path)
{
    const char payload[] = "msdosfs runtime smoke test\n";
    char buf[sizeof(payload)];
    int fd;
    ssize_t n;

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        perror("open write");
        return 1;
    }

    n = write(fd, payload, sizeof(payload) - 1);
    if (n != (ssize_t)(sizeof(payload) - 1)) {
        perror("write");
        close(fd);
        return 1;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return 1;
    }

    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return 1;
    }
    close(fd);

    if (strcmp(buf, payload) != 0) {
        fprintf(stderr, "readback mismatch: '%s'\n", buf);
        return 1;
    }

    if (unlink(path) < 0) {
        perror("unlink");
        return 1;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    const char *device = argc > 1 ? argv[1] : "/dev/disk0s1";
    const char *target = argc > 2 ? argv[2] : "/esp";
    const char *testfile = "/esp/PDMSDOS.TST";
    struct msdosfs_args args;
    int rc = 0;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    args.uid = 0;
    args.gid = 0;
    args.mask = 022;
    args.magic = MSDOSFS_ARGSMAGIC;

    if (mkdir(target, 0755) < 0 && errno != EEXIST) {
        perror("mkdir");
        return 1;
    }

    if (mount("msdos", target, 0, &args) < 0) {
        perror("mount msdos");
        return 1;
    }
    printf("mounted %s on %s as msdos\n", device, target);

    rc |= list_dir(target);
    rc |= write_read_unlink(testfile);
    rc |= list_dir(target);

    if (unmount(target, 0) < 0) {
        perror("unmount");
        rc = 1;
    } else {
        printf("unmounted %s\n", target);
    }

    return rc;
}
