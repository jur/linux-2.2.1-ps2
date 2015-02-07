/* $Id: process.c,v 1.11 1999/01/03 17:50:51 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1998 by Ralf Baechle and others.
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 */
#include <linux/autoconf.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/bootinfo.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/elf.h>

struct task_struct *last_task_used_math = NULL;

asmlinkage void ret_from_sys_call(void);
asmlinkage void (*save_fp)(struct task_struct * p);

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	/* New thread looses kernel privileges. */
	regs->cp0_status = (regs->cp0_status & ~(ST0_CU0|ST0_KSU)) | KSU_USER;
	regs->cp0_epc = pc;
	set_gpreg(regs, 29, sp);
	current->tss.current_ds = USER_DS;
}

void exit_thread(void)
{
	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1, ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");	/* sync fpu */
		set_cp0_status(ST0_CU1, 0);
		last_task_used_math = NULL;
	}
}

void flush_thread(void)
{
	/* Mark fpu context to be cleared */
	current->used_math = 0;

	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		struct pt_regs *regs;

		/* Make CURRENT lose fpu */ 
		set_cp0_status(ST0_CU1, ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");	/* sync fpu */
		set_cp0_status(ST0_CU1, 0);
		last_task_used_math = NULL;
		regs = (struct pt_regs *) ((unsigned long) current +
			KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		regs->cp0_status &= ~ST0_CU1;
	}
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
                 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	long childksp;

	childksp = (unsigned long)p + KERNEL_STACK_SIZE - 32;

	if (last_task_used_math == current) {
		/* In this case, CURRENT->TSS.FPU is not up-to-date */
		set_cp0_status(ST0_CU1, ST0_CU1);
		save_fp(p);
		set_cp0_status(ST0_CU1, 0);
	}
	/* set up new TSS. */
	childregs = (struct pt_regs *) childksp - 1;
	*childregs = *regs;
	set_gpreg(childregs, 7, 0); /* Clear error flag */
	if(current->personality == PER_LINUX) {
		set_gpreg(childregs, 2, 0); /* Child gets zero as return value */
		set_gpreg(regs, 2, p->pid);
	} else {
		/* Under IRIX things are a little different. */
		set_gpreg(childregs, 2, 0);
		set_gpreg(childregs, 3, 1);
		set_gpreg(regs, 2, p->pid);
		set_gpreg(regs, 3, 0);
	}
	if (childregs->cp0_status & ST0_CU0) {
		set_gpreg(childregs, 28, (unsigned long)p);
		set_gpreg(childregs, 29, childksp);
		p->tss.current_ds = KERNEL_DS;
	} else {
		set_gpreg(childregs, 29, usp);
		p->tss.current_ds = USER_DS;
	}
	p->tss.reg29 = (unsigned long) childregs;
	p->tss.reg31 = (unsigned long) ret_from_sys_call;

	/*
	 * New tasks loose permission to use the fpu. This accelerates context
	 * switching for most programs since they don't use the fpu.
	 */
	p->tss.cp0_status = read_32bit_cp0_register(CP0_STATUS) &
                            ~(ST0_CU3|ST0_CU2|ST0_CU1|ST0_KSU);
	childregs->cp0_status &= ~(ST0_CU3|ST0_CU2|ST0_CU1);
	p->mm->context = 0;

	return 0;
}

/* Fill in the elf_gregset_t structure for a core dump.. */
void mips_dump_regs(elf_greg_t *r, struct pt_regs *regs)
{
	int i;

	r[EF_REG0] = 0;
	for (i=1; i<32 ; i++) {
		r[EF_REG0+i] = (elf_greg_t) regs->regs[i];
	}
	r[EF_LO] = (elf_greg_t) regs->lo;
	r[EF_HI] = (elf_greg_t) regs->hi;
#ifdef CONFIG_CONTEXT_R5900
	r[EF_SA] = (elf_greg_t) regs->sa;
#endif
	r[EF_CP0_EPC] = (elf_greg_t) regs->cp0_epc;
	r[EF_CP0_BADVADDR] = (elf_greg_t) regs->cp0_badvaddr;
	r[EF_CP0_STATUS] = (elf_greg_t) regs->cp0_status;
	r[EF_CP0_CAUSE] = (elf_greg_t) regs->cp0_cause;
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	/* We actually store the FPU info in the task->tss
	 * area.
	 */
	if(current->used_math) {
		if (current == last_task_used_math) {
			/* In this case, CURRENT->TSS.FPU is not up-to-date */
			set_cp0_status(ST0_CU1, ST0_CU1);
			save_fp(current);
			set_cp0_status(ST0_CU1, 0);
		}

		if ( sizeof(current->tss.fpu) > sizeof(elf_fpregset_t)){
			printk ("dump_fpu: Can't dump due to lack of "
				"size of elf_fpregset_t.\n" );
			return 0;
		}

		memcpy(r, &current->tss.fpu, sizeof(current->tss.fpu));
		return 1;
	}
	return 0; /* Task didn't use the fpu at all. */
}

/* Fill in the user structure for a core dump.. */
void dump_thread(struct pt_regs *regs, struct user *dump)
{
	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = regs->regs[29] & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE - 1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize =
		(current->mm->start_stack - dump->start_stack + PAGE_SIZE - 1) >> PAGE_SHIFT;
	memcpy(&dump->regs[0], regs, sizeof(struct pt_regs));
	memcpy(&dump->regs[EF_SIZE/4], &current->tss.fpu, sizeof(current->tss.fpu));
}

/*
 * Create a kernel thread
 */
pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"move\t$6,$sp\n\t"
		"move\t$4,%5\n\t"
		"li\t$2,%1\n\t"
		"syscall\n\t"
		"beq\t$6,$sp,1f\n\t"
		"subu\t$sp,32\n\t"	/* delay slot */
		"jalr\t%4\n\t"
		"move\t$4,%3\n\t"	/* delay slot */
		"move\t$4,$2\n\t"
		"li\t$2,%2\n\t"
		"syscall\n"
		"1:\taddiu\t$sp,32\n\t"
		"move\t%0,$2\n\t"
		".set\treorder"
		:"=r" (retval)
		:"i" (__NR_clone), "i" (__NR_exit),
		 "r" (arg), "r" (fn),
		 "r" (flags | CLONE_VM)
		 /*
		  * The called subroutine might have destroyed any of the
		  * at, result, argument or temporary registers ...
		  */
		:"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8",
		 "$9","$10","$11","$12","$13","$14","$15","$24","$25");

	return retval;
}
