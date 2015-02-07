/*
 *  powerbutton.c: PlayStation 2 power button handling
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 *  $Id: powerbutton.c,v 1.1.2.2 2001/08/31 05:53:57 nakamura Exp $
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <asm/signal.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>
#include <asm/ps2/powerbutton.h>

#define POWEROFF_SID 0x9090900

#define PWRBTN_MODE_SHUTDOWN             0x01
#define PWRBTN_MODE_ENABLE_AUTO_SHUTOFF  0x02

struct rpc_wait_queue {
    struct wait_queue *wq;
    volatile int woken;
};

static ps2sif_clientdata_t cd_poweroff_rpc;
static int rpc_initialized;
static struct semaphore poweroff_rpc_sema;
static void powerbutton_handler(void *);

static void rpc_wakeup(void *arg)
{
    struct rpc_wait_queue *rwq = (struct rpc_wait_queue *)arg;

    rwq->woken = 1;
    if (rwq->wq != NULL)
	wake_up(&rwq->wq);
}

/* Install powerhook with RTE (module CDVDFSV).
 * This will not work with TGE.
 */
static int __init rte_powerhook(void)
{
	int res;
	struct sb_cdvd_powerhook_arg arg;

	/*
	 * XXX, you should get the CD/DVD lock.
	 * But it might be OK because this routine will be called
	 * in early stage of boot sequence.
	 */

	/* initialize CD/DVD */
	do {
		if (sbios_rpc(SBR_CDVD_INIT, NULL, &res) < 0)
			return (-1);
	} while (res == -1);

	/* install power button hook */
	arg.func = powerbutton_handler;
	arg.arg = NULL;
	sbios(SB_CDVD_POWERHOOK, &arg);

	return (0);
}

/* Install powerhook with TGE (module poweroff.irx).
 * This will not work with RTE.
 */
static int __init tge_powerhook(void)
{
	int loop;
	struct rpc_wait_queue rwq;
	int rv;
	volatile int j;
	unsigned long flags;

	sema_init(&poweroff_rpc_sema, 1);

	rwq.wq = NULL;
	rwq.woken = 0;

	/* bind poweroff.irx module */
	for (loop = 100; loop; loop--) {
		save_flags(flags); cli();
		rv = ps2sif_bindrpc(&cd_poweroff_rpc, POWEROFF_SID,
			SIF_RPCM_NOWAIT, rpc_wakeup, (void *)&rwq);
		if (rv < 0) {
			printk("poweroff.irx: bind rv = %d.\n", rv);
			break;
		}
		while (!rwq.woken)
			sleep_on(&rwq.wq);
		if (cd_poweroff_rpc.serve != 0)
			break;
		restore_flags(flags);
		j = 0x010000;
		while (j--) ;
	}
	if (cd_poweroff_rpc.serve == 0) {
		restore_flags(flags);
		printk("poweroff.irx bind error 1, power button will not work.\n");
		return -1;
	}
	rpc_initialized = -1;
	restore_flags(flags);
	return 0;
}

int ps2_powerbutton_enable_auto_shutoff(int enable_auto_shutoff)
{
	struct rpc_wait_queue rwq;
	int rv;
	unsigned long flags;

	if (!rpc_initialized) {
		return -1;
	}

	rwq.wq = NULL;
	rwq.woken = 0;
	down(&poweroff_rpc_sema);
	do {
		save_flags(flags); cli();
		rv = ps2sif_callrpc(&cd_poweroff_rpc, PWRBTN_MODE_ENABLE_AUTO_SHUTOFF,
			SIF_RPCM_NOWAIT,
			NULL, 0,
			&enable_auto_shutoff, sizeof(enable_auto_shutoff),
			(ps2sif_endfunc_t) rpc_wakeup,
			(void *)&rwq);
		if (rv == -E_SIF_PKT_ALLOC) {
			restore_flags(flags);
		}
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv != 0) {
		printk("ps2_powerbutton_enable_auto_shutoff callrpc failed, (%d)\n", rv);
	} else {
		while (!rwq.woken)
			sleep_on(&rwq.wq);
	}
	restore_flags(flags);
	up(&poweroff_rpc_sema);
	return rv;
}


int __init ps2_powerbutton_init(void)
{
	int rte_rv;
	int tge_rv;

	rpc_initialized = 0;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0201)
	    	return (-1);
#endif

	rte_rv = rte_powerhook();
	tge_rv = tge_powerhook();

	if ((rte_rv == 0) || (tge_rv ==0)) {
		return 0;
	} else {
		return -1;
	}
 }

static void powerbutton_handler(void *arg)
{
	kill_proc(1, SIGPWR, 1);
}
