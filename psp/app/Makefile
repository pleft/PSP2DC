# psp2dc_app.prx: the user-mode application over PSPExp's USB driver.
# PSPEXP_USB is PSPExp's psp/transport-usb folder (header + libpspexpusb.a).
PSPEXP_USB ?= ../../../PSPExp/psp/transport-usb

# EVMU is libevmu's source and PSP build (scripts/build-evmu.sh)
EVMU ?= ../../third_party
EVMU_SRC = $(EVMU)/libevmu/lib
EVMU_OUT = $(EVMU)/evmu-psp/lib

TARGET = psp2dc_app
OBJS = main.o tools.o ui.o vmu_sa.o vmu_game.o cards.o

INCDIR = $(PSPEXP_USB) $(EVMU_SRC)/api $(EVMU_SRC)/lib/libgimbal/lib/api $(EVMU_OUT)/lib/libgimbal/lib/api
# (libevmu's headers want the configuration its build used)
GBL_DEFS = -DGBL_CONFIG_ASSERT_ERROR_ENABLED=0 -DGBL_CONFIG_ASSERT_PARTIAL_ENABLED=0 \
           -DGBL_CONFIG_ASSERT_UNKNOWN_ENABLED=0 -DGBL_CONFIG_ERRNO_CHECKS=0 \
           -DGBL_CONFIG_LOG_ERROR_ENABLED=0 -DGBL_CONFIG_LOG_ERROR_LEVEL=GBL_LOG_LEVEL_ERROR \
           -DGBL_CONFIG_LOG_PARTIAL_ENABLED=0 -DGBL_CONFIG_LOG_PARTIAL_LEVEL=GBL_LOG_LEVEL_WARNING \
           -DGBL_CONFIG_LOG_UNKNOWN_ENABLED=0 -DGBL_CONFIG_LOG_UNKNOWN_LEVEL=GBL_LOG_LEVEL_ERROR \
           -DGBL_CONFIG_PREFETCH_ENABLED=1 -DGBL_TLS_EMULATED=0 -DGBL_BIG_ENDIAN=0
CFLAGS = -O2 -G0 -Wall -std=gnu17 $(GBL_DEFS)
ASFLAGS = $(CFLAGS)
LIBDIR = $(PSPEXP_USB) $(EVMU_OUT) $(EVMU_OUT)/lib/libgimbal/lib $(EVMU_OUT)/lib/libgimbal/lib/lib/tinycthread \
         $(EVMU_OUT)/lib/libgimbal/lib/lib/tiny-regex-c $(EVMU_OUT)/lib/libgimbal/lib/lib/lz4/build/cmake
LDFLAGS =
LIBS = -lpspexpusb -lpspusb -lpspdisplay -lpsprtc -lpsppower -lpspaudio \
       -llibLibElysianVMU -llibGimbal -ltinycthread -lTinyRegexC -llz4 -llibGimbal -lpthread -lstdc++ -lm

BUILD_PRX = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
