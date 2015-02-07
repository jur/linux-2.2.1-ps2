#
# Makefile for the ps2 device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#


SUB_DIRS     := mcfs
ifeq ($(CONFIG_T10000),y)
  SUB_DIRS += t10000
endif
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := ps2.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

PAD_OBJS := pad.o joystick.o
CDVD_OBJS := cdvd.o cdvdiop.o
SD_OBJS := sd.o sdmixer.o sdcnv.o
MC_OBJS := mcdir.o mcfile.o mcdev.o
MC_XOBJS := mc.o
RTC_OBJS := rtc.o

ifeq ($(CONFIG_PS2_PAD),y)
  L_OBJS += $(PAD_OBJS)
else
  ifeq ($(CONFIG_PS2_PAD),m)
    M_OBJS += ps2pad.o
    MI_OBJS += $(PAD_OBJS)
  endif
endif

ifeq ($(CONFIG_PS2_CDVD),y)
  L_OBJS += $(CDVD_OBJS)
else
  ifeq ($(CONFIG_PS2_CDVD),m)
    M_OBJS += ps2cdvd.o
    MI_OBJS += $(CDVD_OBJS)
  endif
endif

ifeq ($(CONFIG_PS2_SD),y)
  L_OBJS += $(SD_OBJS)
else
  ifeq ($(CONFIG_PS2_SD),m)
    M_OBJS += ps2sd.o
    MI_OBJS += $(SD_OBJS)
  endif
endif

ifeq ($(CONFIG_PS2_MC),y)
  L_OBJS += $(MC_XOBJS) $(MC_OBJS)
else
  ifeq ($(CONFIG_PS2_MC),m)
    M_OBJS += ps2mc.o
    MI_OBJS += $(MC_OBJS)
    MIX_OBJS += $(MC_XOBJS) 
  endif
endif

ifeq ($(CONFIG_PS2_RTC),y)
  L_OBJS += $(RTC_OBJS)
else
  ifeq ($(CONFIG_PS2_RTC),m)
    M_OBJS += ps2rtc.o
    MI_OBJS += $(RTC_OBJS)
  endif
endif

# PS2 ethernet
ifeq ($(CONFIG_PS2_ETHER_SMAP),y)
    L_OBJS += smap.o
    L_OBJS += smaprpc.o
else
  ifeq ($(CONFIG_PS2_ETHER_SMAP),m)
    M_OBJS += smap.o
    M_OBJS += smaprpc.o
  endif
endif

include $(TOPDIR)/Rules.make

ps2pad.o: $(PAD_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(PAD_OBJS)

ps2cdvd.o: $(CDVD_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(CDVD_OBJS)

ps2sd.o: $(SD_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(SD_OBJS)

ps2mc.o: $(MC_XOBJS) $(MC_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(MC_XOBJS) $(MC_OBJS)

ps2rtc.o: $(RTC_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(RTC_OBJS)
