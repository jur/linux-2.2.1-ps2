/* $Id: delay.h,v 1.2 1999/01/04 16:09:20 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf Electronics
 * Copyright (C) 1995 - 1998 by Ralf Baechle
 */
#ifndef __ASM_MIPS_DELAY_H
#define __ASM_MIPS_DELAY_H

#include<linux/autoconf.h>

extern __inline__ void __delay(int loops)
{
	__asm__ __volatile__ (
		".set\tnoreorder\n"
#ifdef CONFIG_CPU_R5900	/* inhibit short loop */
		"1:\n\t"
		"beqz\t%0,2f\n\t"
		"subu\t%0,1\n\t"
		""
		"bnez\t%0,1b\n\t"
		"subu\t%0,1\n\t"
		"2:\n\t"
#else
		"1:\tbnez\t%0,1b\n\t"
		"subu\t%0,1\n\t"
#endif
		".set\treorder"
		:"=r" (loops)
		:"0" (loops));
}

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
extern __inline__ void __udelay(unsigned long usecs, unsigned long lps)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */
	__asm__("multu\t%0,%2\n\t"
		"mfhi\t%0"
		:"=r" (usecs)
		:"0" (usecs),"r" (lps));
	__delay(usecs);
}

#ifdef __SMP__
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_sec
#endif

#define udelay(usecs) __udelay((usecs),__udelay_val)

#endif /* __ASM_MIPS_DELAY_H */
