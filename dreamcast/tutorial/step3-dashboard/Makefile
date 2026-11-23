# PSP2DC tutorial, step3-dashboard (KallistiOS). Built by scripts/build-dreamcast.sh; on its own:
#   . /opt/toolchains/dc/kos/environ.sh && make
TARGET = psp2dc_step3_dashboard.elf
OBJS = main.o ../../libpsp2dc/psp2dc.o
KOS_CFLAGS += -I../../libpsp2dc -I../../../protocol

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
