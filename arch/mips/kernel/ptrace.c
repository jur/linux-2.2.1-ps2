/* $Id: ptrace.c,v 1.12 1999/06/13 16:30:32 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/fp.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#ifdef CONFIG_CONTEXT_R5900
#include <asm/sys_r5900.h>
#endif

asmlinkage void (*save_fp)(struct task_struct *);

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct task_struct * tsk,
			      struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page, retval;
	int fault;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir)) {
		fault = handle_mm_fault(tsk, vma, addr, 0);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return 0;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		fault = handle_mm_fault(tsk, vma, addr, 0);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return 0;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		fault = handle_mm_fault(tsk, vma, addr, 0);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return 0;
	}

	page = pte_page(*pgtable);
	/* This is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) >= MAP_NR(high_memory))
		return 0;
	page += addr & ~PAGE_MASK;
	/* We can't use flush_page_to_ram() since we're running in
	 * another context ...
	 */
	flush_cache_all();
	retval = *(unsigned long *) page;
	flush_cache_all();	/* VCED avoidance  */
	return retval;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct task_struct *tsk,
		     struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;
	int fault;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir)) {
		fault = handle_mm_fault(tsk, vma, addr, 1);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		fault = handle_mm_fault(tsk, vma, addr, 1);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		fault = handle_mm_fault(tsk, vma, addr, 1);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		fault = handle_mm_fault(tsk, vma, addr, 1);
		if (fault > 0)
			goto repeat;
		if (fault < 0)
			force_sig(SIGKILL, tsk);
		return;
	}

	/* This is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) < MAP_NR(high_memory))
		flush_cache_all();
	*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
	if (MAP_NR(page) < MAP_NR(high_memory))
		flush_cache_all();
	/*
	 * We're bypassing pagetables, so we have to set the dirty bit
	 * ourselves this should also re-instate whatever read-only mode
	 * there was before
	 */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_page(vma, addr);
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk->mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 1:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 3:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk, vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x000000ff;
				low |= data << 8;
				high &= ~0xff;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= ~0xffff;
				high |= data >> 16;
				break;
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(tsk, vma, addr & ~(sizeof(long)-1),low);
		put_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(tsk, vma, addr, data);
	return 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	unsigned int flags;
	int res;

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	res = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		res = 0;
		goto out;
	}
	res = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);    /* FIXME!!! */
	if (!child)
                goto out;
        res = -EPERM;
        if (pid == 1)           /* you may not mess with init */
                goto out;
	if (request == PTRACE_ATTACH) {
		if (child == current) {
			goto out;
		}
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid) ||
		    (!cap_issubset(child->cap_permitted,
		                  current->cap_permitted)) ||
                    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE)){
			goto out;
		}
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;

		write_lock_irqsave(&tasklist_lock, flags);
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irqrestore(&tasklist_lock, flags);

		send_sig(SIGSTOP, child, 1);
		res = 0;
		goto out;
	}
	res = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;

		down(&child->mm->mmap_sem);
		res = read_long(child, addr, &tmp);
		up(&child->mm->mmap_sem);
		if (res < 0)
			goto out;
		res = put_user(tmp,(unsigned long *) data);
		goto out;
		}

	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp;

		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		tmp = 0;  /* Default return value. */

		switch(addr) {
		case 0 ... 31:
			tmp = get_gpreg(regs, addr);
			break;
		case FPC_CSR:
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				unsigned long long *fregs;

				if (last_task_used_math == child) {
					set_cp0_status(ST0_CU1, ST0_CU1);
					save_fp(child);
					set_cp0_status(ST0_CU1, 0);
					last_task_used_math = NULL;
					regs->cp0_status &= ~ST0_CU1;
				}
				if (addr==FPC_CSR) {
				    tmp = child->tss.fpu.hard.control;
				} else {
				    fregs = (unsigned long long *)
					&child->tss.fpu.hard.fp_regs[0];
				    tmp = (unsigned long)fregs[addr - FPR_BASE];
				}
			} else {
				if (addr==FPC_CSR)
				    tmp = 0;	/* FPU_DEFAULT */
				else
				    tmp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
#ifdef CONFIG_CONTEXT_R5900
			tmp = *(__u32 *) &(regs->hi);
#else
			tmp = regs->hi;
#endif
			break;
		case MMLO:
#ifdef CONFIG_CONTEXT_R5900
			tmp = *(__u32 *) &(regs->lo);
#else
			tmp = regs->lo;
#endif
			break;
		case FPC_EIR:	/* implementation / version register */
			set_cp0_status(ST0_CU1, ST0_CU1);
			__asm__ ("cfc1	%0, $0" : "=r" (tmp));
			set_cp0_status(ST0_CU1, 0);
			break;
		default:
			tmp = 0;
			res = -EIO;
			goto out;
		}
		res = put_user(tmp, (unsigned long *) data);
		goto out;
	    }

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		down(&child->mm->mmap_sem);
		res = write_long(child,addr,data);
		if (request == PTRACE_POKETEXT)
			flush_cache_sigtramp((unsigned long) addr);
		up(&child->mm->mmap_sem);
		goto out;

	case PTRACE_POKEUSR: {
		unsigned long long *fregs;
		struct pt_regs *regs;
		res = 0;

		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		switch (addr) {
		case 0 ... 31:
			set_gpreg(regs, addr, data);
			break;
		case FPC_CSR:
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				if (last_task_used_math == child) {
					set_cp0_status(ST0_CU1, ST0_CU1);
					save_fp(child);
					set_cp0_status(ST0_CU1, 0);
					last_task_used_math = NULL;
					regs->cp0_status &= ~ST0_CU1;
				}
			} else {
				/* FP not yet used  */
				memset(&child->tss.fpu.hard, ~0,
				       sizeof(child->tss.fpu.hard));
				child->tss.fpu.hard.control
						= 0; /* FPU_DEAFAULT */

				/* Mark to preserve CHILD->TSS.FPU */
				child->used_math = 1;
			}
			if (addr== FPC_CSR) {
			    child->tss.fpu.hard.control = data;
			} else {
			    fregs = (unsigned long long *)
				&child->tss.fpu.hard.fp_regs[0];
			    fregs[addr - FPR_BASE] = (unsigned long long) data;
			}
			break;
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
#ifdef CONFIG_CONTEXT_R5900
			*(__u32 *) &(regs->hi) = data;
#else
			regs->hi = data;
#endif
			break;
		case MMLO:
#ifdef CONFIG_CONTEXT_R5900
			*(__u32 *) &(regs->lo) = data;
#else
			regs->lo = data;
#endif
			break;
		default:
			/* The rest are not allowed. */
			res = -EIO;
			break;
		}
		goto out;
	    }

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		if ((unsigned long) data > _NSIG) {
			res = -EIO;
			goto out;
		}
		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;
		child->exit_code = data;
		wake_up_process(child);
		res = data;
		goto out;
		}

	/*
	 * make the child exit.  Best I can do is send it a sigkill. 
	 * perhaps it should be put in the status that it wants to 
	 * exit.
	 */
	case PTRACE_KILL: {
		if (child->state != TASK_ZOMBIE) {
			child->exit_code = SIGKILL;
			wake_up_process(child);
		}
		res = 0;
		goto out;
		}

	case PTRACE_DETACH: { /* detach a process that was attached. */
		if ((unsigned long) data > _NSIG) {
			res = -EIO;
			goto out;
		}
		child->flags &= ~(PF_PTRACED|PF_TRACESYS);
		child->exit_code = data;
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		wake_up_process(child);
		res = 0;
		goto out;
		}

	default:
		res = -EIO;
		goto out;
	}
out:
	unlock_kernel();
	return res;
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

#ifdef CONFIG_CONTEXT_R5900
/* Extended version of ptrace() */
/* This provides functions for PEEK/POKE r5900 registers */
int sys_r5900_ptrace(long request, long pid, 
		struct sys_r5900_ptrace *user_param)
{
	struct task_struct *child;
	int res;
	struct sys_r5900_ptrace param;
	r5900_reg_union *valp = &(param.reg);
	long addr;


	lock_kernel();
	res = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);    /* FIXME!!! */
	if (!child)
                goto out;
        res = -EPERM;
	if (pid == 1) {		/* you may not mess with init */
		goto out;
	}
	res = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	if (copy_from_user(&param, user_param, sizeof(param))) {
		res = -EFAULT;
		goto out;
	}
	addr = param.addr;

	switch (request) {
	/* Read the word at location addr in the USER area.  */
	case SYS_R5900_PTRACE_PEEKU: 
	    {
		struct pt_regs *regs;

		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		valp->gp = 0;  /* Default return value. */

		switch(addr) {
		case 0 ... 31:
			valp->gp = regs->regs[addr];
			break;
		case FPC_CSR:
		case R5900_FPACC:
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				unsigned long long *fregs;

				if (last_task_used_math == child) {
					set_cp0_status(ST0_CU1, ST0_CU1);
					save_fp(child);
					set_cp0_status(ST0_CU1, 0);
					last_task_used_math = NULL;
					regs->cp0_status &= ~ST0_CU1;
				}

				if (addr == FPC_CSR) {
				    valp->ctl = child->tss.fpu.hard.control;
				} else if (addr == R5900_FPACC) {
				    valp->ctl = child->tss.fpu.hard.fp_acc;
				} else {
				    fregs = (unsigned long long *)
				        &child->tss.fpu.hard.fp_regs[0];
				    valp->fp = 
				        (unsigned long)fregs[addr - FPR_BASE];
				}
			} else {
				if (addr == FPC_CSR)
				    valp->ctl = 0;	/* FPU_DEFAULT */
				else
				    valp->fp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			valp->ctl = regs->cp0_epc;
			break;
		case CAUSE:
			valp->ctl = regs->cp0_cause;
			break;
		case BADVADDR:
			valp->ctl = regs->cp0_badvaddr;
			break;
		case MMHI:
			valp->lohi = regs->hi;
			break;
		case MMLO:
			valp->lohi = regs->lo;
			break;
		case FPC_EIR:	/* implementation / version register */
			set_cp0_status(ST0_CU1, ST0_CU1);
			__asm__ ("cfc1	%0, $0" : "=r" (valp->ctl));
			set_cp0_status(ST0_CU1, 0);
			break;
		case R5900_SA:
			valp->ctl = regs->sa;
			break;
		default:
			valp->gp = 0;
			res = -EIO;
			goto out;
		}
		res = copy_to_user(user_param, &param, sizeof(param)) ;
		goto out;

	    }

	case SYS_R5900_PTRACE_POKEU: 
	    {
		unsigned long long *fregs;
		struct pt_regs *regs;

		res = 0;
		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = valp->gp;
			break;
		case FPC_CSR:
		case R5900_FPACC:
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				if (last_task_used_math == child) {
					set_cp0_status(ST0_CU1, ST0_CU1);
					save_fp(child);
					set_cp0_status(ST0_CU1, 0);
					last_task_used_math = NULL;
					regs->cp0_status &= ~ST0_CU1;
				}
			} else {
				/* FP not yet used  */
				memset(&child->tss.fpu.hard, ~0,
				       sizeof(child->tss.fpu.hard));
				child->tss.fpu.hard.control
						= 0; /* FPU_DEAFAULT */

				/* Mark to preserve CHILD->TSS.FPU */
				child->used_math = 1;
			}
			if (addr== FPC_CSR) {
			    child->tss.fpu.hard.control = valp->ctl;
			} else if (addr == R5900_FPACC) {
			    child->tss.fpu.hard.fp_acc = valp->fp;
			} else {
			    fregs = (unsigned long long *)
				&child->tss.fpu.hard.fp_regs[0];
			    fregs[addr - FPR_BASE] 
					= (unsigned long long) valp->fp;
			}
			break;
		case PC:
			regs->cp0_epc = valp->ctl;
			break;
		case MMHI:
			regs->hi = valp->lohi;
			break;
		case MMLO:
			regs->lo = valp->lohi;
			break;
		case R5900_SA:
			regs->sa = valp->ctl;
			break;
		default:
			/* The rest are not allowed. */
			res = -EIO;
			break;
		}
		goto out;
		
	    }

	default:
		res = -EIO;
		goto out;
	}
out:
	unlock_kernel();
	return res;
}
#endif /* CONFIG_CONTEXT_R5900 */
