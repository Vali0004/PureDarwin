/*
 * Minimal stand-in for Apple's dns_sd.h client API (normally backed by the
 * mDNSResponder daemon over Mach IPC/Unix socket - PureDarwin has neither).
 * res_query.c's mDNS-based query path (DNSServiceQueryRecord() et al.) is a
 * side entry point, NOT on the res_nquery()/res_nsearch() path that
 * res_query()/res_search()/getaddrinfo() actually use (those go through
 * res_send.c's classic raw-socket resolver instead) - so this only needs
 * to compile and fail cleanly, not actually work. Replace when
 * mDNSResponder comes online.
 */
#include "include/dns_sd.h"

DNSServiceErrorType DNSServiceQueryRecord(
	DNSServiceRef *sdRef,
	DNSServiceFlags flags,
	uint32_t interfaceIndex,
	const char *fullname,
	uint16_t rrtype,
	uint16_t rrclass,
	DNSServiceQueryRecordReply callBack,
	void *context)
{
	(void)flags; (void)interfaceIndex; (void)fullname;
	(void)rrtype; (void)rrclass; (void)callBack; (void)context;
	if (sdRef) *sdRef = 0;
	return kDNSServiceErr_Unknown;
}

int DNSServiceRefSockFD(DNSServiceRef sdRef)
{
	(void)sdRef;
	return -1;
}

DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef sdRef)
{
	(void)sdRef;
	return kDNSServiceErr_Unknown;
}

void DNSServiceRefDeallocate(DNSServiceRef sdRef)
{
	(void)sdRef;
}
