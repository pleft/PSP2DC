/* psp/app/comp_fio.h on the computer: the Memory Stick is a folder (sim_files_root). */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../../psp/app/comp_fio.h"
#include "sim.h"

static char root[1024];
static long long free_bytes = -1;
static int fail_writes;

void sim_files_root(const char *dir)
{
    snprintf(root, sizeof(root), "%s", dir);
    mkdir(dir, 0777);
}
void sim_files_free(long long bytes) { free_bytes = bytes; }
void sim_files_fail(int on) { fail_writes = on; }

const char *fio_root(void)
{
    if (!root[0]) sim_files_root(getenv("PSP2DC_SIM_FILES") ? getenv("PSP2DC_SIM_FILES") : "psp2dc-sim-files");
    return root;
}
int fio_open_read(const char *path) { return open(path, O_RDONLY); }
int fio_open_write(const char *path) { return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666); }
int fio_read_at(int fd, unsigned offset, void *buf, int n) { return (int)pread(fd, buf, (size_t)n, (off_t)offset); }
int fio_write(int fd, const void *buf, int n)
{
    if (fail_writes) return -1;
    return (int)write(fd, buf, (size_t)n);
}
void fio_close(int fd) { close(fd); }
int fio_remove(const char *path) { return unlink(path) < 0 ? -1 : 0; }
int fio_rename(const char *from, const char *to)
{
    struct stat st;
    if (stat(to, &st) == 0) return -1;    /* (as if the PSP's would not write over a file: comp_files.c removes it first) */
    return rename(from, to) < 0 ? -1 : 0;
}
int fio_mkdir(const char *path) { mkdir(path, 0777); return 0; }
long long fio_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) return -1;
    return (long long)st.st_size;
}
int fio_list(const char *dir, void (*each)(void *ctx, const char *name, int is_dir, long long size), void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char p[2048];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        if (stat(p, &st) < 0) continue;
        each(ctx, e->d_name, S_ISDIR(st.st_mode), (long long)st.st_size);
    }
    closedir(d);
    return 0;
}
long long fio_free(void) { return free_bytes; }
