#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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

struct generic_mount_args {
    char *fspec;
};

static void
usage(void)
{
    fprintf(stderr, "usage: mount -t type [-o options] special node\n");
    exit(64);
}

static int
parse_options(const char *options)
{
    char buf[256];
    char *opt;
    char *next;
    int flags = 0;

    if (!options || !*options)
        return 0;

    strncpy(buf, options, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (opt = buf; opt && *opt; opt = next) {
        next = strchr(opt, ',');
        if (next)
            *next++ = '\0';

        if (strcmp(opt, "ro") == 0 || strcmp(opt, "rdonly") == 0)
            flags |= MNT_RDONLY;
        else if (strcmp(opt, "rw") == 0)
            flags &= ~MNT_RDONLY;
        else if (strcmp(opt, "nosuid") == 0)
            flags |= MNT_NOSUID;
        else if (strcmp(opt, "nodev") == 0)
            flags |= MNT_NODEV;
        else if (strcmp(opt, "noexec") == 0)
            flags |= MNT_NOEXEC;
        else if (strcmp(opt, "sync") == 0 || strcmp(opt, "synchronous") == 0)
            flags |= MNT_SYNCHRONOUS;
        else if (strcmp(opt, "async") == 0)
            flags |= MNT_ASYNC;
        else if (strcmp(opt, "update") == 0)
            flags |= MNT_UPDATE;
        else if (strcmp(opt, "force") == 0)
            flags |= MNT_FORCE;
        else
            fprintf(stderr, "mount: ignoring unsupported option '%s'\n", opt);
    }

    return flags;
}

static int
mount_msdos(const char *device, const char *dir, int flags)
{
    struct msdosfs_args args;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    args.uid = getuid();
    args.gid = getgid();
    args.mask = 022;
    args.magic = MSDOSFS_ARGSMAGIC;

    return mount("msdos", dir, flags, &args);
}

static int
mount_generic(const char *type, const char *device, const char *dir, int flags)
{
    struct generic_mount_args args;

    memset(&args, 0, sizeof(args));
    args.fspec = (char *)device;
    return mount(type, dir, flags, &args);
}

int
main(int argc, char **argv)
{
    const char *type = NULL;
    const char *options = NULL;
    const char *device;
    const char *dir;
    int ch;
    int flags;
    int rc;

    while ((ch = getopt(argc, argv, "rt:o:")) != -1) {
        switch (ch) {
        case 'r':
            options = options ? options : "ro";
            break;
        case 't':
            type = optarg;
            break;
        case 'o':
            options = optarg;
            break;
        default:
            usage();
        }
    }

    if (!type || argc - optind != 2)
        usage();

    device = argv[optind];
    dir = argv[optind + 1];
    flags = parse_options(options);

    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        perror(dir);
        return 1;
    }

    if (strcmp(type, "msdos") == 0 || strcmp(type, "msdosfs") == 0)
        rc = mount_msdos(device, dir, flags);
    else
        rc = mount_generic(type, device, dir, flags);

    if (rc < 0) {
        fprintf(stderr, "mount: %s on %s as %s: %s\n",
            device, dir, type, strerror(errno));
        return 1;
    }

    return 0;
}
