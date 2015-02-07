/*
 * sys_r5900.c - r5900 spcific syscalls 
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/file.h>
#ifdef CONFIG_CONTEXT_R5900
#include <linux/ptrace.h>
#endif

#include <asm/cachectl.h>
#include <asm/pgtable.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>
#if defined(CONFIG_PERF_DEV) \
	|| defined(CONFIG_PERF_DEV_MODULE)
#include <linux/perf_dev.h>
#endif
#include <asm/sys_r5900.h>


/***  Performance couneter support ***/

/*
 * access methods to raw performance counter registers.
 */

#include <asm/perf_counter.h>

/*
 * access methods to free-running performance counter 
 * 		(extended to 63 bit) registers.
 */

#include "r5900_perf.h"
volatile struct r5900_upper_ctrs r5900_upper_ctrs;

/*
 * performance counter service.
 */


#if defined(CONFIG_PERF_DEV) || defined(CONFIG_PERF_DEV_MODULE)

static
int dev_open(const char * fname, int flags, int mode)
{
	int fd, error;
	lock_kernel();
	fd = get_unused_fd();
	if (fd >= 0) {
		struct file * f = filp_open(fname, flags, mode);
		error = PTR_ERR(f);
		if (IS_ERR(f)) {
			fd = error;
			goto out;
		}
		fd_install(fd, f);
	}
out:
	unlock_kernel();
	return fd;
}

inline
static int try_acquire_counter(void)
{
	extern struct perf_counter_operations *perf_counter_ops;
	extern u8 r5900_perf_mode;
	int fd=-1;

	if (r5900_perf_mode == PERF_MODE_SAMPLE){
		return -EBUSY;
	} 
	if (r5900_perf_mode == PERF_MODE_COUNTER) {
		struct perf_dev_internal_cmd cmd_pkt;
		void * taskp;

		cmd_pkt.cmd = PERF_WRITER;
		taskp = (void *) perf_counter_ops->control (0, 
			PERF_IOCINTERNAL, &cmd_pkt);
		if (taskp ==  current) {
			return 0;
		}
		return -EBUSY;
	}

	fd = dev_open("/dev/" PERF_DEV_COUNTER_NAME, O_RDWR, 0);
	if (fd<0) {
		return fd;
	}
	return 0;
}
#else

inline
static int try_acquire_counter(void)
{
	return 0;
}

#endif


static
int mips_r5900_perf_reg( int cmd, int arg1, int arg2 )
{
	struct sys_r5900_ctrs ctrs;
	__u32 ccr;
	__u64 cnt64;
	int reg;
	void * user_ptr; 
	int ok;

	ok = try_acquire_counter();
	if (ok < 0 )
		return ok;

	switch (cmd) {
	  case SYS_R5900_GET_CTRS:
		user_ptr = (void *)arg1;
		ctrs.ctr0 = get_CTR0_64();
		ctrs.ctr1 = get_CTR1_64();
		if (copy_to_user(user_ptr, &ctrs, sizeof(ctrs))) {
			return -EFAULT;
		}
	  	break;
	  case SYS_R5900_SET_CTRS:
		user_ptr = (void *)arg1;
		if (copy_from_user(&ctrs, user_ptr, sizeof(ctrs))) {
			return -EFAULT;
		}
		set_CTR0_64( ctrs.ctr0 );
		set_CTR1_64( ctrs.ctr1 );
		break;
	  case SYS_R5900_GET_CTR:
		reg = arg1;
		user_ptr = (void *)arg2;
		switch (reg) {
		  case R5900_CTR0:
			cnt64 = get_CTR0_64();
			break;
		  case R5900_CTR1:
			cnt64 = get_CTR1_64();
			break;
		  default:
			return -EINVAL;
			break;
		}
		if (copy_to_user(user_ptr, &cnt64, sizeof(cnt64))) {
			return -EFAULT;
		}
		break;
	  case SYS_R5900_SET_CTR:
		reg = arg1;
		user_ptr = (void *)arg2;
		if (copy_from_user(&cnt64, user_ptr, sizeof(cnt64))) {
			return -EFAULT;
		}
		switch (reg) {
		  case R5900_CTR0:
			set_CTR0_64(cnt64);
			break;
		  case R5900_CTR1:
			set_CTR1_64(cnt64);
			break;
		  default:
			return -EINVAL;
			break;
		}
		break;
	  case SYS_R5900_GET_CCR:
		user_ptr = (void *)arg1;
		ccr = get_CCR();
		if (copy_to_user(user_ptr, &ccr, sizeof(ccr))) {
			return -EFAULT;
		}
		break;
	  case SYS_R5900_SET_CCR:
		user_ptr = (void *)arg1;
		if (copy_from_user(&ccr, user_ptr, sizeof(ccr))) {
			return -EFAULT;
		}
		set_CCR( ccr );
		break;
	}
	return 0;
}


/*
 * initailize r5900 performance counter.
 */

void mips_r5900_perf_reg_init(void)
{
	set_CCR(0);
}

/* *** */
#ifdef CONFIG_CONTEXT_R5900
int sys_r5900_ptrace(long request, long pid,
                struct sys_r5900_ptrace *user_param);
#endif

/***  Generice mips_sys_r5900 service ***/

int mips_sys_r5900(int cmd, int arg1, int arg2 )
{
	switch (cmd) {
	  case SYS_R5900_GET_CTRS:
	  case SYS_R5900_SET_CTRS:
	  case SYS_R5900_GET_CTR:
	  case SYS_R5900_SET_CTR:
	  case SYS_R5900_GET_CCR:
	  case SYS_R5900_SET_CCR:
		return mips_r5900_perf_reg( cmd, arg1, arg2 );
		break;
#ifdef CONFIG_CONTEXT_R5900
	  case SYS_R5900_PTRACE_POKEU:
	  case SYS_R5900_PTRACE_PEEKU:
		return sys_r5900_ptrace((long)cmd, 
			(long)arg1, 
			(struct sys_r5900_ptrace *)arg2);
		break;
#endif
	}
	return -EINVAL;
}
