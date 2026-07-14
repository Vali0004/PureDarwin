#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#define MAX_PROCS 256

static char
state_char(char state)
{
    switch (state) {
    case SIDL:
        return 'I';
    case SRUN:
        return 'R';
    case SSLEEP:
        return 'S';
    case SSTOP:
        return 'T';
    case SZOMB:
        return 'Z';
    default:
        return '?';
    }
}

int
main(int argc, char **argv)
{
    struct kinfo_proc procs[MAX_PROCS];
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t len = sizeof(procs);
    size_t count;

    if (argc > 1 && strcmp(argv[1], "-h") != 0 && strcmp(argv[1], "--help") != 0) {
        fprintf(stderr, "usage: ps\n");
        return 1;
    }

    if (sysctl(mib, 4, procs, &len, NULL, 0) < 0) {
        fprintf(stderr, "ps: sysctl KERN_PROC_ALL failed: %s\n", strerror(errno));
        return 1;
    }

    count = len / sizeof(procs[0]);

    printf("%5s %5s %s %s\n", "PID", "PPID", "S", "COMMAND");
    for (size_t i = 0; i < count; i++) {
        const struct kinfo_proc *kp = &procs[i];
        const char *comm = kp->kp_proc.p_comm;

        if (kp->kp_proc.p_pid == 0 && comm[0] == '\0') {
            continue;
        }

        printf("%5d %5d %c %s\n",
            kp->kp_proc.p_pid,
            kp->kp_eproc.e_ppid,
            state_char(kp->kp_proc.p_stat),
            comm[0] != '\0' ? comm : "-");
    }

    if (len == sizeof(procs)) {
        fprintf(stderr, "ps: output may be truncated at %d processes\n", MAX_PROCS);
    }

    return 0;
}
