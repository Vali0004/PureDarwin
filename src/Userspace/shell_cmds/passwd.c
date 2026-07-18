/*
 * PureDarwin bring-up passwd(1).
 *
 * This intentionally edits /etc/passwd directly. There is no libinfo,
 * master.passwd/pwd_mkdb, shadow file, or crypt(3) policy wired up yet.
 */
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define PASSWD_PATH "/etc/passwd"
#define PASSWD_TMP "/etc/passwd.tmp"
#define LINE_MAX_LEN 2048
#define PASS_MAX_LEN 256

static void
usage(void)
{
	fprintf(stderr, "usage: passwd [-p password] [user]\n");
	exit(2);
}

static const char *
default_user(void)
{
	struct passwd *pw;
	const char *user;

	user = getenv("USER");
	if (user != NULL && user[0] != '\0') {
		return user;
	}
	pw = getpwuid(getuid());
	if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0') {
		return pw->pw_name;
	}
	return "root";
}

static int
same_password(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

static int
read_password_once(const char *prompt, char *buf, size_t buflen)
{
	struct termios oldt, newt;
	int have_termios = 0;
	size_t len;

	if (buflen == 0) {
		return -1;
	}
	fprintf(stderr, "%s", prompt);
	fflush(stderr);
	if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0) {
		newt = oldt;
		newt.c_lflag &= ~ECHO;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt) == 0) {
			have_termios = 1;
		}
	}
	if (fgets(buf, (int)buflen, stdin) == NULL) {
		if (have_termios) {
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
		}
		fprintf(stderr, "\n");
		return -1;
	}
	if (have_termios) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
		fprintf(stderr, "\n");
	}
	len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
		buf[--len] = '\0';
	}
	return 0;
}

static int
read_new_password(char *buf, size_t buflen)
{
	char again[PASS_MAX_LEN];

	if (read_password_once("New password: ", buf, buflen) != 0) {
		return -1;
	}
	if (buf[0] == '\0') {
		fprintf(stderr, "passwd: empty passwords are not allowed\n");
		return -1;
	}
	if (read_password_once("Retype new password: ", again, sizeof(again)) != 0) {
		return -1;
	}
	if (!same_password(buf, again)) {
		fprintf(stderr, "passwd: passwords do not match\n");
		return -1;
	}
	return 0;
}

static int
write_updated_line(FILE *out, const char *line, const char *user,
    const char *password)
{
	const char *field;
	const char *colon;
	const char *rest;

	colon = strchr(line, ':');
	if (colon == NULL) {
		return fputs(line, out) == EOF ? -1 : 0;
	}
	if ((size_t)(colon - line) != strlen(user) ||
	    strncmp(line, user, (size_t)(colon - line)) != 0) {
		return fputs(line, out) == EOF ? -1 : 0;
	}
	field = colon + 1;
	rest = strchr(field, ':');
	if (rest == NULL) {
		return fputs(line, out) == EOF ? -1 : 0;
	}
	if (fprintf(out, "%.*s:plain:%s%s", (int)(colon - line), line,
	    password, rest) < 0) {
		return -1;
	}
	return 1;
}

static int
update_passwd(const char *user, const char *password)
{
	FILE *in;
	FILE *out;
	char line[LINE_MAX_LEN];
	int changed = 0;
	int rc = -1;

	in = fopen(PASSWD_PATH, "r");
	if (in == NULL) {
		fprintf(stderr, "passwd: %s: %s\n", PASSWD_PATH, strerror(errno));
		return -1;
	}
	out = fopen(PASSWD_TMP, "w");
	if (out == NULL) {
		fprintf(stderr, "passwd: %s: %s\n", PASSWD_TMP, strerror(errno));
		fclose(in);
		return -1;
	}
	while (fgets(line, sizeof(line), in) != NULL) {
		int wr = write_updated_line(out, line, user, password);
		if (wr < 0) {
			fprintf(stderr, "passwd: write failed: %s\n", strerror(errno));
			goto out;
		}
		if (wr > 0) {
			changed = 1;
		}
	}
	if (ferror(in)) {
		fprintf(stderr, "passwd: read failed: %s\n", strerror(errno));
		goto out;
	}
	if (!changed) {
		fprintf(stderr, "passwd: user '%s' not found\n", user);
		goto out;
	}
	if (fflush(out) != 0) {
		fprintf(stderr, "passwd: flush failed: %s\n", strerror(errno));
		goto out;
	}
	if (chmod(PASSWD_TMP, 0644) != 0) {
		fprintf(stderr, "passwd: chmod %s: %s\n", PASSWD_TMP, strerror(errno));
		goto out;
	}
	if (rename(PASSWD_TMP, PASSWD_PATH) != 0) {
		fprintf(stderr, "passwd: rename %s: %s\n", PASSWD_TMP, strerror(errno));
		goto out;
	}
	rc = 0;

out:
	fclose(in);
	fclose(out);
	if (rc != 0) {
		unlink(PASSWD_TMP);
	}
	return rc;
}

int
main(int argc, char **argv)
{
	const char *password = NULL;
	const char *user;
	char prompted[PASS_MAX_LEN];
	int ch;

	while ((ch = getopt(argc, argv, "p:")) != -1) {
		switch (ch) {
		case 'p':
			password = optarg;
			break;
		default:
			usage();
		}
	}
	if (argc - optind > 1) {
		usage();
	}
	user = argc == optind ? default_user() : argv[optind];
	if (geteuid() != 0 && strcmp(user, default_user()) != 0) {
		fprintf(stderr, "passwd: permission denied\n");
		return 1;
	}
	if (password == NULL) {
		if (read_new_password(prompted, sizeof(prompted)) != 0) {
			return 1;
		}
		password = prompted;
	}
	if (password[0] == '\0') {
		fprintf(stderr, "passwd: empty passwords are not allowed\n");
		return 1;
	}
	if (update_passwd(user, password) != 0) {
		return 1;
	}
	printf("passwd: password updated for %s\n", user);
	return 0;
}
