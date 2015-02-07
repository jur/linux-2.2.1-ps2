# $Id: Makefile,v 1.12.6.1 2001/08/30 08:22:37 takemura Exp $
#
# Makefile for the Baget specific kernel interface routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

O_TARGET := ps2.o
O_OBJS   := time.o irq.o setup.o ps2_irq.o reset.o rtc.o powerbutton.o
OX_OBJS	 := ps2_ksyms.o
DEBUGLOG_XOBJS := debuglog.o

ifeq ($(CONFIG_BLK_DEV_IDE),y)
  O_OBJS += ide.o
endif

ifeq ($(CONFIG_T10000),y)
  O_OBJS += aif.o
endif

ifeq ($(CONFIG_PS2_DEBUGLOG),y)
  L_OBJS += $(DEBUGLOG_XOBJS)
else
  ifeq ($(CONFIG_PS2_DEBUGLOG),m)
    M_OBJS += ps2debuglog.o
    MIX_OBJS += $(DEBUGLOG_XOBJS)
  endif
endif

include $(TOPDIR)/Rules.make

ps2debuglog.o: $(DEBUGLOG_XOBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(DEBUGLOG_XOBJS)
