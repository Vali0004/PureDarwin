/*
 * ping - minimal IPv4 ICMP echo diagnostic for early PureDarwin networking.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif

#define ICMP_ECHOREPLY 0
#define ICMP_ECHO 8
#define ICMP_PACKET_SIZE 40
#define PING_DEFAULT_HOST "10.0.2.2"
#define PING_DEFAULT_COUNT 4
#define PING_TIMEOUT_SEC 2

struct icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t sequence;
    uint8_t payload[ICMP_PACKET_SIZE - 8];
};

static uint16_t
swap16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t
icmp_checksum(const void *buffer, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint32_t sum = 0;

    while (length > 1) {
        sum += (uint16_t)((bytes[0] << 8) | bytes[1]);
        bytes += 2;
        length -= 2;
    }

    if (length != 0) {
        sum += (uint16_t)(bytes[0] << 8);
    }

    while ((sum >> 16) != 0) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static long
elapsed_ms(const struct timeval *start, const struct timeval *end)
{
    long sec = end->tv_sec - start->tv_sec;
    long usec = end->tv_usec - start->tv_usec;

    return sec * 1000 + usec / 1000;
}

static void
fill_payload(struct icmp_echo *packet)
{
    size_t i;

    for (i = 0; i < sizeof(packet->payload); i++) {
        packet->payload[i] = (uint8_t)('a' + (i % 26));
    }
}

static const uint8_t *
find_icmp_packet(const uint8_t *buffer, ssize_t length, struct in_addr *source)
{
    unsigned int ihl;

    if (length < 8) {
        return NULL;
    }

    if ((buffer[0] >> 4) != 4) {
        return buffer;
    }

    ihl = (unsigned int)(buffer[0] & 0x0f) * 4;
    if (ihl < 20 || length < (ssize_t)(ihl + 8)) {
        return NULL;
    }

    if (source != NULL) {
        memcpy(&source->s_addr, buffer + 12, sizeof(source->s_addr));
    }

    return buffer + ihl;
}

int
main(int argc, char **argv)
{
    const char *host = PING_DEFAULT_HOST;
    int count = PING_DEFAULT_COUNT;
    struct sockaddr_in destination;
    int fd;
    int transmitted = 0;
    int received = 0;
    int ident;
    int seq;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        count = atoi(argv[2]);
        if (count <= 0) {
            count = 1;
        }
    }

    if (host[0] != '\0') {
        size_t host_len = strlen(host);

        if (host_len > 1 && host[host_len - 1] == '.') {
            ((char *)host)[host_len - 1] = '\0';
        }
    }

    memset(&destination, 0, sizeof(destination));
    destination.sin_len = sizeof(destination);
    destination.sin_family = AF_INET;
    if (inet_aton(host, &destination.sin_addr) == 0) {
        printf("ping: invalid IPv4 address %s\n", host);
        return 2;
    }

    fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        printf("ping: raw ICMP socket failed: %s\n", strerror(errno));
        return 1;
    }

    ident = getpid() & 0xffff;
    printf("PING %s: %d data bytes\n", host, (int)sizeof(((struct icmp_echo *)0)->payload));

    for (seq = 1; seq <= count; seq++) {
        struct icmp_echo packet;
        struct timeval start;
        int done = 0;

        memset(&packet, 0, sizeof(packet));
        packet.type = ICMP_ECHO;
        packet.code = 0;
        packet.ident = swap16((uint16_t)ident);
        packet.sequence = swap16((uint16_t)seq);
        fill_payload(&packet);
        packet.checksum = swap16(icmp_checksum(&packet, sizeof(packet)));

        gettimeofday(&start, NULL);
        if (sendto(fd, &packet, sizeof(packet), 0,
            (struct sockaddr *)&destination, sizeof(destination)) < 0) {
            printf("ping: sendto: %s\n", strerror(errno));
            break;
        }
        transmitted++;

        while (!done) {
            fd_set readfds;
            struct timeval timeout;
            int ready;

            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);
            timeout.tv_sec = PING_TIMEOUT_SEC;
            timeout.tv_usec = 0;

            ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
            if (ready < 0) {
                printf("ping: select: %s\n", strerror(errno));
                done = 1;
            } else if (ready == 0) {
                printf("Request timeout for icmp_seq %d\n", seq);
                done = 1;
            } else {
                uint8_t reply[512];
                ssize_t nread;
                struct timeval end;
                struct in_addr source;
                const uint8_t *icmp;

                memset(&source, 0, sizeof(source));
                nread = recv(fd, reply, sizeof(reply), 0);
                if (nread < 0) {
                    printf("ping: recv: %s\n", strerror(errno));
                    done = 1;
                    continue;
                }

                icmp = find_icmp_packet(reply, nread, &source);
                if (icmp == NULL) {
                    continue;
                }

                int same_host = source.s_addr == destination.sin_addr.s_addr;
                if (icmp[0] != ICMP_ECHOREPLY || !same_host) {
                    continue;
                }

                gettimeofday(&end, NULL);
                printf("%ld bytes from %s: icmp_seq=%d time=%ld ms\n",
                    (long)nread, inet_ntoa(source), seq, elapsed_ms(&start, &end));
                received++;
                done = 1;
            }
        }
    }

    close(fd);
    printf("--- %s ping statistics ---\n", host);
    printf("%d packets transmitted, %d packets received\n", transmitted, received);

    return received == transmitted ? 0 : 1;
}
