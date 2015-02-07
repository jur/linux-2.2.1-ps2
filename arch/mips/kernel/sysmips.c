/*
 * MIPS specific syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 *
 * $Id: sysmips.c,v 1.6 1998/08/25 09:14:42 ralf Exp $
 */
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/utsname.h>

#include <asm/cachectl.h>
#include <asm/pgtable.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>
#ifdef CONFIG_CPU_R5900
#include <asm/sys_r5900.h>
#endif

/*
 * How long a hostname can we get from user space?
 *  -EFAULT if invalid area or too long
 *  0 if ok
 *  >0 EFAULT after xx bytes
 */
static inline int
get_max_hostname(unsigned long address)
{
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, address);
	if (!vma || vma->vm_start > address || !(vma->vm_flags & VM_READ))
		return -EFAULT;
	address = vma->vm_end - address;
	if (address > PAGE_SIZE)
		return 0;
	if (vma->vm_next && vma->vm_next->vm_start == vma->vm_end &&
	   (vma->vm_next->vm_flags & VM_READ))
		return 0;
	return address;
}

asmlinkage int
sys_sysmips(int cmd, int arg1, int arg2, int arg3)
{
	int	*p;
	char	*name;
	int	flags, tmp, len, retval;

	lock_kernel();
	switch(cmd)
	{
	case SETNAME:
		retval = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;

		name = (char *) arg1;
		len = strlen_user(name);

		retval = len;
		if (len < 0)
			goto out;

		retval = -EINVAL;
		if (len == 0 || len > __NEW_UTS_LEN)
			goto out;

		copy_from_user(system_utsname.nodename, name, len);
		system_utsname.nodename[len] = '\0';
		retval = 0;
		goto out;

	case MIPS_ATOMIC_SET:
	/* This is page-faults safe */
	{ 
		static volatile int lock = 0;
		static struct wait_queue *wq = NULL;

		struct wait_queue wait = { current, NULL };
		int retry = 0;

		p = (int *) arg1;
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&wq, &wait);
		while (lock) {
			if (retry > 20) break;
			retry ++;

			if (signal_pending(current)) {
				remove_wait_queue(&wq, &wait);
				current->state = TASK_RUNNING;
				retval =   -EINTR;
				goto out;
			}
			schedule();
			current->state = TASK_INTERRUPTIBLE;
		}
		remove_wait_queue (&wq, &wait);
		current->state = TASK_RUNNING;

		if (lock) {
			retval = -EAGAIN;
			goto out;
		}
		lock ++;

		retval = verify_area(VERIFY_WRITE, p, sizeof(*p));
		if (retval) {
			goto out_atomic_set;
		}

		/* swap *p and arg2, this cause possibly page faults */
		if (__get_user(retval, p)) {
			retval = -EFAULT;
			goto out_atomic_set;
		}
		__put_user(arg2, p);

	out_atomic_set:
		lock --;
		wake_up_interruptible(&wq);
		goto out;
	}

	case MIPS_FIXADE:
		tmp = current->tss.mflags & ~3;
		current->tss.mflags = tmp | (arg1 & 3);
		retval = 0;
		goto out;

	case FLUSH_CACHE:
		flush_cache_all();
		retval = 0;
		goto out;

	case MIPS_RDNVRAM:
		retval = -EIO;
		goto out;
#ifdef CONFIG_CPU_R5900
	case MIPS_SYS_R5900:
		retval = mips_sys_r5900(arg1, arg2, arg3);
		goto out;
#endif
	default:
		retval = -EINVAL;
		goto out;
	}

out:
	unlock_kernel();
	return retval;
}

/*
 * No implemented yet ...
 */
asmlinkage int
sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}

