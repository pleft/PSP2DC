/* For building dreamcast/libpsp2dc on the computer (../test.c): the few
 * things of KallistiOS it uses, with KallistiOS's own checks. */
#ifndef SIM_KOS_H
#define SIM_KOS_H
#include <stdint.h>
#include <dc/maple.h>
void thd_sleep(int ms);
/* and what dreamcast/demo uses of the television and the controller */
#define KOS_INIT_FLAGS(flags)
extern uint16_t *vram_s;
void vid_waitvbl(void);
void bfont_draw_str(void *buffer, int width, int opaque, const char *str);
#endif
