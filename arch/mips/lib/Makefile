# $Id: Makefile,v 1.9 1999/01/04 16:03:50 ralf Exp $
#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

ifdef CONFIG_CONTEXT_R5900
MEMCPY_OBJS=r5900_memcpy.o
else
MEMCPY_OBJS=memcpy.o
endif

GCC_RT_FUNCS = _muldi3 _divdi3 _moddi3 _udivdi3 _umoddi3 _negdi2 \
     _lshrdi3 _ashldi3 _ashrdi3 _ffsdi2 \
    _udiv_w_sdiv _udivmoddi4 _cmpdi2 _ucmpdi2

GCC_RT_OBJS = $(addsuffix .o, $(GCC_RT_FUNCS))

GCC_RT_SRCS = $(addsuffix .c, $(GCC_RT_FUNCS))

L_TARGET = lib.a
L_OBJS	= csum_partial.o csum_partial_copy.o dump_tlb.o floppy-std.o \
	  floppy-no.o ide-std.o ide-no.o kbd-std.o kbd-no.o rtc-std.o \
	  rtc-no.o memset.o $(MEMCPY_OBJS) strlen_user.o strncpy_user.o \
	  watch.o \
	  $(GCC_RT_OBJS)

include $(TOPDIR)/Rules.make

gcc_rt.c: gcc_rt.h

$(GCC_RT_OBJS):	EXTRA_CFLAGS+=-DL$(basename $@)

$(GCC_RT_SRCS):	gcc_rt.c
	rm -f $@
	ln -s gcc_rt.c $@

clean:
	rm -f $(GCC_RT_SRCS)

