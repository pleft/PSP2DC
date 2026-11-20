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

#define PSP2DC_PROTOCOL_VERSION 7

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
#define PSP2DC_MSG_KEYBOARD   0x07  /* the keyboard: its mode and the keys held (Psp2dcKeyboard) */
#define PSP2DC_MSG_COMP_EVENT 0x08  /* the companion: bytes for the Dreamcast program (whole records) */
/* adapter -> PSP */
#define PSP2DC_MSG_STATUS     0x10   /* about once a second */
#define PSP2DC_MSG_CARD_STATE 0x12   /* the answer to HELLO and CARD_EJECT: the card the adapter holds */
#define PSP2DC_MSG_CARD_SAVE  0x13   /* a quarter block of the card to keep (a CARD_PART payload) */
#define PSP2DC_MSG_LCD    0x11   /* the VMU's screen, each time the Dreamcast draws one */
#define PSP2DC_MSG_GAME_ID 0x14  /* a game's ID, from openMenu as it starts a disc (Psp2dcGameId) */
#define PSP2DC_MSG_COMP_DATA 0x15 /* the companion: the next bytes of the Dreamcast program's stream */
#define PSP2DC_MSG_COMP_RESET 0x16 /* the companion: a session begins; the stream starts afresh */

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

/* KEYBOARD payload (protocol 6): sent when it changes, and every half
 * second. The adapter shows the Dreamcast a keyboard as Sega's "Maple Bus
 * 1.0 Function Type Specifications FT6: Keyboard" (rev. 0.90) has it, a US
 * 104-key one with its own LEDs (Caps Lock kept by the PSP).
 *   mode OFF:  the pad and the VMU, as before.
 *   ONLY:      a keyboard alone, as Sega's: no pad, no VMU.
 * (Mode 1 was one device with both functions, pad and keyboard, the VMU in
 * it: Typing of the Dead never read its keys (18 November 2026). Dropped;
 * an adapter takes it as OFF.)
 * A change of mode is the device unplugged for a second, then the new one
 * plugged in: the Dreamcast reads what it is again.
 * cond is the 8 bytes the keyboard answers the Dreamcast with (FT6 4.2):
 * the modifier keys (bit 0 left Ctrl, 1 left Shift, 2 left Alt, 3 left GUI,
 * 4 right Ctrl, 5 right Shift, 6 right Alt, 7 S2), the LEDs (bit 0 Num
 * Lock, 1 Caps Lock, 2 Scroll Lock), then up to six keys held, left-aligned,
 * 0 after the last: the key codes are USB's (FT6 4.3: a 04h ... z 1Dh). An
 * adapter that hears from no PSP for a moment lets the keys go. */
typedef struct {
    uint8_t mode;         /* PSP2DC_KBD_* */
    uint8_t cond[8];
    uint8_t reserved[3];
} __attribute__((packed)) Psp2dcKeyboard;
#define PSP2DC_KBD_OFF  0
#define PSP2DC_KBD_ONLY 2

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
    /* the keyboard (protocol 6) */
    uint8_t  kbd_mode;        /* PSP2DC_KBD_*, as the Dreamcast is shown it */
    uint8_t  kbd_leds;        /* the LEDs the Dreamcast last set (FT6 Set Condition) */
    uint16_t kbd_reads;       /* times the Dreamcast read the keyboard (wraps) */
    uint16_t comp_cmds;       /* the companion: commands from the Dreamcast (wraps) */
    uint16_t comp_agains;     /* ... of them answered "again", no room yet (wraps) */
    uint32_t comp_bytes;      /* ... and the bytes they brought for the PSP */
} __attribute__((packed)) Psp2dcStatus;
#define PSP2DC_STATUS_OLD_LEN 18  /* an adapter's status before the Maple counts */
#define PSP2DC_STATUS_V5_LEN  36  /* ... and before the keyboard */
#define PSP2DC_STATUS_V6_LEN  40  /* ... and before the companion */
#define PSP2DC_STATUS_CARD 0x01   /* the adapter holds a card and shows it to the Dreamcast */

/* LCD payload: the screen as the Dreamcast sent it. 48x32 dots, 1 bit a dot
 * (1 = dark), 6 bytes a row, the most significant bit the leftmost dot. It is
 * upside down for someone holding the controller, where the VMU sits reversed:
 * the PSP turns it half round to show it. */
#define PSP2DC_LCD_BYTES 192
#define PSP2DC_LCD_WIDTH  48
#define PSP2DC_LCD_HEIGHT 32

/*
 * The companion (docs/psp2dc-companion.md, Appendix B): a Dreamcast program
 * built for it (KallistiOS, dreamcast/libpsp2dc) talks to the PSP through the
 * adapter. Protocol 7; older PSPs and adapters ignore the messages.
 *
 * Dreamcast <-> adapter: Maple command PSP2DC_COMP_CMD to the memory card
 * unit, word 0 the memory card's function code (as openMenu's Game ID is
 * sent), then Psp2dcCompRequest and its data, padded to a word. The adapter
 * answers at once from its own buffers: a data transfer (8) with the function
 * word, Psp2dcCompReply and the bytes the PSP has sent since the last reply
 * (so every command is also a poll); or "again" (0xFC) when a SEND's data has
 * no room yet, which KallistiOS resends at the next vblank.
 *
 * Adapter <-> PSP: COMP_DATA and COMP_EVENT carry the two byte streams, as
 * they are. A stream is a run of records (Psp2dcCompRecord, then its data).
 * The Dreamcast sends whole records in a command; the PSP sends each record
 * whole in one COMP_EVENT, so a record from the PSP is at most
 * PSP2DC_COMP_EVENT_MAX bytes with its header. Either side starts over when
 * the session changes (the PSP's link opened again, or bytes were lost on
 * the way to it): what was on its way is dropped. The Dreamcast program sees
 * the session in every reply; the PSP is sent COMP_RESET before a session's
 * first bytes.
 */
#define PSP2DC_COMP_CMD        0x50
#define PSP2DC_COMP_OP_HELLO   0     /* is a companion there? the reply's data: Psp2dcCompHello */
#define PSP2DC_COMP_OP_SEND    1     /* data: records for the PSP */
#define PSP2DC_COMP_OP_POLL    2     /* nothing to send */
#define PSP2DC_COMP_OK         0
#define PSP2DC_COMP_NO_PSP     1     /* no PSP on the adapter: a SEND's data was dropped */
#define PSP2DC_COMP_BAD        2     /* the request made no sense */
#define PSP2DC_COMP_DATA_MAX   1000  /* data bytes in a request or a reply */
#define PSP2DC_COMP_EVENT_MAX  56    /* a record from the PSP, header and all */
typedef struct {
    uint8_t op;           /* PSP2DC_COMP_OP_* */
    uint8_t seq;          /* echoed in the reply. A SEND with the seq of the last one taken is not
                           * taken twice (its reply was lost, it is sent again); 1 to 255, moving
                           * on with each new SEND only (the others carry 0), HELLO starts the
                           * count over */
    uint16_t len;         /* data bytes that follow */
} __attribute__((packed)) Psp2dcCompRequest;
typedef struct {
    uint8_t status;       /* PSP2DC_COMP_OK... */
    uint8_t session;      /* changes each time the PSP's link opens */
    uint8_t seq;
    uint8_t reserved;
    uint16_t room;        /* bytes free in the adapter's buffer towards the PSP */
    uint16_t len;         /* data bytes that follow: from the PSP (HELLO: Psp2dcCompHello) */
} __attribute__((packed)) Psp2dcCompReply;
typedef struct {
    char magic[6];        /* "PSP2DC" */
    uint8_t version;      /* PSP2DC_PROTOCOL_VERSION */
    uint8_t reserved;
} __attribute__((packed)) Psp2dcCompHello;

/* a record of either stream */
typedef struct {
    uint8_t type;         /* PSP2DC_REC_* */
    uint16_t len;         /* data bytes that follow */
} __attribute__((packed)) Psp2dcCompRecord;
/* Dreamcast -> PSP */
#define PSP2DC_REC_CLEAR   0x01   /* the page emptied */
#define PSP2DC_REC_PRINT   0x02   /* a row (1 byte), then its text */
#define PSP2DC_REC_TEST    0x7E   /* a running number (4 bytes, from 0 after a CLEAR), then bytes: byte i is
                                   * (number + i) & 0xFF; the PSP checks and counts them */
#define PSP2DC_REC_ECHO    0x7F   /* any bytes (at most PSP2DC_COMP_EVENT_MAX - 3): sent back as they are */
/* PSP -> Dreamcast */
#define PSP2DC_REC_BUTTONS 0x01   /* the PSP's buttons (4 bytes, PSP_CTRL_* bits), when they change */
#define PSP2DC_REC_CAPS    0x02   /* Psp2dcCompCaps, in answer to a QUERY */
/*      PSP2DC_REC_ECHO    0x7F      an ECHO's bytes */

/*
 * The screen (docs/psp2dc-companion.md, Appendix C): what a program shows on
 * the PSP beyond rows of text. The adapter only carries these records, so
 * they need no protocol version of their own: a PSP that knows them answers
 * a QUERY, an older one ignores them all.
 *
 * The PSP keeps and draws two kinds of thing, on its whole screen. Widgets
 * (a label, a number, a bar, a light, a list) are made once and then sent
 * only what changes; the PSP gives them its theme's look. Regions are
 * rectangles of pixels the program draws into (filled rectangles, lines,
 * text); what is drawn shows at the next PRESENT. Regions are under the
 * widgets, and widgets are drawn in the order of their ids. A new session
 * starts with neither.
 *
 * Coordinates are pixels from the screen's top left (a region's drawing:
 * from the region's), signed 16 bits; colours are RGB565; text is ASCII
 * (0x20 to 0x7E), a list's lines parted by a line feed.
 */
#define PSP2DC_REC_NOP      0x00  /* nothing: the program is still there (once a second when nothing else goes) */
#define PSP2DC_REC_QUERY    0x03  /* asks for the PSP's Psp2dcCompCaps */
#define PSP2DC_REC_RESET    0x04  /* every widget and region removed */
#define PSP2DC_REC_WIDGET   0x10  /* Psp2dcCompWidget, then its text: a widget made, or made again */
#define PSP2DC_REC_VALUE    0x11  /* id (1), value (4, signed) */
#define PSP2DC_REC_TEXT     0x12  /* id (1), then the text */
#define PSP2DC_REC_REMOVE   0x13  /* id (1) */
#define PSP2DC_REC_NOTIFY   0x14  /* text, shown over the screen for a moment */
#define PSP2DC_REC_REGION   0x20  /* Psp2dcCompRect: a region made, black; every other region is emptied
                                   * too. w or h 0: removed */
#define PSP2DC_REC_FILL     0x21  /* Psp2dcCompRect, colour (2) */
#define PSP2DC_REC_LINE     0x22  /* Psp2dcCompRect holding x0, y0, x1, y1, then colour (2) */
#define PSP2DC_REC_DRAWTEXT 0x23  /* id (1), x (2), y (2), colour (2), size (1: 1 to 4 times 8 pixels), text */
#define PSP2DC_REC_CLIP     0x24  /* Psp2dcCompRect: the region's drawing is kept inside it from here on */
#define PSP2DC_REC_PRESENT  0x25  /* id (1): what was drawn since the last one shows */
/* Pictures (Appendix D). A region has a palette of 256 colours for the formats that index
 * one; at first entry i is the colour whose red, green and blue are i's top 3, next 3 and
 * low 2 bits. A sprite is a picture the PSP keeps, sent once (in pieces, a few rows a
 * record) and then drawn wherever by its number. */
#define PSP2DC_REC_PALETTE    0x26  /* id (1), first (1), then colours (2 each): the region's palette from
                                     * entry `first` on */
#define PSP2DC_REC_BLIT       0x27  /* Psp2dcCompRect, format (1), then h rows of w pixels */
#define PSP2DC_REC_SPRITE     0x28  /* Psp2dcCompSprite, then rows of its pixels from row y on: a sprite made
                                     * (y 0) or gone on with */
#define PSP2DC_REC_DRAWSPRITE 0x29  /* region (1), sprite (1), x (2), y (2), keyed (1), key (2): the sprite
                                     * drawn, in the region's palette; keyed 1: pixels equal to key (an
                                     * index, or with RGB565 a colour) are left out */
#define PSP2DC_FMT_1    0     /* 1 bit a pixel, the top bit first: palette entries 0 and 1 */
#define PSP2DC_FMT_4    1     /* 4 bits a pixel, the high half first */
#define PSP2DC_FMT_8    2     /* a byte a pixel */
#define PSP2DC_FMT_565  3     /* RGB565, the low byte first: no palette */
#define PSP2DC_FMT_ROW(format, w) /* a row's bytes (each row starts on a byte) */ \
    ((format) == PSP2DC_FMT_1 ? ((w) + 7) / 8 : (format) == PSP2DC_FMT_4 ? ((w) + 1) / 2 : (format) == PSP2DC_FMT_8 ? (w) : 2 * (w))
#define PSP2DC_SPRITES       64             /* ids 0 to 63 */
#define PSP2DC_SPRITE_BYTES  (512 * 1024)   /* all sprites together: a byte a pixel, two with RGB565 */
typedef struct {
    uint8_t id;
    uint8_t format;       /* PSP2DC_FMT_* */
    uint16_t w, h;
    uint16_t y;           /* the first row that follows */
} __attribute__((packed)) Psp2dcCompSprite;

#define PSP2DC_WIDGETS         64    /* ids 0 to 63 */
#define PSP2DC_REGIONS         4     /* ids 0 to 3 */
#define PSP2DC_WIDGET_TEXT_MAX 200
#define PSP2DC_SCREEN_W        480
#define PSP2DC_SCREEN_H        272
#define PSP2DC_REGION_PIXELS   (480 * 272)   /* all regions together, at most */
#define PSP2DC_RGB565(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))

#define PSP2DC_WIDGET_LABEL  0   /* its text */
#define PSP2DC_WIDGET_NUMBER 1   /* its text at the left, its value at the right */
#define PSP2DC_WIDGET_BAR    2   /* its value of its max as a bar; taller than 19 pixels, the text and the
                                  * two numbers above the bar */
#define PSP2DC_WIDGET_LIGHT  3   /* a dot, lit when the value is not 0, and its text */
#define PSP2DC_WIDGET_LIST   4   /* its text's lines, the one numbered by the value marked (none: -1) */
#define PSP2DC_WF_COLOUR 0x01    /* the colour field is used; without, the PSP's theme chooses */
#define PSP2DC_WF_BIG    0x02    /* a label's text or a number's value twice the size */
#define PSP2DC_WF_CENTRE 0x04    /* a label's text in the middle of its width */
#define PSP2DC_WF_RIGHT  0x08    /* ... or at its right */
#define PSP2DC_WF_PANEL  0x10    /* a panel behind the widget */
typedef struct {
    uint8_t id;
    int16_t x, y;
    uint16_t w, h;
} __attribute__((packed)) Psp2dcCompRect;
typedef struct {
    uint8_t id;
    uint8_t type;         /* PSP2DC_WIDGET_* */
    uint8_t flags;        /* PSP2DC_WF_* */
    uint8_t reserved;
    int16_t x, y;
    uint16_t w, h;
    uint16_t colour;
    int32_t value;
    int32_t max;          /* a bar's full value */
} __attribute__((packed)) Psp2dcCompWidget;
typedef struct {
    uint8_t level;        /* 2: the screen; 3: pictures too (and the last two fields) */
    uint8_t widgets;      /* PSP2DC_WIDGETS */
    uint8_t regions;      /* PSP2DC_REGIONS */
    uint8_t text_max;     /* PSP2DC_WIDGET_TEXT_MAX */
    uint16_t width, height;   /* the screen */
    uint32_t region_pixels;
    uint32_t widget_types;    /* bit n: PSP2DC_WIDGET_ n is known */
    uint32_t formats;         /* bit n: PSP2DC_FMT_ n is known */
    uint8_t sprites;          /* PSP2DC_SPRITES */
    uint8_t reserved;
    uint32_t sprite_bytes;    /* PSP2DC_SPRITE_BYTES */
} __attribute__((packed)) Psp2dcCompCaps;

#endif /* PSP2DC_PROTOCOL_H */
