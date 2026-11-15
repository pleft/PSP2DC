/*
 * PSP2DC protocol: what the PSP application and the adapter firmware say to
 * each other. Shared by psp/app (PSP side) and the joypad-os host driver
 * (firmware/joypad-os/src/usb/usbh/psp2dc).
 *
 * Transport: PSPExp's USB link (pspexp_usb.prx): a vendor-specific interface
 * with one bulk IN and one bulk OUT endpoint, each direction a byte stream.
 * The host opens the link by sending the 16-byte sync packet and the driver
 * echoes it (see PSPExp protocol/pspexp_usb_link.h); the values below repeat
 * the few constants the firmware needs so it does not depend on PSPExp.
 *
 * After the sync, both sides exchange messages:
 *
 *     byte 0  'D'
 *     byte 1  'C'
 *     byte 2  type
 *     byte 3  payload length (0..255)
 *     ...     payload
 *
 * Every message is written as one USB transfer. Message sizes are chosen so
 * that no message is a multiple of 64 bytes (the full-speed packet size): the
 * transfer then always ends with a short packet, which is what completes the
 * receiver's read. Keep that true when adding messages.
 *
 * The RP2040 host runs one non-interrupt transfer at a time, so the firmware
 * never has an IN and an OUT pending together: it reads a message, sends
 * whatever it has waiting, then reads again. The PSP sends INPUT every frame,
 * which is what gives the firmware its chances to send.
 */
#ifndef PSP2DC_PROTOCOL_H
#define PSP2DC_PROTOCOL_H

#include <stdint.h>

#define PSP2DC_PROTOCOL_VERSION 1

/* the PSPExp link under it */
#define PSP2DC_USB_VID      0x054Cu
#define PSP2DC_USB_PID      0x01CAu
#define PSP2DC_USB_SYNC_LEN 16
#define PSP2DC_USB_SYNC_BYTES { 'S','Y','N','C','P','S','P','E','X','P',0x01,0,0,0,0,0 }
#define PSP2DC_USB_PACKET_FS 64

#define PSP2DC_MAGIC0 'D'
#define PSP2DC_MAGIC1 'C'
#define PSP2DC_HEADER_LEN 4

/* PSP -> adapter */
#define PSP2DC_MSG_HELLO  0x01   /* once after each sync */
#define PSP2DC_MSG_INPUT  0x02   /* every frame */
/* adapter -> PSP */
#define PSP2DC_MSG_STATUS 0x10   /* about once a second */
#define PSP2DC_MSG_LCD    0x11   /* the VMU's screen, each time the Dreamcast draws one */

/* HELLO payload */
typedef struct {
    uint8_t version;      /* PSP2DC_PROTOCOL_VERSION */
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcHello;

/* INPUT payload: the Dreamcast controller state the PSP wants presented.
 * The PSP does the mapping (and, later, the profiles), so the adapter only
 * forwards. Sticks: 0 = left / up, 128 = centre, 255 = right / down.
 * Triggers: 0 = released, 255 = fully pressed. */
typedef struct {
    uint16_t buttons;     /* PSP2DC_BTN_*, little endian */
    uint8_t  stick_x, stick_y;
    uint8_t  trigger_l, trigger_r;
    uint8_t  seq;         /* +1 each message */
    uint8_t  reserved;
} __attribute__((packed)) Psp2dcInput;

#define PSP2DC_BTN_A     0x0001
#define PSP2DC_BTN_B     0x0002
#define PSP2DC_BTN_X     0x0004
#define PSP2DC_BTN_Y     0x0008
#define PSP2DC_BTN_START 0x0010
#define PSP2DC_BTN_UP    0x0020
#define PSP2DC_BTN_DOWN  0x0040
#define PSP2DC_BTN_LEFT  0x0080
#define PSP2DC_BTN_RIGHT 0x0100
#define PSP2DC_BTN_Z     0x0200
#define PSP2DC_BTN_C     0x0400
#define PSP2DC_BTN_D     0x0800

/* STATUS payload */
typedef struct {
    uint32_t uptime_ms;   /* adapter's clock, little endian */
    uint8_t  version;     /* PSP2DC_PROTOCOL_VERSION */
    uint8_t  flags;       /* reserved */
    uint8_t  reserved[2];
} __attribute__((packed)) Psp2dcStatus;

/* LCD payload: the screen as the Dreamcast sent it. 48x32 dots, 1 bit a dot
 * (1 = dark), 6 bytes a row, the most significant bit the leftmost dot. It is
 * upside down for someone holding the controller, where the VMU sits reversed:
 * the PSP turns it half round to show it. */
#define PSP2DC_LCD_BYTES 192
#define PSP2DC_LCD_WIDTH  48
#define PSP2DC_LCD_HEIGHT 32

#endif /* PSP2DC_PROTOCOL_H */
