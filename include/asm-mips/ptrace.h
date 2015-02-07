/* $Id: ptrace.h,v 1.4 1999/01/04 16:09:25 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 by Ralf Baechle
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * Machine dependent structs and defines to help the user use
 * the ptrace system call.
 */
#ifndef __ASM_MIPS_PTRACE_H
#define __ASM_MIPS_PTRACE_H

#include <linux/autoconf.h>
#include <linux/types.h>

/* 
   Register specifiers for PTRACE_PEEKUSR and PTRACE_POKEUSER

   Note: Don't confuse following constants as real offset values 
   for pt_regs struct. Following constants are used only 
   for ptrace() interface and fixed for ptrace() interface 
   compatibility, even if pt_regs struct is changed.  
   Real offset values for pt_regs struct are in <asm/offset.h>.

   Please see arc/mips/kernel/ptrace.c for detail.
*/

/* 0 - 31 are integer registers, 32 - 63 are fp registers.  */
#define FPR_BASE	32
#define PC		64
#define CAUSE		65
#define BADVADDR	66
#define MMHI		67
#define MMLO		68
#define FPC_CSR		69
#define FPC_EIR		70
#ifdef CONFIG_CONTEXT_R5900
#define R5900_SA	71
#define R5900_FPACC	72
#endif /* CONFIG_CONTEXT_R5900 */

#ifndef __ASSEMBLY__
/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
	/* Pad bytes for argument save space on the stack. */
	unsigned long pad0[6];

#ifdef CONFIG_CONTEXT_R5900
	/* Saved main processor registers. */
	__u128	regs[32];

	/* Other saved registers. */
	__u128	lo;
	__u128	hi;
	__u32	sa;
#else
	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Other saved registers. */
	unsigned long lo;
	unsigned long hi;
#endif

	/*
	 * saved cp0 registers
	 */
	unsigned long cp0_epc;
	unsigned long cp0_badvaddr;
	unsigned long cp0_status;
	unsigned long cp0_cause;
};

#ifdef CONFIG_CONTEXT_R5900

typedef union  {
	__u128	gp;	// general purpose regs.
	__u32	fp;	// fp regs (use __u32 not float)
	__u32	ctl;	// cop0/sa regs
	__u128	lohi;	// (lo1.lo0)/(hi1.hi0) regs
} r5900_reg_union ;

#endif /* CONFIG_CONTEXT_R5900 */

#else /* !(__ASSEMBLY__) */

#include <asm/offset.h>

#endif /* !(__ASSEMBLY__) */

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* 
 * Access methods to pt_regs->regs.
 */
#ifdef CONFIG_CONTEXT_R5900

#ifdef __BIG_ENDIAN
#error "not supported"
#endif
/* get LS32B of reg. specified in index */
static inline
unsigned long get_gpreg(struct pt_regs * regs, int index)
{
	void *addr = (void *)& regs->regs[index];

	return *(unsigned long *) addr;
}

/* set 0-31th bits of reg. specified in index and 32-63th bits as same as 31th bit, 
  other bits are preserved, just like "LW" insn dose. */
static inline
void set_gpreg(struct pt_regs * regs, int index, unsigned long val)
{
	unsigned long *addr = (unsigned long *)& regs->regs[index];
	addr[0] = val;
	if (val & 0x80000000) {
		addr[1] = 0xffffffff;
	} else {
		addr[1] = 0;
	}
}

#else /* CONFIG_CONTEXT_R5900 */

static inline
unsigned long get_gpreg(struct pt_regs * regs, int index)
{
	return regs->regs[index];
}

static inline
void set_gpreg(struct pt_regs * regs, int index, unsigned long val)
{
	regs->regs[index] = val;
}

#endif /* CONFIG_CONTEXT_R5900 */

/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) ((regs)->cp0_status & 0x10)

#define instruction_pointer(regs) ((regs)->cp0_epc)

extern void (*show_regs)(struct pt_regs *);
extern void (*show_tlbs)(void);

#endif /* !(__ASSEMBLY__) */

#endif

#endif /* __ASM_MIPS_PTRACE_H */
