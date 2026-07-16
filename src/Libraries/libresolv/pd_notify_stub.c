/* No-op notifyd client stub - see include/notify.h for why. */
#include "include/notify.h"

uint32_t
notify_register_plain(const char *name, int *out_token)
{
	(void)name;
	if (out_token) *out_token = -1;
	return NOTIFY_STATUS_FAILED;
}

uint32_t
notify_register_check(const char *name, int *out_token)
{
	(void)name;
	if (out_token) *out_token = -1;
	return NOTIFY_STATUS_FAILED;
}

uint32_t
notify_check(int token, int *check)
{
	(void)token;
	if (check) *check = 0;
	return NOTIFY_STATUS_FAILED;
}

uint32_t
notify_get_state(int token, int *state)
{
	(void)token;
	if (state) *state = 0;
	return NOTIFY_STATUS_FAILED;
}

uint32_t
notify_cancel(int token)
{
	(void)token;
	return NOTIFY_STATUS_OK;
}

uint32_t
notify_post(const char *name)
{
	(void)name;
	return NOTIFY_STATUS_FAILED;
}

uint32_t
notify_monitor_file(int token, const char *name, int flags)
{
	(void)token;
	(void)name;
	(void)flags;
	return NOTIFY_STATUS_FAILED;
}
