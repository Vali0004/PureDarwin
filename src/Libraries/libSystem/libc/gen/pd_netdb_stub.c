/*
 * Minimal netdb surface: gethostbyname/h_errno/hstrerror.
 *
 * The real implementations live in libinfo/libresolv (mDNSResponder-backed),
 * none of which exist in PureDarwin yet -- and there is no network stack to
 * query anyway. BusyBox's hostname applet links these for `hostname -i`-style
 * lookups; fail cleanly with HOST_NOT_FOUND instead of stubbing to a crash.
 * Replace when libinfo/libresolv come online.
 */
#include <netdb.h>
#include <stddef.h>

int h_errno;

struct hostent *
gethostbyname(const char *name)
{
	(void)name;
	h_errno = HOST_NOT_FOUND;
	return NULL;
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
