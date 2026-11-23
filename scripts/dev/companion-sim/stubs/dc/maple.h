/* See ../kos.h. */
#ifndef SIM_DC_MAPLE_H
#define SIM_DC_MAPLE_H
#include <stdint.h>

#define MAPLE_FUNC_MEMCARD      0x02000000
#define MAPLE_FUNC_CONTROLLER   0x01000000
#define CONT_B                  (1 << 1)
#define CONT_A                  (1 << 2)
#define CONT_START              (1 << 3)
#define CONT_DPAD_UP            (1 << 4)
#define CONT_DPAD_DOWN          (1 << 5)
#define CONT_DPAD_LEFT          (1 << 6)
#define CONT_DPAD_RIGHT         (1 << 7)
#define MAPLE_RESPONSE_DATATRF  8
#define MAPLE_RESPONSE_NONE     (-1)
#define MAPLE_RESPONSE_AGAIN    (-4)
#define MAPLE_FRAME_VACANT      0
#define MAPLE_FRAME_UNSENT      1
#define MAPLE_FRAME_SENT        2
#define MAPLE_FRAME_RESPONDED   3

typedef struct maple_state maple_state_t;
typedef struct maple_frame {
    int cmd, dst_port, dst_unit, length;
    volatile int state;
    int queued;
    uint32_t *send_buf;
    uint8_t *recv_buf;
    void (*callback)(maple_state_t *, struct maple_frame *);
    uint8_t recv_buf_arr[1024 + 32];
} maple_frame_t;
typedef struct {
    int8_t response;
    uint8_t dst_addr, src_addr, data_len;
    uint8_t data[];
} maple_response_t;
typedef struct { uint32_t functions; } maple_devinfo_t;
typedef struct { int port, unit; maple_devinfo_t info; } maple_device_t;

typedef struct { uint32_t buttons; int ltrig, rtrig; } cont_state_t;
void *maple_dev_status(maple_device_t *dev);
maple_device_t *maple_enum_dev(int port, int unit);
maple_device_t *maple_enum_type(int n, uint32_t func);
int  maple_frame_trylock(maple_frame_t *frame);
void maple_frame_init(maple_frame_t *frame);
void maple_frame_unlock(maple_frame_t *frame);
int  maple_queue_frame(maple_frame_t *frame);
#endif
