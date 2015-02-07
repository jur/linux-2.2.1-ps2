/*
 * sys_r5900.h - r5900 spcific syscalls 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#ifndef __ASM_SYS_R5900_H
#define __ASM_SYS_R5900_H

#include <linux/autoconf.h>
#include <asm/perf_counter.h>
#ifdef CONFIG_CONTEXT_R5900
#include <asm/ptrace.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* sys r5900 command */
#define	MIPS_SYS_R5900	 	1024

/* sub command */
/*   -- perf counter */
#define	SYS_R5900_GET_CCR	1	/* GET CCR register */
#define	SYS_R5900_SET_CCR	2	/* SET CCR register */
#define	SYS_R5900_GET_CTR	3	/* GET specified CTR register */
#define	SYS_R5900_SET_CTR	4	/* SET specified CTR register */
#define	SYS_R5900_GET_CTRS	5	/* GET all CTR registers */
#define	SYS_R5900_SET_CTRS	6	/* SET all CTR registers */

/*   -- extended ptrace */
#define SYS_R5900_PTRACE_PEEKU	10	/* PTRACE_PEEKUSER for r5900 regs. */
#define SYS_R5900_PTRACE_POKEU	11	/* PTRACE_POKEUSER for r5900 regs. */

/*
 * CTR0 and CTR1 are 31bit counter in real hardware.
 * But, linux kernel provides  software-extended 64bit counter.
 *
 */

/* spcify counter for GET_CTR/SET_CTR */
#define R5900_CTR0	1
#define R5900_CTR1	2


#ifndef _LANGUAGE_ASSEMBLY

#include <asm/types.h>

struct sys_r5900_ctrs {
	__u64	ctr0;
	__u64	ctr1;
};

#ifdef __KERNEL__

struct  r5900_upper_ctrs {
	__u32	ctr0;
	__u32	ctr1;
};

#endif /* __KERNEL__ */

#ifdef CONFIG_CONTEXT_R5900
struct sys_r5900_ptrace {
	long		addr;
	r5900_reg_union	reg;
};

#endif

#endif /* _LANGUAGE_ASSEMBLY */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __ASM_SYS_R5900_H */
