/*
 * Storage for the well-known IPv6 address globals that <netinet6/in6.h>
 * declares extern (in6addr_any/in6addr_loopback). Real Darwin defines
 * these in Libinfo; this from-scratch libc has no equivalent, so any
 * userspace code that references them directly (rather than only via
 * IN6ADDR_ANY_INIT/IN6ADDR_LOOPBACK_INIT struct initializers) - e.g.
 * xterm's Xtrans IPv6 socket transport - needs real definitions here.
 */
#include <netinet/in.h>

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
