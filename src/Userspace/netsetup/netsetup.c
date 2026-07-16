/*
 * netsetup - tiny early PureDarwin IPv4 configurator.
 *
 * Defaults match QEMU user networking:
 *   en0 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SA_SIZE
#define SA_SIZE(sa) \
    (((sa)->sa_len == 0) ? sizeof(long) : \
    (1 + (((sa)->sa_len - 1) | (sizeof(long) - 1))))
#endif

static int
parse_ipv4(const char *text, struct sockaddr_in *sin)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    if (inet_aton(text, &sin->sin_addr) == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void
copy_sockaddr(struct sockaddr *dst, const struct sockaddr_in *src)
{
    memset(dst, 0, sizeof(*dst));
    memcpy(dst, src, sizeof(*src));
}

static int
set_flags(int sock, const char *ifname, short set, short clear)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        printf("netsetup: %s SIOCGIFFLAGS failed: %s\n", ifname, strerror(errno));
        return -1;
    }

    ifr.ifr_flags |= set;
    ifr.ifr_flags &= (short)~clear;

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        printf("netsetup: %s SIOCSIFFLAGS failed: %s\n", ifname, strerror(errno));
        return -1;
    }
    return 0;
}

static int
add_addr(int sock, const char *ifname, const char *addr, const char *mask, const char *broadcast)
{
    struct ifaliasreq ifra;
    struct sockaddr_in sin;

    memset(&ifra, 0, sizeof(ifra));
    strlcpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name));

    if (parse_ipv4(addr, &sin) < 0) {
        printf("netsetup: invalid address %s\n", addr);
        return -1;
    }
    copy_sockaddr(&ifra.ifra_addr, &sin);

    if (parse_ipv4(mask, &sin) < 0) {
        printf("netsetup: invalid netmask %s\n", mask);
        return -1;
    }
    copy_sockaddr(&ifra.ifra_mask, &sin);

    if (broadcast && broadcast[0]) {
        if (parse_ipv4(broadcast, &sin) < 0) {
            printf("netsetup: invalid broadcast %s\n", broadcast);
            return -1;
        }
        copy_sockaddr(&ifra.ifra_broadaddr, &sin);
    }

    if (ioctl(sock, SIOCAIFADDR, &ifra) < 0) {
        if (errno == EEXIST) {
            printf("netsetup: %s already has %s\n", ifname, addr);
            return 0;
        }
        printf("netsetup: %s SIOCAIFADDR %s failed: %s\n",
            ifname, addr, strerror(errno));
        return -1;
    }

    printf("netsetup: %s inet %s netmask %s", ifname, addr, mask);
    if (broadcast && broadcast[0])
        printf(" broadcast %s", broadcast);
    printf("\n");
    return 0;
}

static char *
append_sockaddr(char *cursor, const struct sockaddr *sa)
{
    size_t padded = SA_SIZE(sa);

    memset(cursor, 0, padded);
    memcpy(cursor, sa, sa->sa_len);
    return cursor + padded;
}

static int
add_default_route(const char *gateway)
{
    struct {
        struct rt_msghdr hdr;
        char addrs[256];
    } msg;
    struct sockaddr_in dst;
    struct sockaddr_in gw;
    struct sockaddr_in mask;
    char *cursor;
    int sock;
    ssize_t written;

    if (parse_ipv4("0.0.0.0", &dst) < 0 ||
        parse_ipv4(gateway, &gw) < 0 ||
        parse_ipv4("0.0.0.0", &mask) < 0) {
        printf("netsetup: invalid gateway %s\n", gateway);
        return -1;
    }

    sock = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (sock < 0) {
        printf("netsetup: PF_ROUTE socket failed: %s\n", strerror(errno));
        return -1;
    }

    memset(&msg, 0, sizeof(msg));
    cursor = msg.addrs;
    cursor = append_sockaddr(cursor, (struct sockaddr *)&dst);
    cursor = append_sockaddr(cursor, (struct sockaddr *)&gw);
    cursor = append_sockaddr(cursor, (struct sockaddr *)&mask);

    msg.hdr.rtm_msglen = (unsigned short)(cursor - (char *)&msg);
    msg.hdr.rtm_version = RTM_VERSION;
    msg.hdr.rtm_type = RTM_ADD;
    msg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
    msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    msg.hdr.rtm_pid = getpid();
    msg.hdr.rtm_seq = 1;

    written = write(sock, &msg, msg.hdr.rtm_msglen);
    if (written < 0) {
        if (errno == EEXIST) {
            close(sock);
            printf("netsetup: default route already exists\n");
            return 0;
        }
        printf("netsetup: add default route via %s failed: %s\n",
            gateway, strerror(errno));
        close(sock);
        return -1;
    }

    close(sock);
    printf("netsetup: default route via %s\n", gateway);
    return 0;
}

static void
usage(void)
{
    printf("usage:\n");
    printf("  netsetup\n");
    printf("  netsetup IFADDR NETMASK GATEWAY\n");
    printf("  netsetup IFNAME IFADDR NETMASK GATEWAY [BROADCAST]\n");
}

int
main(int argc, char **argv)
{
    const char *ifname = "en0";
    const char *addr = "10.0.2.15";
    const char *mask = "255.255.255.0";
    const char *gateway = "10.0.2.2";
    const char *broadcast = "10.0.2.255";
    int sock;
    int rc = 0;

    if (argc == 4) {
        addr = argv[1];
        mask = argv[2];
        gateway = argv[3];
        broadcast = "";
    } else if (argc == 5 || argc == 6) {
        ifname = argv[1];
        addr = argv[2];
        mask = argv[3];
        gateway = argv[4];
        broadcast = (argc == 6) ? argv[5] : "";
    } else if (argc != 1) {
        usage();
        return 2;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("netsetup: AF_INET socket failed: %s\n", strerror(errno));
        return 1;
    }

    if (add_addr(sock, "lo0", "127.0.0.1", "255.0.0.0", "127.255.255.255") < 0)
        rc = 1;
    if (set_flags(sock, "lo0", IFF_UP, 0) < 0)
        rc = 1;

    if (add_addr(sock, ifname, addr, mask, broadcast) < 0)
        rc = 1;
    if (set_flags(sock, ifname, IFF_UP, 0) < 0)
        rc = 1;

    close(sock);

    if (add_default_route(gateway) < 0)
        rc = 1;

    return rc;
}
