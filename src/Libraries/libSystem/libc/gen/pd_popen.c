/*
 * Minimal real popen/pclose.
 *
 * PureDarwin's libc does not build the FreeBSD-derived gen/FreeBSD/popen.c
 * as-is: that implementation goes through posix_spawn_file_actions_t and
 * socketpair() for its "r+" mode, neither of which this from-scratch libc
 * has wired up yet. BusyBox's awk applet only ever opens pipes in "r" or
 * "w" mode (never "r+"), so this is a plain fork()+pipe()+dup2()+execve()
 * implementation covering exactly that surface, built on primitives
 * (_fork, _pipe, _dup2, _execve, _waitpid) that are already part of this
 * stub's exported symbol set.
 */
#include <errno.h>
#include <paths.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <crt_externs.h>
#define environ (*_NSGetEnviron())

struct pd_popen_entry {
	struct pd_popen_entry *next;
	FILE *fp;
	pid_t pid;
};

static struct pd_popen_entry *pd_popen_list;
static pthread_mutex_t pd_popen_lock = PTHREAD_MUTEX_INITIALIZER;

FILE *
popen(const char *command, const char *type)
{
	int pdes[2];
	pid_t pid;
	FILE *iop;
	int parent_fd, child_fd, child_target_fd;
	struct pd_popen_entry *ent;

	if (command == NULL || type == NULL
	 || (*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return NULL;
	}

	if (pipe(pdes) < 0)
		return NULL;

	if (*type == 'r') {
		parent_fd = pdes[0];
		child_fd = pdes[1];
		child_target_fd = STDOUT_FILENO;
	} else {
		parent_fd = pdes[1];
		child_fd = pdes[0];
		child_target_fd = STDIN_FILENO;
	}

	ent = malloc(sizeof(*ent));
	if (ent == NULL) {
		close(pdes[0]);
		close(pdes[1]);
		return NULL;
	}

	pid = fork();
	if (pid < 0) {
		free(ent);
		close(pdes[0]);
		close(pdes[1]);
		return NULL;
	}

	if (pid == 0) {
		/* child */
		close(parent_fd);
		if (child_fd != child_target_fd) {
			dup2(child_fd, child_target_fd);
			close(child_fd);
		}
		{
			char *argv[] = { "sh", "-c", (char *)command, NULL };
			execve(_PATH_BSHELL, argv, environ);
		}
		_exit(127);
	}

	/* parent */
	close(child_fd);
	iop = fdopen(parent_fd, type);
	if (iop == NULL) {
		int saved_errno = errno;
		close(parent_fd);
		free(ent);
		errno = saved_errno;
		return NULL;
	}

	ent->fp = iop;
	ent->pid = pid;

	pthread_mutex_lock(&pd_popen_lock);
	ent->next = pd_popen_list;
	pd_popen_list = ent;
	pthread_mutex_unlock(&pd_popen_lock);

	return iop;
}

int
pclose(FILE *iop)
{
	struct pd_popen_entry *ent, **prev;
	pid_t pid = -1;
	int status;

	pthread_mutex_lock(&pd_popen_lock);
	prev = &pd_popen_list;
	for (ent = pd_popen_list; ent != NULL; ent = ent->next) {
		if (ent->fp == iop) {
			*prev = ent->next;
			pid = ent->pid;
			break;
		}
		prev = &ent->next;
	}
	pthread_mutex_unlock(&pd_popen_lock);

	if (pid == -1) {
		errno = ECHILD;
		return -1;
	}

	free(ent);
	fclose(iop);

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	return status;
}
