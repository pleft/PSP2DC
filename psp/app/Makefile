# psp2dc_app.prx: the user-mode application over PSPExp's USB driver.
# PSPEXP_USB is the USB driver's folder (its header + libpspexpusb.a), psp/usb.
PSPEXP_USB ?= ../usb

TARGET = psp2dc_app
OBJS = main.o tools.o ui.o keyboard.o companion.o comp_screen.o comp_files.o comp_fio_psp.o vmu_sa.o vmu_game.o vmu_core.o cards.o card_sync.o cardfs.o

INCDIR = $(PSPEXP_USB)
# -MMD: each object's headers are written beside it (.d) and read back below, so a
# changed header builds again all that use it (a stale comp_screen.o, built before
# companion.h grew, read the palette as a region and took the PSP down)
CFLAGS = -O2 -G0 -Wall -std=gnu17 -MMD -MP
ASFLAGS = $(CFLAGS)
LIBDIR = $(PSPEXP_USB)
LDFLAGS =
LIBS = -lpspexpusb -lpspusb -lpspdisplay -lpsprtc -lpsppower -lpspaudio

BUILD_PRX = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

-include $(OBJS:.o=.d)
