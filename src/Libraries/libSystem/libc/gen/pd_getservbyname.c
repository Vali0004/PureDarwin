/*
 * Minimal /etc/services-backed getservbyname.
 *
 * On Darwin this lives in libinfo (OpenDirectory-backed), which PureDarwin
 * does not have yet. Rather than stub it to NULL unconditionally, parse the
 * classic whitespace-separated /etc/services so callers (Xvfb's socket setup,
 * mainly) can resolve well-known service names when the file exists on disk.
 * Not thread-safe (single static result buffer) - enough for bring-up.
 *
 * Format: name  port/proto  [aliases...]
 */
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_LINE_MAX 256
#define SV_MAX_ALIASES 8

static struct servent sv_result;
static char sv_linebuf[SV_LINE_MAX];
static char *sv_aliases[SV_MAX_ALIASES + 1];

struct servent *
getservbyname(const char *name, const char *proto)
{
	FILE *fp;
	char *line;

	fp = fopen("/etc/services", "r");
	if (fp == NULL) {
		return NULL;
	}

	while ((line = fgets(sv_linebuf, sizeof(sv_linebuf), fp)) != NULL) {
		char *cursor = line;
		char *tok_name, *tok_portproto, *slash;
		int port;
		int nalias = 0;

		if (*cursor == '#' || *cursor == '\n' || *cursor == '\0') {
			continue;
		}

		tok_name = strtok(cursor, " \t\r\n");
		if (tok_name == NULL) {
			continue;
		}
		tok_portproto = strtok(NULL, " \t\r\n");
		if (tok_portproto == NULL) {
			continue;
		}

		if (strcmp(tok_name, name) != 0) {
			continue;
		}

		slash = strchr(tok_portproto, '/');
		if (slash == NULL) {
			continue;
		}
		*slash = '\0';
		port = atoi(tok_portproto);

		if (proto != NULL && strcmp(slash + 1, proto) != 0) {
			continue;
		}

		while (nalias < SV_MAX_ALIASES) {
			char *alias = strtok(NULL, " \t\r\n");
			if (alias == NULL) {
				break;
			}
			sv_aliases[nalias++] = alias;
		}
		sv_aliases[nalias] = NULL;

		sv_result.s_name = tok_name;
		sv_result.s_aliases = sv_aliases;
		sv_result.s_port = htons((unsigned short)port);
		sv_result.s_proto = slash + 1;

		fclose(fp);
		return &sv_result;
	}

	fclose(fp);
	return NULL;
}
