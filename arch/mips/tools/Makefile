# Makefile for MIPS kernel build tools.
#
# Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
# Copyright (C) 1997 Ralf Baechle (ralf@gnu.ai.mit.edu)
#
# $Id: Makefile,v 1.2 1997/12/01 17:57:41 ralf Exp $
#
TARGET	:= $(TOPDIR)/include/asm-$(ARCH)/offset.h
TARGET2	:= $(TOPDIR)/include/asm-$(ARCH)/r5900_offset.h

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: $(TARGET) $(TARGET2)

$(TARGET): offset.h
	cmp -s $^ $@ || (cp $^ $(TARGET).new && mv $(TARGET).new $(TARGET))

offset.h: offset.s
	sed -n '/^@@@/s///p' $^ >$@

offset.s: offset.c

$(TARGET2): r5900_offset.h
	cmp -s $^ $@ || (cp $^ $(TARGET2).new && mv $(TARGET2).new $(TARGET2))

r5900_offset.h: r5900_offset.s
	sed -n '/^@@@/s///p' $^ >$@

r5900_offset.s: r5900_offset.c

clean:
	rm -f offset.[hs] $(TARGET).new
	rm -f r5900_offset.[hs] $(TARGET2).new

include $(TOPDIR)/Rules.make
