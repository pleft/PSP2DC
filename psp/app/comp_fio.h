/*
 * The few file calls comp_files.c needs, so that it runs on the PSP
 * (comp_fio_psp.c: sceIo) and on the computer (scripts/dev/companion-sim:
 * POSIX). Paths are whole ones, under fio_root().
 */
#ifndef PSP2DC_COMP_FIO_H
#define PSP2DC_COMP_FIO_H

const char *fio_root(void);                       /* where the programs' folders are */
int  fio_open_read(const char *path);             /* a handle (>= 0), or < 0 */
int  fio_open_write(const char *path);            /* made, or emptied */
int  fio_read_at(int fd, unsigned offset, void *buf, int n);   /* bytes read, < 0 on failure */
int  fio_write(int fd, const void *buf, int n);   /* bytes written */
void fio_close(int fd);
int  fio_remove(const char *path);                /* 0, or < 0 */
int  fio_rename(const char *from, const char *to);
int  fio_mkdir(const char *path);                 /* (there already: fine) */
long long fio_size(const char *path);             /* < 0: no such file */
/* each entry of a folder but "." and "..": 0, or < 0 when it cannot be read */
int  fio_list(const char *dir, void (*each)(void *ctx, const char *name, int is_dir, long long size), void *ctx);
long long fio_free(void);                         /* bytes free on the Memory Stick, < 0 when not known */

#endif
