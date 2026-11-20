# The companion's test program (KallistiOS). Built by scripts/build-dreamcast.sh.
TARGET = psp2dc_test.elf
OBJS = main.o ../libpsp2dc/psp2dc.o
KOS_CFLAGS += -I../libpsp2dc -I../../protocol

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS)

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
