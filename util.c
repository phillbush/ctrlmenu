#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctrlmenu.h"

#define SHELL "sh"

int
max(int x, int y)
{
	return x > y ? x : y;
}

int
min(int x, int y)
{
	return x < y ? x : y;
}

void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

void *
erealloc(void *ptr, size_t size)
{
	void *p;

	if ((p = realloc(ptr, size)) == NULL)
		err(1, "realloc");
	return p;
}

char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

char *
estrndup(const char *s, size_t maxlen)
{
	char *t;

	if ((t = strndup(s, maxlen)) == NULL)
		err(1, "strndup");
	return t;
}

void
epipe(int fd[])
{
	if (pipe(fd) == -1) {
		err(1, "pipe");
	}
}

void
eexecshell(const char *cmd, const char *arg)
{
	if (execlp(SHELL, SHELL, "-c", cmd, SHELL, arg, NULL) == -1) {
		err(1, "%s", "execlp");
	}
}

void
edup2(int fd1, int fd2)
{
	if (dup2(fd1, fd2) == -1)
		err(1, "dup2");
	close(fd1);
}

pid_t
efork(void)
{
	pid_t pid;

	if ((pid = fork()) == -1)
		err(1, "fork");
	return pid;
}
