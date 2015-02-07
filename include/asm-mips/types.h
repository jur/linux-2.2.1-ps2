/*
 * include/asm-mips/types.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 */
#ifndef __ASM_MIPS_TYPES_H
#define __ASM_MIPS_TYPES_H
#include <linux/autoconf.h>

typedef unsigned long umode_t;

/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#if ( _MIPS_SZLONG == 64 )

typedef __signed__ long __s64;
typedef unsigned long __u64;

#else

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif
  
#endif

#ifdef CONFIG_CPU_R5900
typedef __signed__ int __s128 __attribute__((mode(TI)));
typedef unsigned int __u128 __attribute__((mode(TI)));
#endif

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

typedef __signed char s8;
typedef unsigned char u8;

typedef __signed short s16;
typedef unsigned short u16;

typedef __signed int s32;
typedef unsigned int u32;

#if ( _MIPS_SZLONG == 64 )

typedef __signed__ long s64;
typedef unsigned long u64;

#else
  
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long s64;
typedef unsigned long long u64;
#endif
  
#endif
#define BITS_PER_LONG _MIPS_SZLONG

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#ifdef CONFIG_CPU_R5900
typedef __signed__ int s128 __attribute__((mode(TI)));
typedef unsigned int u128 __attribute__((mode(TI)));
#endif
#endif

#endif /* __KERNEL__ */

#endif /* __ASM_MIPS_TYPES_H */
