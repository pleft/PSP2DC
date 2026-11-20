/* For building dreamcast/libpsp2dc on the computer (../test.c): the few
 * things of KallistiOS it uses, with KallistiOS's own checks. */
#ifndef SIM_KOS_H
#define SIM_KOS_H
#include <stdint.h>
#include <dc/maple.h>
void thd_sleep(int ms);
#endif
