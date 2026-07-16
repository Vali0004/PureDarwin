/*
 * Small set of networking helpers libresolv's res_send.c/ns_print.c call
 * that don't exist elsewhere in PureDarwin yet. Real, useful
 * implementations where cheap (arc4random, a numeric-only getnameinfo);
 * safe empty-result stubs where a full implementation needs more OS
 * plumbing than exists yet (getifaddrs - no NET_RT_IFLIST sysctl/ioctl
 * wiring; if_nametoindex - no interface name table). res_send.c already
 * treats getifaddrs/if_nametoindex failure as "skip this optimization",
 * not fatal, so returning "no interfaces" is safe, just less clever about
 * picking a local source address.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern void arc4random_buf(void *buf, size_t nbytes);

uint32_t
arc4random(void)
{
	uint32_t v;
	arc4random_buf(&v, sizeof(v));
	return v;
}

struct ifaddrs;

int
getifaddrs(struct ifaddrs **ifap)
{
	if (ifap) *ifap = 0;
	return 0;   /* empty list: no interfaces reported */
}

void
freeifaddrs(struct ifaddrs *ifa)
{
	(void)ifa;
}

unsigned int
if_nametoindex(const char *ifname)
{
	(void)ifname;
	return 0;   /* "not found", per POSIX */
}

/* Numeric-only getnameinfo: formats addresses/ports as text, never does
 * reverse DNS (NI_NUMERICHOST|NI_NUMERICSERV behavior always, regardless
 * of flags) - sufficient for res_send.c's error-message formatting. */
int
getnameinfo(const struct sockaddr *sa, socklen_t salen,
    char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags)
{
	(void)salen; (void)flags;
	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
		if (host && hostlen) {
			if (!inet_ntop(AF_INET, &sin->sin_addr, host, hostlen))
				return -1;
		}
		if (serv && servlen) {
			char buf[8];
			int n = snprintf(buf, sizeof(buf), "%u", ntohs(sin->sin_port));
			if (n < 0 || (socklen_t)n >= servlen) return -1;
			memcpy(serv, buf, n + 1);
		}
		return 0;
	}
	if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
		if (host && hostlen) {
			if (!inet_ntop(AF_INET6, &sin6->sin6_addr, host, hostlen))
				return -1;
		}
		if (serv && servlen) {
			char buf[8];
			int n = snprintf(buf, sizeof(buf), "%u", ntohs(sin6->sin6_port));
			if (n < 0 || (socklen_t)n >= servlen) return -1;
			memcpy(serv, buf, n + 1);
		}
		return 0;
	}
	errno = EAFNOSUPPORT;
	return -1;
}

/* DNSSEC KEY-record footprint (RFC 2535 Appendix C), only used by
 * ns_print.c's verbose KEY-record formatter - not on the query/parse path
 * getaddrinfo needs. Dropped along with the rest of dst_support.c (see
 * CMakeLists.txt); a trivial reimplementation is cheap enough to keep
 * here rather than pull that whole file back in. */
unsigned int
res_9_dst_s_dns_key_id(const unsigned char *dns_key_rdata, const int rdata_len)
{
	unsigned int ac = 0;
	int i;
	if (!dns_key_rdata) return 0;
	for (i = 0; i < rdata_len; i++)
		ac += (i & 1) ? dns_key_rdata[i] : (unsigned int)dns_key_rdata[i] << 8;
	ac += (ac >> 16) & 0xFFFF;
	return ac & 0xFFFF;
}

/* NSAP addresses (X.25/OSI-era) are not used by anything reachable from
 * getaddrinfo/res_query; only ns_print.c's verbose NSAP-record formatter
 * calls this. */
char *
inet_nsap_ntoa(int binlen, const unsigned char *binary, char *ascii)
{
	(void)binlen; (void)binary;
	static char empty[] = "";
	if (ascii) { ascii[0] = 0; return ascii; }
	return empty;
}
