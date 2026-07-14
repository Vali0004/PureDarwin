/*
 * Minimal netdb surface: gethostbyname/h_errno/hstrerror.
 *
 * The real implementations live in libinfo/libresolv (mDNSResponder-backed),
 * none of which exist in PureDarwin yet -- and there is no network stack to
 * query anyway. Resolve what needs no resolver: dotted-quad literals and
 * localhost. That is exactly what libX11/xtrans needs to connect to a local
 * X server (it calls gethostbyname("127.0.0.1") before connect()). Anything
 * else fails cleanly with HOST_NOT_FOUND instead of stubbing to a crash.
 * Replace when libinfo/libresolv come online.
 */
#include <netdb.h>
#include <stddef.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int h_errno;

struct hostent *
gethostbyname(const char *name)
{
	static struct in_addr addr;
	static char *addr_list[2] = { (char *)&addr, NULL };
	static char *aliases[1] = { NULL };
	static char hname[256];
	static struct hostent he = {
		.h_name = hname,
		.h_aliases = aliases,
		.h_addrtype = AF_INET,
		.h_length = sizeof(struct in_addr),
		.h_addr_list = addr_list,
	};

	if (name == NULL) {
		h_errno = HOST_NOT_FOUND;
		return NULL;
	}
	if (strcmp(name, "localhost") == 0 ||
	    strcmp(name, "localhost.localdomain") == 0) {
		addr.s_addr = htonl(INADDR_LOOPBACK);
	} else if (!inet_aton(name, &addr)) {
		h_errno = HOST_NOT_FOUND;
		return NULL;
	}
	strlcpy(hname, name, sizeof(hname));
	h_errno = 0;
	return &he;
}

const char *
hstrerror(int err)
{
	switch (err) {
	case NETDB_SUCCESS:  return "Resolver Error 0 (no error)";
	case HOST_NOT_FOUND: return "Unknown host";
	case TRY_AGAIN:      return "Host name lookup failure";
	case NO_RECOVERY:    return "Unknown server error";
	case NO_DATA:        return "No address associated with name";
	default:             return "Unknown resolver error";
	}
}
