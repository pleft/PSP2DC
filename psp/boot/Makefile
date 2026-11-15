# psp2dc_boot.prx packed as EBOOT.PBP. Kernel-style build (no crt0): the
# module supplies module_start itself, like PSPExp's and PSPLink's bootstrap.
TARGET = psp2dc_boot
OBJS = main.o

INCDIR =
CFLAGS = -O2 -mno-gpopt -Wall
ASFLAGS = $(CFLAGS)
LIBDIR =
LDFLAGS =
LIBS =

BUILD_PRX = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

all: EBOOT.PBP

# ICON0.PNG (144x80, the XMB tile) and PIC1.PNG (480x272, the XMB background
# while PSP2DC is highlighted) come from scripts/dev/make-eboot-art.py.
EBOOT.PBP: $(TARGET).prx ICON0.PNG PIC1.PNG
	mksfoex 'PSP2DC' PARAM.SFO
	pack-pbp EBOOT.PBP PARAM.SFO ICON0.PNG NULL NULL PIC1.PNG NULL $(TARGET).prx NULL

EXTRA_CLEAN = clean_pbp
clean_pbp:
	rm -f PARAM.SFO EBOOT.PBP
