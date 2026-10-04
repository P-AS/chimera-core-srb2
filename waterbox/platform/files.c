/* files.c - the machine's filesystem: what the host mounts, read-only, by
 * name, and what the game writes, kept in the machine's own memory. The link
 * wraps the file calls the engine makes (fopen, access, stat, remove,
 * fileno, fstat, opendir), the same in both builds:
 *
 *   - a path is a name: the engine builds "./gamedata.dat" from its folder
 *     ".", and miniBox finds a mount by its exact name, so a leading "./"
 *     goes;
 *   - a file the game writes is a memory file (malloc'd guest memory: it is
 *     in every savestate and rewinds with the machine), and it shadows a
 *     mount of the same name from then on; a mount opened for update or
 *     append is copied in first. Files written during Init (before seal) are
 *     in the sealed baseline, so a state carries only what changes after;
 *   - nothing reaches the host: no write ever goes to a mount or a host file;
 *   - folders are implicit: a name with a '/' is a file, I_mkdir records the
 *     folder (platform/i_system.c), stat says a recorded folder or one a file
 *     is in is a folder; opendir lists nothing (the machine has no folder of
 *     the host's to list, and the native reference must not list one);
 *   - access is answered by opening: miniBox has no access(2).
 *
 * The memory files are what the core exports as save data (wbx-entry.c's
 * GetSaveData*): everything the game wrote but its configuration. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "chimera-platform.h"

FILE *__real_fopen(const char *path, const char *mode);
int __real_stat(const char *path, struct stat *st);
int __real_fstat(int fd, struct stat *st);
int __real_fileno(FILE *f);

#define MAX_FILES 512
#define MAX_DIRS 64
#define MAX_OPEN 64
/* the descriptors memory files answer with (fileno): above any real one */
#define FAKE_FD 0x40000000

struct memfile
{
	char *name;
	unsigned char *data;
	size_t size, cap;
};

static struct memfile g_files[MAX_FILES];
static int g_nfiles;
static char *g_dirs[MAX_DIRS];
static int g_ndirs;

/* an open memory file: its file (by index, so a file growing moves nothing),
 * the position, and the FILE the engine holds */
struct handle
{
	FILE *f;
	int file;
	size_t pos;
	int writes;
};
static struct handle g_open[MAX_OPEN];

static const char *machine_path(const char *path)
{
	while (path && path[0] == '.' && path[1] == '/')
		path += 2;
	return path;
}

static int find(const char *name)
{
	for (int i = 0; i < g_nfiles; i++)
		if (!strcmp(g_files[i].name, name))
			return i;
	return -1;
}

static int create(const char *name)
{
	if (g_nfiles == MAX_FILES)
		return -1;
	struct memfile *m = &g_files[g_nfiles];
	m->name = strdup(name);
	m->data = NULL;
	m->size = m->cap = 0;
	return m->name ? g_nfiles++ : -1;
}

static int reserve(struct memfile *m, size_t size)
{
	if (size <= m->cap)
		return 0;
	size_t cap = m->cap ? m->cap : 4096;
	while (cap < size)
		cap *= 2;
	unsigned char *data = realloc(m->data, cap);
	if (!data)
		return -1;
	m->data = data;
	m->cap = cap;
	return 0;
}

/* a mount's bytes into a memory file of the same name (update, append) */
static int copy_mount(const char *name)
{
	FILE *f = __real_fopen(name, "rb");
	if (!f)
		return create(name);
	const int i = create(name);
	if (i >= 0)
	{
		unsigned char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		{
			struct memfile *m = &g_files[i];
			if (reserve(m, m->size + n) != 0)
				break;
			memcpy(m->data + m->size, buf, n);
			m->size += n;
		}
	}
	fclose(f);
	return i;
}

/* ---- the FILE of a memory file (fopencookie) */

static ssize_t mf_read(void *cookie, char *buf, size_t size)
{
	struct handle *h = cookie;
	const struct memfile *m = &g_files[h->file];
	if (h->pos >= m->size)
		return 0;
	if (size > m->size - h->pos)
		size = m->size - h->pos;
	memcpy(buf, m->data + h->pos, size);
	h->pos += size;
	return (ssize_t)size;
}

static ssize_t mf_write(void *cookie, const char *buf, size_t size)
{
	struct handle *h = cookie;
	struct memfile *m = &g_files[h->file];
	if (!h->writes)
		return -1;
	if (reserve(m, h->pos + size) != 0)
		return -1;
	if (h->pos > m->size)
		memset(m->data + m->size, 0, h->pos - m->size);
	memcpy(m->data + h->pos, buf, size);
	h->pos += size;
	if (h->pos > m->size)
		m->size = h->pos;
	return (ssize_t)size;
}

static int mf_seek(void *cookie, off_t *offset, int whence)
{
	struct handle *h = cookie;
	const struct memfile *m = &g_files[h->file];
	off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)h->pos : (off_t)m->size;
	if (base + *offset < 0)
		return -1;
	h->pos = (size_t)(base + *offset);
	*offset = (off_t)h->pos;
	return 0;
}

static int mf_close(void *cookie)
{
	struct handle *h = cookie;
	h->f = NULL;
	return 0;
}

static FILE *open_memory(int file, const char *mode, size_t pos, int writes)
{
	int slot = -1;
	for (int i = 0; i < MAX_OPEN; i++)
		if (!g_open[i].f)
		{
			slot = i;
			break;
		}
	if (slot < 0)
	{
		errno = EMFILE;
		return NULL;
	}
	struct handle *h = &g_open[slot];
	h->file = file;
	h->pos = pos;
	h->writes = writes;
	const cookie_io_functions_t io = { mf_read, mf_write, mf_seek, mf_close };
	h->f = fopencookie(h, mode, io);
	return h->f;
}

static struct handle *handle_of(FILE *f)
{
	for (int i = 0; i < MAX_OPEN; i++)
		if (g_open[i].f && g_open[i].f == f)
			return &g_open[i];
	return NULL;
}

/* ---- the calls the engine makes */

FILE *__wrap_fopen(const char *path, const char *mode)
{
	const char *name = machine_path(path);
	if (!name || !*name)
	{
		errno = ENOENT;
		return NULL;
	}
	const int writes = strpbrk(mode, "wa+") != NULL;
	int i = find(name);
	if (!writes)
		return i >= 0 ? open_memory(i, mode, 0, 0) : __real_fopen(name, mode);
	if (mode[0] == 'w')
	{
		if (i < 0)
			i = create(name);
		else
			g_files[i].size = 0;
	}
	else if (i < 0)
	{
		/* update or append: the mount's bytes first, or an empty file for an
		 * append; "r+" of a file that is nowhere fails, as fopen's does */
		FILE *probe = __real_fopen(name, "rb");
		if (probe)
			fclose(probe);
		else if (mode[0] == 'r')
		{
			errno = ENOENT;
			return NULL;
		}
		i = copy_mount(name);
	}
	if (i < 0)
	{
		errno = ENOSPC;
		return NULL;
	}
	return open_memory(i, mode, mode[0] == 'a' ? g_files[i].size : 0, 1);
}

int __wrap_access(const char *path, int mode)
{
	const char *name = machine_path(path);
	if (find(name) >= 0)
		return 0;
	FILE *f = __real_fopen(name, "rb");
	if (!f)
	{
		/* nothing there: writable all the same (it would be a memory file) */
		if (mode & W_OK)
			return 0;
		errno = ENOENT;
		return -1;
	}
	fclose(f);
	return 0;
}

static int is_dir(const char *name)
{
	const size_t n = strlen(name);
	if (n == 0 || !strcmp(name, "."))
		return 1;
	for (int i = 0; i < g_ndirs; i++)
		if (!strcmp(g_dirs[i], name))
			return 1;
	for (int i = 0; i < g_nfiles; i++)
		if (!strncmp(g_files[i].name, name, n) && g_files[i].name[n] == '/')
			return 1;
	return 0;
}

int __wrap_stat(const char *path, struct stat *st)
{
	const char *name = machine_path(path);
	const int i = find(name);
	if (i >= 0 || is_dir(name))
	{
		memset(st, 0, sizeof *st);
		st->st_mode = i >= 0 ? S_IFREG | 0644 : S_IFDIR | 0755;
		st->st_size = i >= 0 ? (off_t)g_files[i].size : 0;
		st->st_nlink = 1;
		return 0;
	}
	return __real_stat(name, st);
}

int __wrap_remove(const char *path)
{
	const int i = find(machine_path(path));
	if (i < 0)
	{
		errno = ENOENT;
		return -1;
	}
	for (int h = 0; h < MAX_OPEN; h++)
		if (g_open[h].f && g_open[h].file == i)
		{
			errno = EBUSY;
			return -1;
		}
	free(g_files[i].name);
	free(g_files[i].data);
	/* the last file into the hole; an open handle of the last follows it */
	const int last = --g_nfiles;
	g_files[i] = g_files[last];
	for (int h = 0; h < MAX_OPEN; h++)
		if (g_open[h].f && g_open[h].file == last)
			g_open[h].file = i;
	return 0;
}

int __wrap_fileno(FILE *f)
{
	struct handle *h = handle_of(f);
	return h ? FAKE_FD + (int)(h - g_open) : __real_fileno(f);
}

int __wrap_fstat(int fd, struct stat *st)
{
	if (fd >= FAKE_FD && fd < FAKE_FD + MAX_OPEN && g_open[fd - FAKE_FD].f)
	{
		memset(st, 0, sizeof *st);
		st->st_mode = S_IFREG | 0644;
		st->st_size = (off_t)g_files[g_open[fd - FAKE_FD].file].size;
		st->st_nlink = 1;
		return 0;
	}
	return __real_fstat(fd, st);
}

DIR *__wrap_opendir(const char *path)
{
	(void)path;
	errno = ENOENT;
	return NULL;
}

void chimera_mkdir(const char *path)
{
	const char *name = machine_path(path);
	if (is_dir(name) || g_ndirs == MAX_DIRS)
		return;
	g_dirs[g_ndirs] = strdup(name);
	if (g_dirs[g_ndirs])
		g_ndirs++;
}

/* ---- the save data: every memory file but the configuration */

static int is_save_data(const struct memfile *m)
{
	return strcmp(m->name, "config.cfg") != 0;
}

int chimera_savedata_count(void)
{
	int n = 0;
	for (int i = 0; i < g_nfiles; i++)
		n += is_save_data(&g_files[i]);
	return n;
}

static const struct memfile *savedata(int index)
{
	for (int i = 0; i < g_nfiles; i++)
		if (is_save_data(&g_files[i]) && index-- == 0)
			return &g_files[i];
	return NULL;
}

const char *chimera_savedata_name(int index)
{
	const struct memfile *m = savedata(index);
	return m ? m->name : "";
}

int64_t chimera_savedata_size(int index)
{
	const struct memfile *m = savedata(index);
	return m ? (int64_t)m->size : 0;
}

const uint8_t *chimera_savedata_buffer(int index)
{
	const struct memfile *m = savedata(index);
	return m ? m->data : NULL;
}
