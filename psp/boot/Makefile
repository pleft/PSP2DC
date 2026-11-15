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

EBOOT.PBP: $(TARGET).prx
	mksfoex 'PSP2DC' PARAM.SFO
	pack-pbp EBOOT.PBP PARAM.SFO NULL NULL NULL NULL NULL $(TARGET).prx NULL

EXTRA_CLEAN = clean_pbp
clean_pbp:
	rm -f PARAM.SFO EBOOT.PBP
