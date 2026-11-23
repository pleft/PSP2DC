/* comp_fio.h on the PSP. */
#include <string.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include "comp_fio.h"

const char *fio_root(void) { return "ms0:/PSP2DC/games"; }
int fio_open_read(const char *path) { return sceIoOpen(path, PSP_O_RDONLY, 0); }
int fio_open_write(const char *path) { return sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777); }
int fio_read_at(int fd, unsigned offset, void *buf, int n)
{
    if (sceIoLseek(fd, offset, PSP_SEEK_SET) != (SceOff)offset) return -1;
    return sceIoRead(fd, buf, n);
}
int fio_write(int fd, const void *buf, int n) { return sceIoWrite(fd, buf, n); }
void fio_close(int fd) { sceIoClose(fd); }
int fio_remove(const char *path) { return sceIoRemove(path) < 0 ? -1 : 0; }
int fio_rename(const char *from, const char *to) { return sceIoRename(from, to) < 0 ? -1 : 0; }
int fio_mkdir(const char *path) { sceIoMkdir(path, 0777); return 0; }
long long fio_size(const char *path)       /* by opening it, as cards.c finds files */
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    SceOff n;
    if (fd < 0) return -1;
    n = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoClose(fd);
    return n < 0 ? -1 : (long long)n;
}
int fio_list(const char *dir, void (*each)(void *ctx, const char *name, int is_dir, long long size), void *ctx)
{
    SceUID d = sceIoDopen(dir);
    SceIoDirent e;
    if (d < 0) return -1;
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
        each(ctx, e.d_name, FIO_S_ISDIR(e.d_stat.st_mode), (long long)e.d_stat.st_size);
    }
    sceIoDclose(d);
    return 0;
}
long long fio_free(void)                   /* the SDK's SCE_PR_GETDEV (pspiofilemgr_devctl.h) */
{
    SceDevInf inf;
    SceDevctlCmd cmd;
    memset(&inf, 0, sizeof(inf));
    cmd.dev_inf = &inf;
    if (sceIoDevctl("ms0:", SCE_PR_GETDEV, &cmd, sizeof(cmd), NULL, 0) < 0) return -1;
    return (long long)inf.freeClusters * inf.sectorCount * inf.sectorSize;
}
