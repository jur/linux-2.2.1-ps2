/*
 * Inline functions to do unaligned accesses.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 */
#ifndef __ASM_MIPS_UNALIGNED_H
#define __ASM_MIPS_UNALIGNED_H

#include <linux/autoconf.h>
#include <asm/sgidefs.h>
#include <asm/string.h>

/*
 * Load quad unaligned.
 */
extern __inline__ unsigned long long ldq_u(const unsigned long long * __addr)
{
#if ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) 

	union {
		struct {
		  unsigned long l_loaddr, l_hiaddr;
		} si;
		unsigned long long di;
	} u;
#ifdef	CONFIG_CONTEXT_R5900
	__asm__(".set	push\n"
		".set	mips3\n"
		"uld	$8, (%2)\n"
		/* 63-32th bits must be same as 31th bit */
		"dsra	%1, $8, 32\n"   // si.lo = $8 >> 32
		"sll	%0, $8, 0\n"    // si.hi = signed_32bit($8)
		".set	pop"
		:"=&r" (u.si.l_loaddr), "=&r"(u.si.l_hiaddr)
		:"r" (__addr)
		: "$8" );

#else /* CONFIG_CONTEXT_R5900 */
	__asm__("ulw\t%0,(%2)\n"
		"ulw\t%1,4(%2)"
		:"=&r" (u.si.l_loaddr), "=&r" (u.si.l_hiaddr)
		:"r" (__addr));
#endif /* CONFIG_CONTEXT_R5900 */

	return u.di;

#else /* ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) */
	unsigned long long __res;
	__asm__("uld\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));
	return __res;
#endif /* ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) */
}

/*
 * Load long unaligned.
 */
extern __inline__ unsigned long ldl_u(const unsigned int * __addr)
{
	unsigned long __res;

	__asm__("ulw\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Load word unaligned.
 */
extern __inline__ unsigned long ldw_u(const unsigned short * __addr)
{
	unsigned long __res;

	__asm__("ulh\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Store quad ununaligned.
 */
extern __inline__ void stq_u(unsigned long long __val, unsigned long long * __addr)
{
#if ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) 
	union {
		struct {
		  unsigned long l_loaddr, l_hiaddr;
		} si;
		unsigned long long di;
	} u;

	u.di = __val;
#ifdef	CONFIG_CONTEXT_R5900
	__asm__(".set	push\n"
		".set	mips3\n"
		"pextlw	$8, %1, %0\n"
		"usd	$8, (%2)\n"
		".set	pop"
		: /* No result */
		:"r" (u.si.l_loaddr), "r"(u.si.l_hiaddr), "r" (__addr)
		: "$8" );

#else /* CONFIG_CONTEXT_R5900 */
	__asm__ __volatile(
		"usw\t%0,(%2)\n"
		"usw\t%1,4(%2)"
		: /* No result */
		:"r" (u.si.l_loaddr), "r" (u.si.l_hiaddr),
		 "r" (__addr));
#endif /* CONFIG_CONTEXT_R5900 */

#else /* ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) */
	__asm__ __volatile__(
		"usd\t%0,(%1)"
		: /* No result */
		:"r" (__val),
		 "r" (__addr));
#endif /* ( (_MIPS_ISA == _MIPS_ISA_MIPS1)||(_MIPS_ISA == _MIPS_ISA_MIPS2)) */
}

/*
 * Store long ununaligned.
 */
extern __inline__ void stl_u(unsigned long __val, unsigned int * __addr)
{
	__asm__ __volatile__(
		"usw\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

/*
 * Store word ununaligned.
 */
extern __inline__ void stw_u(unsigned long __val, unsigned short * __addr)
{
	__asm__ __volatile__(
		"ush\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

extern inline unsigned long long __get_unaligned(const void *ptr, size_t size)
{
	unsigned long long val;
	switch (size) {
	      case 1:
		val = *(const unsigned char *)ptr;
		break;
	      case 2:
		val = ldw_u((const unsigned short *)ptr);
		break;
	      case 4:
		val = ldl_u((const unsigned int *)ptr);
		break;
	      case 8:
		val = ldq_u((const unsigned long long *)ptr);
		break;
	}
	return val;
}

extern inline void __put_unaligned(unsigned long long val, void *ptr, size_t size)
{
	switch (size) {
	      case 1:
		*(unsigned char *)ptr = (val);
	        break;
	      case 2:
		stw_u(val, (unsigned short *)ptr);
		break;
	      case 4:
		stl_u(val, (unsigned int *)ptr);
		break;
	      case 8:
		stq_u(val, (unsigned long long *)ptr);
		break;
	}
}

/* 
 * The main single-value unaligned transfer routines.
 */
#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))__get_unaligned((ptr), sizeof(*(ptr))))
#define put_unaligned(x,ptr) \
	__put_unaligned((unsigned long long)(x), (ptr), sizeof(*(ptr)))

#endif /* __ASM_MIPS_UNALIGNED_H */
