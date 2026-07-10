/*
 * Minimal /etc/group-backed getgrnam/getgrgid/getgrouplist.
 *
 * Same story as pd_getpwent.c: on Darwin these live in libinfo, which
 * PureDarwin does not have yet. Parse the classic colon-separated /etc/group
 * so BusyBox (ls -l, id) can resolve group names. Not thread-safe; single
 * static result buffer. Replace with real libinfo when it exists.
 *
 * Format: name:passwd:gid:member1,member2,...
 */
#include <grp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GR_LINE_MAX 1024
#define GR_MAX_MEMBERS 32

static struct group gr_result;
static char gr_linebuf[GR_LINE_MAX];
static char *gr_members[GR_MAX_MEMBERS + 1];

static char *
next_field(char **cursor)
{
	char *start = *cursor;
	char *colon;

	if (start == NULL) {
		return NULL;
	}
	colon = strchr(start, ':');
	if (colon != NULL) {
		*colon = '\0';
		*cursor = colon + 1;
	} else {
		char *nl = strpbrk(start, "\r\n");
		if (nl != NULL) {
			*nl = '\0';
		}
		*cursor = NULL;
	}
	return start;
}

/* Parse one /etc/group line into gr_result (backed by gr_linebuf). */
static struct group *
parse_line(const char *line)
{
	char *cursor;
	char *name, *passwd, *gid, *members;
	int n = 0;

	strncpy(gr_linebuf, line, sizeof(gr_linebuf) - 1);
	gr_linebuf[sizeof(gr_linebuf) - 1] = '\0';
	cursor = gr_linebuf;

	name    = next_field(&cursor);
	passwd  = next_field(&cursor);
	gid     = next_field(&cursor);
	members = next_field(&cursor);

	if (name == NULL || gid == NULL) {
		return NULL;
	}

	if (members != NULL) {
		char *m = members;
		while (m != NULL && *m != '\0' && n < GR_MAX_MEMBERS) {
			char *comma = strchr(m, ',');
			if (comma != NULL) {
				*comma = '\0';
			}
			gr_members[n++] = m;
			m = comma ? comma + 1 : NULL;
		}
	}
	gr_members[n] = NULL;

	gr_result.gr_name   = name;
	gr_result.gr_passwd = passwd ? passwd : (char *)"";
	gr_result.gr_gid    = (gid_t)strtoul(gid, NULL, 10);
	gr_result.gr_mem    = gr_members;
	return &gr_result;
}

static struct group *
lookup(const char *want_name, gid_t want_gid, int by_name)
{
	FILE *f;
	char line[GR_LINE_MAX];
	struct group *result = NULL;

	f = fopen("/etc/group", "r");
	if (f == NULL) {
		return NULL;
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		struct group *gr;

		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		gr = parse_line(line);
		if (gr == NULL) {
			continue;
		}
		if (by_name) {
			if (want_name != NULL && strcmp(gr->gr_name, want_name) == 0) {
				result = gr;
				break;
			}
		} else if (gr->gr_gid == want_gid) {
			result = gr;
			break;
		}
	}
	fclose(f);
	return result;
}

struct group *
getgrnam(const char *name)
{
	return lookup(name, 0, 1);
}

struct group *
getgrgid(gid_t gid)
{
	return lookup(NULL, gid, 0);
}

/*
 * Collect groups `name` belongs to, always including `basegid` first.
 * Scans /etc/group membership lists. Returns 0 on success, -1 if the
 * result was truncated (matching the BSD contract).
 */
int
getgrouplist(const char *name, int basegid, int *groups, int *ngroups)
{
	FILE *f;
	char line[GR_LINE_MAX];
	int max = *ngroups;
	int n = 0;
	int truncated = 0;

	if (max > 0) {
		groups[n++] = basegid;
	} else {
		truncated = 1;
	}

	f = fopen("/etc/group", "r");
	if (f != NULL) {
		while (fgets(line, sizeof(line), f) != NULL) {
			struct group *gr;
			char **m;

			if (line[0] == '#' || line[0] == '\n') {
				continue;
			}
			gr = parse_line(line);
			if (gr == NULL || (int)gr->gr_gid == basegid) {
				continue;
			}
			for (m = gr->gr_mem; *m != NULL; m++) {
				if (strcmp(*m, name) != 0) {
					continue;
				}
				if (n < max) {
					groups[n++] = (int)gr->gr_gid;
				} else {
					truncated = 1;
				}
				break;
			}
		}
		fclose(f);
	}
	*ngroups = n;
	return truncated ? -1 : 0;
}
