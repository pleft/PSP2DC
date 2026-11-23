/*
 * The companion's files (psp2dc_protocol.h; docs/psp2dc-companion.md,
 * Appendix E): a Dreamcast program's own files on the Memory Stick.
 *
 * The reader thread hands over the records (comp_files_record, from
 * companion.c) and the start of each session; a thread of its own does the
 * work (comp_files_work), so neither the link nor the screen waits on the
 * Memory Stick. Its answers go out through the function given to
 * comp_files_init, a record at a time, from that thread. Nothing here is the
 * PSP's own (comp_fio.h), so the same file runs on the computer.
 */
#ifndef PSP2DC_COMP_FILES_H
#define PSP2DC_COMP_FILES_H

/* send: one whole record for the Dreamcast (at most PSP2DC_COMP_UP_MAX bytes) */
void comp_files_init(int (*send)(const unsigned char *rec, int len));
void comp_files_reset(void);                                         /* (the reader thread) */
void comp_files_record(int type, const unsigned char *d, int len);   /* (the reader thread) */
int  comp_files_work(void);   /* what has come: 1 when there was something, 0 when there was not */

typedef struct {
    unsigned written, read;      /* bytes, since the start */
    unsigned ops, failed;        /* operations ended, and of them not with OK */
    unsigned dropped;            /* records with no room to wait in (each makes its operation fail) */
} CompFilesStats;
extern CompFilesStats g_comp_files;

#endif
