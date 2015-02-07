/*
 * include/asm-mips/sigcontext.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * $Id: sigcontext.h,v 1.5 1997/12/16 05:36:43 ralf Exp $
 */
#ifndef __ASM_MIPS_SIGCONTEXT_H
#define __ASM_MIPS_SIGCONTEXT_H

#include <linux/autoconf.h>
#include <linux/types.h>
/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/mips/tools/offset.c
 */
struct sigcontext {
	unsigned int       sc_regmask;		/* Unused */
	unsigned int       sc_status;
	unsigned long long sc_pc;
#ifdef CONFIG_CONTEXT_R5900
	__u128		   sc_regs[32];
#else
	unsigned long long sc_regs[32];
#endif
	unsigned long long sc_fpregs[32];
	unsigned int       sc_ownedfp;
	unsigned int       sc_fpc_csr;
	unsigned int       sc_fpc_eir;
	unsigned int       sc_ssflags;		/* Unused */
#ifdef CONFIG_CONTEXT_R5900
	__u128			sc_mdhi;
	__u128			sc_mdlo;
	__u32			sc_sa;
	__u32			sc_fp_acc;
#else
	unsigned long long sc_mdhi;
	unsigned long long sc_mdlo;
#endif

	unsigned int       sc_cause;
	unsigned int       sc_badvaddr;		/* Unused */

	unsigned long      sc_sigset[4];	/* kernel's sigset_t */
};

#endif /* __ASM_MIPS_SIGCONTEXT_H */
