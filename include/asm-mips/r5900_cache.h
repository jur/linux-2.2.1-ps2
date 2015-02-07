/*
 * original file:
 *
 * r4kcache.h: Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: r4kcache.h,v 1.5 1998/03/04 05:36:23 ralf Exp $
 *
 * modified for R5900(EE core) by SCEI.
 */
#ifndef _MIPS_R5900CACHE_H
#define _MIPS_R5900CACHE_H

#include <asm/asm.h>
#include <asm/r5900_cacheops.h>

extern inline void flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		"cache %1, (%0)\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_I));
}

extern inline void flush_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		"cache %1, (%0)\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_Inv_D));
}

/*
 * The next two are for badland addresses like signal trampolines.
 */
extern inline void protected_flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		"1:\tcache %1,(%0)\n"
		"2:\tsync.l\n\t"
		"sync.p\n\t"
		".set mips0\n\t"
		".set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		STR(PTR)"\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_I));
}

extern inline void protected_writeback_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"sync.l\n\t"
		"sync.p\n\t"
		"1:\tcache %1,(%0)\n"
		"2:\tsync.l\n\t"
		"sync.p\n\t"
		".set mips0\n\t"
		".set reorder\n\t"
		".section\t__ex_table,\"a\"\n\t"
		STR(PTR)"\t1b,2b\n\t"
		".previous"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_D));
}

#define cache64_unroll32(base,op)				\
	__asm__ __volatile__("					\
		.set noreorder;					\
		.set mips3;					\
		sync.l; sync.p;				\
		cache %1, 0x000(%0); \
		sync.l; sync.p;				\
		cache %1, 0x040(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x080(%0); \
		sync.l; sync.p;				\
		cache %1, 0x0c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x100(%0); \
		sync.l; sync.p;				\
		cache %1, 0x140(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x180(%0); \
		sync.l; sync.p;				\
		cache %1, 0x1c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x200(%0); \
		sync.l; sync.p;				\
		cache %1, 0x240(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x280(%0); \
		sync.l; sync.p;				\
		cache %1, 0x2c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x300(%0); \
		sync.l; sync.p;				\
		cache %1, 0x340(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x380(%0); \
		sync.l; sync.p;				\
		cache %1, 0x3c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x400(%0); \
		sync.l; sync.p;				\
		cache %1, 0x440(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x480(%0); \
		sync.l; sync.p;				\
		cache %1, 0x4c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x500(%0); \
		sync.l; sync.p;				\
		cache %1, 0x540(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x580(%0); \
		sync.l; sync.p;				\
		cache %1, 0x5c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x600(%0); \
		sync.l; sync.p;				\
		cache %1, 0x640(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x680(%0); \
		sync.l; sync.p;				\
		cache %1, 0x6c0(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x700(%0); \
		sync.l; sync.p;				\
		cache %1, 0x740(%0);	\
		sync.l; sync.p;				\
		cache %1, 0x780(%0); \
		sync.l; sync.p;				\
		cache %1, 0x7c0(%0);	\
		sync.l; sync.p;				\
		.set mips0;					\
		.set reorder"					\
		:						\
		: "r" (base),					\
		  "i" (op));

extern inline void blast_dcache64(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size / 2);

	while(start < end) {
		cache64_unroll32(start,Index_Writeback_Inv_D);
		cache64_unroll32(start + 1,Index_Writeback_Inv_D);
		start += 0x800;
	}
}

extern inline void blast_dcache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache64_unroll32(start,Hit_Writeback_Inv_D);
		start += 0x800;
	}
}

extern inline void blast_dcache64_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE / 2);

	while(start < end) {
		cache64_unroll32(start,Index_Writeback_Inv_D);
		cache64_unroll32(start + 1,Index_Writeback_Inv_D);
		start += 0x800;
	}
}

extern inline void blast_icache64(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size / 2);

	while(start < end) {
		cache64_unroll32(start,Index_Invalidate_I);
		cache64_unroll32(start + 1,Index_Invalidate_I);
		start += 0x800;
	}
}

extern inline void blast_icache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		cache64_unroll32(start,Hit_Invalidate_I);
		start += 0x800;
	}
}

extern inline void blast_icache64_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE / 2);

	while(start < end) {
		cache64_unroll32(start,Index_Invalidate_I);
		cache64_unroll32(start + 1,Index_Invalidate_I);
		start += 0x800;
	}
}

#endif /* !(_MIPS_R5900CACHE_H) */
