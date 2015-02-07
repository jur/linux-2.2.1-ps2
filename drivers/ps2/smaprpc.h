
/*
 *  smap.h -- PlayStation 2 Ethernet device driver header file
 *
 *	Copyright (C) 2001, 2002  Sony Computer Entertainment Inc.
 *	Copyright (C) 2009 - 2011 Mega Man
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 *  This driver replaces the original smap.h in linux 2.4.17
 *  This driver is intended to be used with the slim PSTwo
 *  and the smaprpc.irx module of kernelloader.
 */

#ifndef	__SMAP_H__
#define	__SMAP_H__

#include <linux/version.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/types.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#include <linux/kcomp.h>
#endif

#include <asm/smplock.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ps2/irq.h>
#include <asm/ps2/sifdefs.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#define net_device device
#endif

/*
 * SMAP control structure(smap channel)
 */
struct smaprpc_chan {
	spinlock_t spinlock;
	struct net_device *net_dev;
	u_int32_t flags;
	u_int32_t irq;
	struct net_device_stats net_stats;

	ps2sif_clientdata_t cd_smap_rpc;
	int rpc_initialized;
	struct semaphore smap_rpc_sema;

	struct task_struct *smaprun_task;
	struct completion *smaprun_compl;
	wait_queue_head_t wait_smaprun;

	struct sk_buff_head txqueue;

	void *shared_addr;
	unsigned int shared_size;
};

/* flags */
#define	SMAPRPC_F_OPENED		(1<<0)

#endif /* __SMAP_H__ */
