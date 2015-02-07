/*
 * sbcall.c: SBIOS support routines
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sbcall.c,v 1.5.6.1 2001/11/14 04:39:09 nakamura Exp $
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/ps2/irq.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>

typedef struct t_SifCmdHeader
{
	u32 size;
	void *dest;
	int cid;
	u32 unknown;
} SifCmdHeader_t;

static uint32_t usrCmdHandler[256];

typedef struct {
	struct t_SifCmdHeader    sifcmd;
	u32 data[16];
} iop_sifCmdBufferIrq_t;

void handleRPCIRQ(iop_sifCmdBufferIrq_t *sifCmdBufferIrq, void *arg)
{
	extern void handleSimulatedIRQ(int irq);

	handleSimulatedIRQ(sifCmdBufferIrq->data[0]);
}

static void handlePowerOff(void *sifCmdBuffer, void *arg)
{
	kill_proc(1, SIGPWR, 1);
}

/*
 *  SIF DMA functions
 */

unsigned int ps2sif_setdma(ps2sif_dmadata_t *sdd, int len)
{
    struct sb_sifsetdma_arg arg;
    arg.sdd = sdd;
    arg.len = len;
    return sbios(SB_SIFSETDMA, &arg);
}

int ps2sif_dmastat(unsigned int id)
{
    struct sb_sifdmastat_arg arg;
    arg.id = id;
    return sbios(SB_SIFDMASTAT, &arg);
}

void ps2sif_writebackdcache(void *addr, int size)
{
    dma_cache_wback_inv((unsigned long)addr, size);
}


/*
 *  SIF RPC functions
 */

int ps2sif_getotherdata(ps2sif_receivedata_t *rd, void *src, void *dest, int size, unsigned int mode, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifgetotherdata_arg arg;
    arg.rd = rd;
    arg.src = src;
    arg.dest = dest;
    arg.size = size;
    arg.mode = mode;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFGETOTHERDATA, &arg);
}

int ps2sif_bindrpc(ps2sif_clientdata_t *bd, unsigned int command, unsigned int mode, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifbindrpc_arg arg;
    arg.bd = bd;
    arg.command = command;
    arg.mode = mode;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFBINDRPC, &arg);
}

int ps2sif_callrpc(ps2sif_clientdata_t *bd, unsigned int fno, unsigned int mode, void *send, int ssize, void *receive, int rsize, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifcallrpc_arg arg;
    arg.bd = bd;
    arg.fno = fno;
    arg.mode = mode;
    arg.send = send;
    arg.ssize = ssize;
    arg.receive = receive;
    arg.rsize = rsize;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFCALLRPC, &arg);
}

int ps2sif_checkstatrpc(ps2sif_rpcdata_t *cd)
{
    struct sb_sifcheckstatrpc_arg arg;
    arg.cd = cd;
    return sbios(SB_SIFCHECKSTATRPC, &arg);
}

void ps2sif_setrpcqueue(ps2sif_queuedata_t *pSrqd, void (*callback)(void*), void *aarg)
{
    struct sb_sifsetrpcqueue_arg arg;
    arg.pSrqd = pSrqd;
    arg.callback = callback;
    arg.arg = aarg;
    sbios(SB_SIFSETRPCQUEUE, &arg);
}

void ps2sif_registerrpc(ps2sif_servedata_t *pr, unsigned int command,
			ps2sif_rpcfunc_t func, void *buff,
			ps2sif_rpcfunc_t cfunc, void *cbuff,
			ps2sif_queuedata_t *pq)
{
    struct sb_sifregisterrpc_arg arg;
    arg.pr = pr;
    arg.command = command;
    arg.func = func;
    arg.buff = buff;
    arg.cfunc = cfunc;
    arg.cbuff = cbuff;
    arg.pq = pq;
    sbios(SB_SIFREGISTERRPC, &arg);
}

ps2sif_servedata_t *ps2sif_removerpc(ps2sif_servedata_t *pr, ps2sif_queuedata_t *pq)
{
    struct sb_sifremoverpc_arg arg;
    arg.pr = pr;
    arg.pq = pq;
    return (ps2sif_servedata_t *)sbios(SB_SIFREMOVERPC, &arg);
}

ps2sif_queuedata_t *ps2sif_removerpcqueue(ps2sif_queuedata_t *pSrqd)
{
    struct sb_sifremoverpcqueue_arg arg;
    arg.pSrqd = pSrqd;
    return (ps2sif_queuedata_t *)sbios(SB_SIFREMOVERPCQUEUE, &arg);
}

ps2sif_servedata_t *ps2sif_getnextrequest(ps2sif_queuedata_t *qd)
{
    struct sb_sifgetnextrequest_arg arg;
    arg.qd = qd;
    return (ps2sif_servedata_t *)sbios(SB_SIFGETNEXTREQUEST, &arg);
}

void ps2sif_execrequest(ps2sif_servedata_t *rdp)
{
    struct sb_sifexecrequest_arg arg;
    arg.rdp = rdp;
    sbios(SB_SIFEXECREQUEST, &arg);
}


/*
 *  SBIOS blocking RPC function
 */

struct rpc_wait_queue {
    struct wait_queue *wq;
    volatile int woken;
};

static void rpc_wakeup(void *p, int result)
{
    struct rpc_wait_queue *rwq = (struct rpc_wait_queue *)p;

    rwq->woken = 1;
    if (rwq->wq != NULL)
	wake_up(&rwq->wq);
}

int sbios_rpc(int func, void *arg, int *result)
{
    int ret;
    unsigned long flags;
    struct rpc_wait_queue rwq;
    struct sbr_common_arg carg;

    carg.arg = arg;
    carg.func = rpc_wakeup;
    carg.para = &rwq;

    rwq.wq = NULL;
    rwq.woken = 0;

    save_flags(flags); cli();
    do {
	ret = sbios(func, &carg);
	switch (ret) {
	case 0:
	    break;
	case -SIF_RPCE_SENDP:
	    /* resouce temporarily unavailable */
	    break;
	default:
	    /* ret == -SIF_PRCE_GETP (=1) */
	    restore_flags(flags);
	    *result = ret;
	    printk("sbios_rpc: RPC failed, result=%d\n", ret);
	    return ret;
	}
    } while (ret < 0);

    while (!rwq.woken)
	sleep_on(&rwq.wq);

    restore_flags(flags);
    *result = carg.result;
    return 0;
}


/*
 *  Miscellaneous functions
 */

void ps2_halt(int mode)
{
    struct sb_halt_arg arg;
    arg.mode = mode;
    sbios(SB_HALT, &arg);
}

void ps2_setdve(int mode)
{
    struct sb_setdve_arg arg;
    arg.mode = mode;
    sbios(SB_SETDVE, &arg);
}

int ps2_setgscrt(int inter, int omode, int ffmode, int *dx1, int *dy1, int *dx2, int *dy2)
{
    struct sb_setgscrt_arg arg;
    arg.inter = inter;
    arg.omode = omode;
    arg.ffmode = ffmode;
    arg.dx1 = dx1;
    arg.dy1 = dy1;
    arg.dx2 = dx2;
    arg.dy2 = dy2;
    return sbios(SB_SETGSCRT, &arg);
}


/*
 *  Initialize
 */

static void sif0_dma_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    sbios(SB_SIFCMDINTRHDLR, 0);
}

__initfunc(int ps2sif_init(void))
{
    struct sb_sifaddcmdhandler_arg addcmdhandlerparam;
    struct sb_sifsetcmdbuffer_arg setcmdhandlerbufferparam;

    setcmdhandlerbufferparam.db = usrCmdHandler;
    setcmdhandlerbufferparam.size = sizeof(usrCmdHandler) / 8;

    if (sbios(SB_SIFINIT, 0) < 0)
	return -1;
    if (sbios(SB_SIFINITCMD, 0) < 0)
	return -1;
    if (request_irq(IRQ_DMAC_5, sif0_dma_handler, SA_INTERRUPT, "SIF0 DMA", NULL))
	return -1;
    if (sbios(SB_SIFSETCMDBUFFER, &setcmdhandlerbufferparam) < 0) {
        printk("Failed to initialize EEDEBUG handler (1).\n");
    } else {
        addcmdhandlerparam.fid = 0x20;
        addcmdhandlerparam.func = handleRPCIRQ;
        addcmdhandlerparam.data = NULL;
        if (sbios(SB_SIFADDCMDHANDLER, &addcmdhandlerparam) < 0) {
            printk("Failed to initialize SIF IRQ handler.\n");
        }

        /* The module poweroff.irx is configured to inform us. */
        addcmdhandlerparam.fid = 20;
        addcmdhandlerparam.func = handlePowerOff;
        addcmdhandlerparam.data = NULL;
        if (sbios(SB_SIFADDCMDHANDLER, &addcmdhandlerparam) < 0) {
            printk("Failed to initialize SIF Power Off handler.\n");
        }
    }
    if (sbios(SB_SIFINITRPC, 0) < 0)
	return -1;

    if (ps2sif_initiopheap() < 0)
	return -1;

    printk("PlayStation 2 SIF BIOS: %04x\n", sbios(SB_GETVER, 0));
    return 0;
}

void ps2sif_exit(void)
{
    sbios(SB_SIFEXITRPC, 0);
    sbios(SB_SIFEXITCMD, 0);
    free_irq(IRQ_DMAC_5, NULL);
    sbios(SB_SIFEXIT, 0);
}
