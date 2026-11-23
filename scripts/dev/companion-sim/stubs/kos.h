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
/* ... and of leaving: the buttons that go back to the Dreamcast's menu (never pressed here) */
typedef void (*cont_btn_callback_t)(uint8_t addr, uint32_t btns);
static inline int cont_btn_callback(uint8_t addr, uint32_t btns, cont_btn_callback_t cb) { (void)addr; (void)btns; (void)cb; return 0; }
void arch_menu(void);
#endif
