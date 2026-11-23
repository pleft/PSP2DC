TARGET = libpsp2dc.a

OBJS = dreamcast/libpsp2dc/psp2dc.o

KOS_CFLAGS += -Iprotocol -Idreamcast/libpsp2dc

include ${KOS_PORTS}/scripts/lib.mk
