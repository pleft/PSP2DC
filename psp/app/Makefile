# psp2dc_app.prx: the user-mode application over PSPExp's USB driver.
# PSPEXP_USB is PSPExp's psp/transport-usb folder (header + libpspexpusb.a).
PSPEXP_USB ?= ../../../PSPExp/psp/transport-usb

TARGET = psp2dc_app
OBJS = main.o tools.o ui.o vmu_sa.o

INCDIR = $(PSPEXP_USB)
CFLAGS = -O2 -G0 -Wall
ASFLAGS = $(CFLAGS)
LIBDIR = $(PSPEXP_USB)
LDFLAGS =
LIBS = -lpspexpusb -lpspusb -lpspdisplay -lpsprtc -lpsppower

BUILD_PRX = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
