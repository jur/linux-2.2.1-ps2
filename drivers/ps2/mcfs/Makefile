#
# Makefile for the ps2 memory card file system.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := ps2mcfs.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

MCFS_OBJS := mcfs.o inode.o root.o dir.o file.o null.o dirent.o path.o filedesc.o

ifeq ($(CONFIG_PS2_MCFS),y)
  L_OBJS += $(MCFS_OBJS)
else
  ifeq ($(CONFIG_PS2_MCFS),m)
    M_OBJS += ps2mcfs.o
    MI_OBJS += $(MCFS_OBJS)
  endif
endif

include $(TOPDIR)/Rules.make

ps2mcfs.o: $(MCFS_OBJS)
	$(LD) $(LD_RFLAG) -o $@ -r $(MCFS_OBJS)
