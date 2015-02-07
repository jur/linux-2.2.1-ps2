# $Id: Makefile,v 1.3 2001/03/28 07:47:23 nakamura Exp $
#
# Makefile for the PS2 SIF BIOS library routines under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

L_TARGET := sbioslib.a

L_OBJS   := init.o printf.o sbcall.o iopheap.o
LX_OBJS  := sifsyms.o

MX_OBJS  :=
MI_OBJS  :=
MIX_OBJS :=

LOCK_OBJS := 
LOCK_XOBJS := siflock.o

ifeq ($(CONFIG_PS2_LOCK),y)
  LX_OBJS += $(LOCK_OBJS) $(LOCK_XOBJS)
else
  ifeq ($(CONFIG_PS2_LOCK),m)
    M_OBJS += ps2lock.o
    MI_OBJS += $(LOCK_OBJS)
    MIX_OBJS +=  $(LOCK_XOBJS)
  endif
endif

include $(TOPDIR)/Rules.make

ps2lock.o: siflock.o
	$(LD) $(LD_RFLAG) -o $@ -r siflock.o
