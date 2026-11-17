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

#define PSP2DC_PROTOCOL_VERSION 5

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
#define PSP2DC_MSG_CARD_SEND  0x06  /* the adapter's card is wanted: send its blocks */
/* adapter -> PSP */
#define PSP2DC_MSG_STATUS     0x10   /* about once a second */
#define PSP2DC_MSG_CARD_STATE 0x12   /* the answer to HELLO and CARD_EJECT: the card the adapter holds */
#define PSP2DC_MSG_CARD_SAVE  0x13   /* a quarter block of the card to keep (a CARD_PART payload) */
#define PSP2DC_MSG_LCD    0x11   /* the VMU's screen, each time the Dreamcast draws one */
#define PSP2DC_MSG_GAME_ID 0x14  /* a game's ID, from openMenu as it starts a disc (Psp2dcGameId) */

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
 *            LOADED, it holds one from earlier in this session (the PSP was
 *            unplugged or restarted meanwhile): its name and CRC-32. It
 *            sends no block until the PSP says what it wants.
 *   EMPTY:   the PSP sends its file as 1024 CARD_PART (four to a transfer),
 *            then CARD_DONE with the card's name; with no file, CARD_DONE
 *            alone, and the adapter keeps its freshly formatted card (and
 *            sends it all). Only now does the adapter show the Dreamcast a
 *            memory card.
 *   LOADED:  the PSP decides, from the adapter's CRC, its own copy of that
 *            card and the CRC of the last copy both had (psp/app/main.c):
 *            CARD_SEND to be sent the adapter's blocks (all of them, or only
 *            those the Dreamcast writes from now on), or CARD_EJECT to put in
 *            its own. The adapter's copy is never taken for another card's:
 *            version 4 sent it at once, and a PSP that had changed card
 *            while unplugged wrote it over the other card (2026-11-15).
 *   Changing card (the PSP holds several card files): the PSP sends
 *            CARD_EJECT once every block has reached it (or with DISCARD,
 *            when it has decided the adapter's copy is not needed). The
 *            adapter takes the card away from the Dreamcast, as a VMU pulled
 *            out, and answers CARD_STATE EMPTY; the PSP sends the other card
 *            as above, and the adapter shows it to the Dreamcast a second
 *            later at the earliest, so the Dreamcast sees the card change.
 *            While blocks are still on their way the adapter refuses:
 *            CARD_STATE LOADED.
 *   Then, and from then on: the adapter sends every block the PSP may not
 *            have (all of them once, after CARD_SEND all or a CARD_DONE
 *            without a file; afterwards each block a game writes) as
 *            CARD_SAVE, and the PSP writes its file a moment after they stop
 *            coming.
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

#define PSP2DC_CARD_NAME_LEN 32   /* a card's name, NUL-terminated (the PSP's file name without .bin) */

/* GAME_ID payload (adapter -> PSP, 140 bytes: a 144-byte transfer, not a
 * multiple of 64). openMenu (the Virtual Folder Bundle, vm2_api.c) sends a
 * virtual VMU, before starting a disc, Maple command 0x21 to the memory card:
 * the function word, the disc's product number (12 bytes, NUL-padded, e.g.
 * "T-8101N") and, when it has one, the game's name (128 bytes); "openmenu"
 * at its own start. It sends it only to a VMU whose all-status reply carries
 * a name it knows at byte 112 (the adapter gives USB4Maple's). The PSP may
 * then put that game's own card in. Older PSPs ignore the message. */
typedef struct {
    char id[12];          /* as sent, not NUL-terminated when 12 long */
    char name[128];       /* NUL-padded; empty when openMenu sent none */
} __attribute__((packed)) Psp2dcGameId;

/* CARD_DONE payload */
typedef struct {
    uint8_t sent;         /* 1: a card was sent; 0: the PSP has no file */
    uint8_t reserved[3];
    char    name[PSP2DC_CARD_NAME_LEN];   /* the card's name, kept by the adapter */
} __attribute__((packed)) Psp2dcCardDone;

/* CARD_EJECT payload */
typedef struct {
    uint8_t flags;        /* PSP2DC_EJECT_DISCARD */
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcCardEject;
#define PSP2DC_EJECT_DISCARD 0x01   /* out even with blocks not yet sent: the PSP does not need them */

/* CARD_SEND payload */
typedef struct {
    uint8_t all;          /* 1: every block; 0: only those written from now on */
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcCardSend;

/* CARD_STATE payload */
typedef struct {
    uint8_t  state;       /* PSP2DC_CARD_EMPTY / PSP2DC_CARD_LOADED */
    uint8_t  reserved[3];
    uint32_t crc;         /* LOADED: psp2dc_crc32 of the card's 128 KB, little endian */
    char     name[PSP2DC_CARD_NAME_LEN];   /* LOADED: its name, as CARD_DONE gave it */
} __attribute__((packed)) Psp2dcCardState;
#define PSP2DC_CARD_EMPTY  0
#define PSP2DC_CARD_LOADED 1

/* CRC-32 as zlib's (reflected polynomial 0xEDB88320), four bits a step: what
 * both ends compute over a whole card to tell whether two copies are the
 * same. Start with 0; feed the bytes in order, in as many calls as wanted. */
static inline uint32_t psp2dc_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static const uint32_t t[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

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
    /* the Maple bus, since the adapter started (added 17 November 2026; an
     * older adapter's status ends above, and the PSP takes either) */
    uint16_t maple_bad;       /* packets from the Dreamcast that failed their check */
    uint16_t maple_unanswered;/* packets the adapter sent no reply to */
    uint16_t maple_skipped;   /* replies not sent at once: the bus still busy with the last one */
    uint8_t  vmu_drops;       /* times the adapter took the card away from the Dreamcast */
    uint8_t  last_cmd;        /* the last unanswered packet: its command, */
    uint8_t  last_dest;       /* its address (0x20 the pad, 0x01 the card, 0x02 the rumble) */
    uint8_t  last_words;      /* and its length in words */
    uint32_t last_func;       /* its first word (a function code), or 0 */
    uint16_t maple_slow;      /* replies begun 1 ms or more after their packet */
    uint16_t maple_max_us;    /* the longest a packet waited for its reply since the last status (us) */
} __attribute__((packed)) Psp2dcStatus;
#define PSP2DC_STATUS_OLD_LEN 18  /* an adapter's status before the Maple counts */
#define PSP2DC_STATUS_CARD 0x01   /* the adapter holds a card and shows it to the Dreamcast */

/* LCD payload: the screen as the Dreamcast sent it. 48x32 dots, 1 bit a dot
 * (1 = dark), 6 bytes a row, the most significant bit the leftmost dot. It is
 * upside down for someone holding the controller, where the VMU sits reversed:
 * the PSP turns it half round to show it. */
#define PSP2DC_LCD_BYTES 192
#define PSP2DC_LCD_WIDTH  48
#define PSP2DC_LCD_HEIGHT 32

#endif /* PSP2DC_PROTOCOL_H */
