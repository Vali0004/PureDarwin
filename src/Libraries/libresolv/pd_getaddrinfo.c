#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>
#include <netdb.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

static struct addrinfo *
pd_gai_new(int family, int socktype, int protocol, uint16_t port,
    const void *addr, socklen_t addrlen, const char *canonname)
{
	struct addrinfo *ai;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;

	ai = calloc(1, sizeof(*ai));
	if (ai == NULL)
		return NULL;

	ai->ai_family = family;
	ai->ai_socktype = socktype;
	ai->ai_protocol = protocol;

	if (family == AF_INET) {
		sin4 = calloc(1, sizeof(*sin4));
		if (sin4 == NULL) {
			free(ai);
			return NULL;
		}
		sin4->sin_family = AF_INET;
		sin4->sin_port = htons(port);
		memcpy(&sin4->sin_addr, addr, addrlen);
		ai->ai_addr = (struct sockaddr *)sin4;
		ai->ai_addrlen = sizeof(*sin4);
	} else {
		sin6 = calloc(1, sizeof(*sin6));
		if (sin6 == NULL) {
			free(ai);
			return NULL;
		}
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		memcpy(&sin6->sin6_addr, addr, addrlen);
		ai->ai_addr = (struct sockaddr *)sin6;
		ai->ai_addrlen = sizeof(*sin6);
	}

	if (canonname != NULL) {
		ai->ai_canonname = strdup(canonname);
	}

	return ai;
}

static int
pd_gai_port(const char *servname, uint16_t *port_out)
{
	char *end;
	unsigned long v;

	if (servname == NULL) {
		*port_out = 0;
		return 0;
	}
	v = strtoul(servname, &end, 10);
	if (*servname != '\0' && *end == '\0' && v <= 65535) {
		*port_out = (uint16_t)v;
		return 0;
	}

	{
		struct servent *se = getservbyname(servname, NULL);
		if (se != NULL) {
			*port_out = ntohs((uint16_t)se->s_port);
			return 0;
		}
	}

	return EAI_SERVICE;
}

/* Query one RR type (T_A or T_AAAA) and append every matching answer RR to
 * *tail. Returns 0 on "query worked, zero-or-more records appended", or a
 * negative res_search() failure (caller decides if that's fatal). */
static int
pd_gai_query(const char *node, int rtype, int socktype, int protocol,
    uint16_t port, struct addrinfo ***tailp, const char **cnameptr)
{
	unsigned char answer[NS_PACKETSZ * 4];
	ns_msg handle;
	ns_rr rr;
	int len, i, count;
	char namebuf[NS_MAXDNAME];

	len = res_search(node, ns_c_in, rtype, answer, (int)sizeof(answer));
	if (len < 0)
		return -1;
	if (ns_initparse(answer, len, &handle) < 0)
		return -1;

	count = ns_msg_count(handle, ns_s_an);
	for (i = 0; i < count; i++) {
		struct addrinfo *ai;

		if (ns_parserr(&handle, ns_s_an, i, &rr) < 0)
			continue;
		if (ns_rr_type(rr) != (ns_type)rtype)
			continue;

		if (*cnameptr == NULL) {
			if (ns_name_ntop(ns_rr_name(rr), namebuf, sizeof(namebuf)) > 0)
				*cnameptr = namebuf[0] ? namebuf : node;
		}

		if (rtype == T_A) {
			if (ns_rr_rdlen(rr) != 4)
				continue;
			ai = pd_gai_new(AF_INET, socktype, protocol, port,
			    ns_rr_rdata(rr), 4, NULL);
		} else {
			if (ns_rr_rdlen(rr) != 16)
				continue;
			ai = pd_gai_new(AF_INET6, socktype, protocol, port,
			    ns_rr_rdata(rr), 16, NULL);
		}
		if (ai == NULL)
			continue;

		**tailp = ai;
		*tailp = &ai->ai_next;
	}

	return 0;
}

int
getaddrinfo(const char * __restrict nodename, const char * __restrict servname,
    const struct addrinfo * __restrict hints, struct addrinfo ** __restrict res)
{
	int family = AF_UNSPEC, socktype = 0, protocol = 0, flags = 0;
	uint16_t port = 0;
	int error;
	struct addrinfo *head = NULL, **tail = &head;
	struct in_addr a4;
	struct in6_addr a6;
	char cname_buf[256];
	const char *cname = NULL;

	if (nodename == NULL && servname == NULL)
		return EAI_NONAME;

	if (hints != NULL) {
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		flags = hints->ai_flags;
	}

	error = pd_gai_port(servname, &port);
	if (error != 0)
		return error;

	if (nodename == NULL) {
		a4.s_addr = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
		head = pd_gai_new(AF_INET, socktype, protocol, port, &a4, 4, NULL);
		if (head == NULL)
			return EAI_MEMORY;
		*res = head;
		return 0;
	}

	/* Numeric address fast path - no DNS traffic, matches AI_NUMERICHOST
	 * and also just saves a round trip for the common "connect to an IP
	 * literal" case. */
	if (family != AF_INET6 && inet_pton(AF_INET, nodename, &a4) == 1) {
		head = pd_gai_new(AF_INET, socktype, protocol, port, &a4, 4,
		    (flags & AI_CANONNAME) ? nodename : NULL);
		if (head == NULL)
			return EAI_MEMORY;
		*res = head;
		return 0;
	}
	if (family != AF_INET && inet_pton(AF_INET6, nodename, &a6) == 1) {
		head = pd_gai_new(AF_INET6, socktype, protocol, port, &a6, 16,
		    (flags & AI_CANONNAME) ? nodename : NULL);
		if (head == NULL)
			return EAI_MEMORY;
		*res = head;
		return 0;
	}
	if (flags & AI_NUMERICHOST)
		return EAI_NONAME;

	if (family == AF_INET || family == AF_UNSPEC) {
		(void)pd_gai_query(nodename, T_A, socktype, protocol, port,
		    &tail, &cname);
	}
	if (family == AF_INET6 || family == AF_UNSPEC) {
		(void)pd_gai_query(nodename, T_AAAA, socktype, protocol, port,
		    &tail, &cname);
	}

	if (head == NULL)
		return EAI_NONAME;

	if ((flags & AI_CANONNAME) && cname != NULL) {
		strlcpy(cname_buf, cname, sizeof(cname_buf));
		head->ai_canonname = strdup(cname_buf);
	}

	*res = head;
	return 0;
}

void
freeaddrinfo(struct addrinfo *ai)
{
	struct addrinfo *next;

	while (ai != NULL) {
		next = ai->ai_next;
		free(ai->ai_canonname);
		free(ai->ai_addr);
		free(ai);
		ai = next;
	}
}

const char *
gai_strerror(int ecode)
{
	switch (ecode) {
	case EAI_AGAIN:    return "Temporary failure in name resolution";
	case EAI_BADFLAGS: return "Invalid value for ai_flags";
	case EAI_FAIL:     return "Non-recoverable failure in name resolution";
	case EAI_FAMILY:   return "ai_family not supported";
	case EAI_MEMORY:   return "Memory allocation failure";
	case EAI_NONAME:   return "hostname nor servname provided, or not known";
	case EAI_SERVICE:  return "servname not supported for ai_socktype";
	case EAI_SOCKTYPE: return "ai_socktype not supported";
	case EAI_SYSTEM:   return "System error";
	default:           return "Unknown error";
	}
}
