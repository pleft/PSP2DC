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
 * A USB transfer carries one message, or several one after the other (a
 * memory card block is four CARD_PART messages in one transfer). Sizes are
 * chosen so that no transfer is a multiple of 64 bytes (the full-speed packet
 * size): it then always ends with a short packet, which is what completes the
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

#define PSP2DC_PROTOCOL_VERSION 4

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
#define PSP2DC_MSG_HELLO     0x01   /* once after each sync */
#define PSP2DC_MSG_INPUT     0x02   /* every frame */
#define PSP2DC_MSG_CARD_PART 0x03   /* the memory card going up, a quarter block a message */
#define PSP2DC_MSG_CARD_DONE 0x04   /* ... and its end (or: there is no card file) */
#define PSP2DC_MSG_CARD_EJECT 0x05  /* take the card out: another one is to be put in */
/* adapter -> PSP */
#define PSP2DC_MSG_STATUS     0x10   /* about once a second */
#define PSP2DC_MSG_CARD_STATE 0x12   /* the answer to HELLO: whether the adapter holds a card */
#define PSP2DC_MSG_CARD_SAVE  0x13   /* a quarter block of the card to keep (a CARD_PART payload) */
#define PSP2DC_MSG_LCD    0x11   /* the VMU's screen, each time the Dreamcast draws one */

/* HELLO payload */
typedef struct {
    uint8_t version;      /* PSP2DC_PROTOCOL_VERSION */
    uint8_t flags;        /* PSP2DC_HELLO_HAS_CARD */
    uint8_t reserved[2];
} __attribute__((packed)) Psp2dcHello;
#define PSP2DC_HELLO_HAS_CARD 0x01   /* the PSP has a card file to send */

/*
 * The memory card. The adapter answers the Dreamcast from a copy in its RAM
 * (it has microseconds to answer) and writes nothing to flash; the PSP keeps
 * the card as a file on its Memory Stick, a plain 128 KB image (256 blocks of
 * 512 bytes, the usual VMU dump).
 *
 *   PSP      HELLO (has a card file or not)
 *   adapter  CARD_STATE: EMPTY, it holds none yet (just powered on), or
 *            LOADED, it holds one from earlier in this session
 *   EMPTY:   the PSP sends its file as 1024 CARD_PART (four to a transfer),
 *            then CARD_DONE; with no file, CARD_DONE alone, and the adapter
 *            keeps its freshly formatted card. Only now does the adapter show
 *            the Dreamcast a memory card.
 *   LOADED:  the PSP sends nothing: the adapter's copy is the newer one.
 *   Changing card (the PSP holds several card files): the PSP sends
 *            CARD_EJECT once every block has reached it. The adapter takes
 *            the card away from the Dreamcast, as a VMU pulled out, and
 *            answers CARD_STATE EMPTY; the PSP sends the other card as above,
 *            and the adapter shows it to the Dreamcast a second later at the
 *            earliest, so the Dreamcast sees the card change. While blocks
 *            are still on their way the adapter refuses: CARD_STATE LOADED.
 *   Then, and from then on: the adapter sends every block the PSP may not
 *            have (all of them once, after LOADED or a CARD_DONE without a
 *            file; afterwards each block a game writes) as CARD_SAVE, and the
 *            PSP writes its file a moment after they stop coming.
 */
#define PSP2DC_CARD_BLOCKS     256
#define PSP2DC_CARD_BLOCK_SIZE 512
#define PSP2DC_CARD_PART_SIZE  128
#define PSP2DC_CARD_PARTS      4
#define PSP2DC_CARD_BYTES      (PSP2DC_CARD_BLOCKS * PSP2DC_CARD_BLOCK_SIZE)

/* CARD_PART and CARD_SAVE payload */
typedef struct {
    uint8_t block;        /* 0..255 */
    uint8_t part;         /* 0..3 */
    uint8_t data[PSP2DC_CARD_PART_SIZE];
} __attribute__((packed)) Psp2dcCardPart;
/* one block: four messages in one transfer (536 bytes, not a multiple of 64) */
#define PSP2DC_CARD_BLOCK_XFER (PSP2DC_CARD_PARTS * (PSP2DC_HEADER_LEN + sizeof(Psp2dcCardPart)))

/* CARD_DONE payload */
typedef struct {
    uint8_t sent;         /* 1: a card was sent; 0: the PSP has no file */
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcCardDone;

/* CARD_STATE payload */
typedef struct {
    uint8_t state;        /* PSP2DC_CARD_EMPTY / PSP2DC_CARD_LOADED */
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcCardState;
#define PSP2DC_CARD_EMPTY  0
#define PSP2DC_CARD_LOADED 1

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
    uint8_t  flags;       /* PSP2DC_STATUS_CARD */
    /* what the Dreamcast asked of the VMU's screen, for the status page */
    uint8_t  lcd_media;       /* media info requests for the LCD (wraps) */
    uint8_t  lcd_last_words;  /* the last LCD write's length in words (50 = a screen) */
    uint16_t lcd_writes;      /* block writes to the LCD */
    uint16_t lcd_screens;     /* of those, taken as a screen */
    uint32_t lcd_last_addr;   /* the last LCD write's block word (0 = the screen) */
    uint16_t card_pending;    /* blocks of the card still to be sent to the PSP */
} __attribute__((packed)) Psp2dcStatus;
#define PSP2DC_STATUS_CARD 0x01   /* the adapter holds a card and shows it to the Dreamcast */

/* LCD payload: the screen as the Dreamcast sent it. 48x32 dots, 1 bit a dot
 * (1 = dark), 6 bytes a row, the most significant bit the leftmost dot. It is
 * upside down for someone holding the controller, where the VMU sits reversed:
 * the PSP turns it half round to show it. */
#define PSP2DC_LCD_BYTES 192
#define PSP2DC_LCD_WIDTH  48
#define PSP2DC_LCD_HEIGHT 32

#endif /* PSP2DC_PROTOCOL_H */
