#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
ifdef CONFIG_PS2
O_OBJS	 := extable.o init.o fault.o r5900.o loadmmu.o
else
O_OBJS	 := extable.o init.o fault.o r4xx0.o r2300.o r6000.o tfp.o \
            andes.o r5900.o loadmmu.o
endif

ifdef CONFIG_PS2
O_OBJS   += ioremap.o
endif

ifdef CONFIG_SGI
O_OBJS   += umap.o
endif

ifdef CONFIG_BAGET_MIPS
O_OBJS   += umap.o
endif

include $(TOPDIR)/Rules.make
