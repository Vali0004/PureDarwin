/*
 * Minimal /etc/passwd-backed getpwnam/getpwuid.
 *
 * On Darwin these live in libinfo (OpenDirectory-backed), which PureDarwin does
 * not have yet. Rather than stub them to NULL, parse the classic colon-separated
 * /etc/passwd so BusyBox (ls -l, id, whoami) can resolve names for accounts that
 * exist on disk. Not thread-safe and no getpwent() iteration state beyond a
 * single static buffer -- enough for the single-user bring-up shell. Replace
 * with real libinfo when it exists.
 *
 * Format: name:passwd:uid:gid:gecos:dir:shell
 */
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW_LINE_MAX 1024

static struct passwd pw_result;
static char pw_linebuf[PW_LINE_MAX];
static FILE *pw_iter;

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
		/* last field: trim trailing newline, then terminate iteration */
		char *nl = strpbrk(start, "\r\n");
		if (nl != NULL) {
			*nl = '\0';
		}
		*cursor = NULL;
	}
	return start;
}

/* Parse one /etc/passwd line into pw_result (backed by pw_linebuf). */
static struct passwd *
parse_line(const char *line)
{
	char *cursor;
	char *name, *passwd, *uid, *gid, *gecos, *dir, *shell;

	strncpy(pw_linebuf, line, sizeof(pw_linebuf) - 1);
	pw_linebuf[sizeof(pw_linebuf) - 1] = '\0';
	cursor = pw_linebuf;

	name   = next_field(&cursor);
	passwd = next_field(&cursor);
	uid    = next_field(&cursor);
	gid    = next_field(&cursor);
	gecos  = next_field(&cursor);
	dir    = next_field(&cursor);
	shell  = next_field(&cursor);

	if (name == NULL || uid == NULL || gid == NULL) {
		return NULL;
	}

	pw_result.pw_name   = name;
	pw_result.pw_passwd = passwd ? passwd : (char *)"";
	pw_result.pw_uid    = (uid_t)strtoul(uid, NULL, 10);
	pw_result.pw_gid    = (gid_t)strtoul(gid, NULL, 10);
	pw_result.pw_change = 0;
	pw_result.pw_class  = (char *)"";
	pw_result.pw_gecos  = gecos ? gecos : (char *)"";
	pw_result.pw_dir    = dir ? dir : (char *)"";
	pw_result.pw_shell  = shell ? shell : (char *)"";
	pw_result.pw_expire = 0;
	return &pw_result;
}

static struct passwd *
lookup(const char *want_name, uid_t want_uid, int by_name)
{
	FILE *f;
	char line[PW_LINE_MAX];
	struct passwd *result = NULL;

	f = fopen("/etc/passwd", "r");
	if (f == NULL) {
		return NULL;
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		struct passwd *pw;

		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		pw = parse_line(line);
		if (pw == NULL) {
			continue;
		}
		if (by_name) {
			if (want_name != NULL && strcmp(pw->pw_name, want_name) == 0) {
				result = pw;
				break;
			}
		} else if (pw->pw_uid == want_uid) {
			result = pw;
			break;
		}
	}
	fclose(f);
	return result;
}

struct passwd *
getpwent(void)
{
	char line[PW_LINE_MAX];

	if (pw_iter == NULL) {
		pw_iter = fopen("/etc/passwd", "r");
		if (pw_iter == NULL) {
			return NULL;
		}
	}
	while (fgets(line, sizeof(line), pw_iter) != NULL) {
		struct passwd *pw;

		if (line[0] == '#' || line[0] == '\n') {
			continue;
		}
		pw = parse_line(line);
		if (pw != NULL) {
			return pw;
		}
	}
	return NULL;
}

void
setpwent(void)
{
	if (pw_iter != NULL) {
		rewind(pw_iter);
		return;
	}
	pw_iter = fopen("/etc/passwd", "r");
}

int
setpassent(int stayopen)
{
	(void)stayopen;
	setpwent();
	return pw_iter != NULL;
}

void
endpwent(void)
{
	if (pw_iter != NULL) {
		fclose(pw_iter);
		pw_iter = NULL;
	}
}

struct passwd *
getpwnam(const char *name)
{
	return lookup(name, 0, 1);
}

struct passwd *
getpwuid(uid_t uid)
{
	return lookup(NULL, uid, 0);
}
