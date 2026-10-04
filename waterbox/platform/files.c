/* files.c - the engine's files are the machine's: what the host mounts, by
 * name. The link wraps the calls the engine makes (fopen, access, stat,
 * remove), the same in both builds:
 *
 *   - a path is the mount's name: the engine builds "./srb2.pk3" from its
 *     WAD folder ".", and miniBox finds a file by its exact name, so a
 *     leading "./" goes;
 *   - nothing is written (until the machine has a filesystem of its own,
 *     milestone 3): a write-mode fopen fails, a remove does nothing, and
 *     access says nothing is writable - so the native reference cannot
 *     write the host's files where the sandbox could not;
 *   - access is answered by opening the file: miniBox has no access(2). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

FILE *__real_fopen(const char *path, const char *mode);
int __real_stat(const char *path, struct stat *st);

static const char *machine_path(const char *path)
{
	while (path && path[0] == '.' && path[1] == '/')
		path += 2;
	return path;
}

static int writes(const char *mode)
{
	return strpbrk(mode, "wa+") != NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (writes(mode))
		return NULL;
	return __real_fopen(machine_path(path), mode);
}

int __wrap_access(const char *path, int mode)
{
	if (mode & W_OK)
		return -1;
	FILE *f = __real_fopen(machine_path(path), "rb");
	if (!f)
		return -1;
	fclose(f);
	return 0;
}

int __wrap_stat(const char *path, struct stat *st)
{
	return __real_stat(machine_path(path), st);
}

int __wrap_remove(const char *path)
{
	(void)path;
	return -1;
}
