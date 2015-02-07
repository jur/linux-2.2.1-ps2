/* $Id: fp.h,v 1.1 1998/07/16 19:10:04 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */

#include <linux/autoconf.h>

#ifdef CONFIG_CPU_R5900
#  if !defined(SYNC_AFTER_MTC0_STR)
#    define SYNC_AFTER_MTC0_STR	"\n\tsync.p\n\t"
#  endif
#else
#  undef SYNC_AFTER_MTC0_STR
#  define SYNC_AFTER_MTC0_STR
#endif

/*
 * Activate and deactive the floatingpoint accelerator.
 */
#define enable_cp1()							\
	__asm__ __volatile__(						\
		".set\tnoat\n\t"					\
		"mfc0\t$1,$12\n\t"					\
		"or\t$1,%0\n\t"						\
		"mtc0\t$1,$12\n\t"					\
		SYNC_AFTER_MTC0_STR					\
		".set\tat"						\
		: : "r" (ST0_CU1));

#define disable_cp1()							\
	__asm__ __volatile__(						\
		".set\tnoat\n\t"					\
		"mfc0\t$1,$12\n\t"					\
		"or\t$1,%0\n\t"						\
		"xor\t$1,%0\n\t"					\
		"mtc0\t$1,$12\n\t"					\
		SYNC_AFTER_MTC0_STR					\
		".set\tat"						\
		: : "r" (ST0_CU1));

