/*
 * No-op dnsinfo (configd/SCDynamicStore) client stub - see
 * include/dnsinfo.h for why. Every caller already falls back cleanly to
 * parsing /etc/resolv.conf directly when dns_configuration_copy() returns
 * NULL.
 */
#include "include/dnsinfo.h"

dns_config_t *
dns_configuration_copy(void)
{
	return 0;
}

void
dns_configuration_free(dns_config_t *config)
{
	(void)config;
}

const char *
dns_configuration_notify_key(void)
{
	return "pd.dnsinfo.unavailable";
}
