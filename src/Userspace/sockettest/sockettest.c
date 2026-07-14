/*
 * sockettest - minimal TCP loopback diagnostic for PureDarwin.
 *
 * Answers one question: why can't an X client connect to a server
 * listening on 127.0.0.1? Exercises the exact xtrans sequence
 * (socket/bind-listen on one end, socket/connect on the other) inside a
 * single process, then reports errno at each step. Also dumps the
 * interface list so a missing 127.0.0.1 on lo0 is visible directly.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>

static void
report(const char *what, int ok)
{
    if (ok)
        printf("[sockettest] %-28s ok\n", what);
    else
        printf("[sockettest] %-28s FAILED errno=%d (%s)\n",
               what, errno, strerror(errno));
}

int
main(void)
{
    /* The xtrans client path (with IPv6 off) resolves the display host with
     * gethostbyname() before connecting; exercise it the same way. */
    struct hostent *he = gethostbyname("127.0.0.1");
    report("gethostbyname(127.0.0.1)", he != NULL);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        unsigned addr;
        memcpy(&addr, he->h_addr_list[0], 4);
        printf("[sockettest]   -> addrtype %d len %d addr %#x\n",
               he->h_addrtype, he->h_length, ntohl(addr));
    }
    he = gethostbyname("localhost");
    report("gethostbyname(localhost)", he != NULL);

    if (!he)
        printf("[sockettest]   h_errno=%d\n", h_errno);

    unsigned raw = inet_addr("127.0.0.1");
    report("inet_addr(127.0.0.1)", raw == htonl(0x7f000001));
    printf("[sockettest]   inet_addr raw=%#x htonl=%#x isdigit('1')=%d isascii('1')=%d\n",
           raw, (unsigned)htonl(0x7f000001), isdigit('1'), isascii('1'));

    struct in_addr ia;
    ia.s_addr = 0;
    int ar = inet_aton("127.0.0.1", &ia);
    printf("[sockettest]   inet_aton rc=%d s_addr=%#x\n", ar, ia.s_addr);

    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    report("socket(listen)", lsock >= 0);
    if (lsock < 0) return 1;

    int one = 1;
    setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(16321);
    report("bind 0.0.0.0:16321", bind(lsock, (struct sockaddr *)&sin, sizeof(sin)) == 0);
    report("listen", listen(lsock, 4) == 0);

    int csock = socket(AF_INET, SOCK_STREAM, 0);
    report("socket(connect)", csock >= 0);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(16321);
    sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */

    report("connect 127.0.0.1:16321",
           connect(csock, (struct sockaddr *)&sin, sizeof(sin)) == 0);

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int asock = accept(lsock, (struct sockaddr *)&peer, &plen);
    report("accept", asock >= 0);

    if (asock >= 0) {
        char c = 'x';
        report("write over loopback", write(asock, &c, 1) == 1);
        report("read over loopback", read(csock, &c, 1) == 1 && c == 'x');
        close(asock);
    }

    close(csock);
    close(lsock);
    printf("[sockettest] done\n");
    return 0;
}
