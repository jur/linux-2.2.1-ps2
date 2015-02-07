/*
 *  PlayStation 2 CD/DVD driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: cdvdiop.c,v 1.17.6.2 2001/09/19 10:08:22 takemura Exp $
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/cdrom.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/siflock.h>
#include "cdvdcall.h"
#include "cdvd.h"

static struct sbr_common_arg carg = {
  func: ps2cdvd_intr,
  para: &carg,
};

int ps2cdvd_send_ready(int mode)
{
	int res;
	static struct sbr_cdvd_ready_arg arg;

	arg.mode = mode;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_READY, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_read(u_int lbn, u_int sectors, void *buf, 
		      struct sceCdRMode *rmode)
{
	int res;
	static struct sbr_cdvd_read_arg arg;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_READ, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_read_dvd(u_int lbn, u_int sectors, void *buf, 
		      struct sceCdRMode *rmode)
{
	int res;
	static struct sbr_cdvd_read_arg arg;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_READ_DVD, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_stop(void)
{
	int res;

	while ((res = sbios(SBR_CDVD_STOP, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_gettoc(u_char *buf, int len)
{
	int res;
	static struct sbr_cdvd_gettoc_arg arg;

	arg.buf = buf;
	arg.len = len;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_GETTOC, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_mmode(int media)
{
	int res;
	static struct sbr_cdvd_mmode_arg arg;

	arg.media = media;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_MMODE, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_geterror(void)
{
	int res;

	while ((res = sbios(SBR_CDVD_GETERROR, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

int ps2cdvd_send_gettype(void)
{
	int res;

	while ((res = sbios(SBR_CDVD_GETTYPE, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}

/*
 * Tray Request
 */
int ps2cdvd_send_trayreq(int req)
{
	int res;
	static struct sbr_cdvd_trayreq_arg arg;

	arg.req = req;
	carg.arg = &arg;

	while ((res = sbios(SBR_CDVD_TRAYREQ, &carg)) == -SIF_RPCE_SENDP)
		/* busy wait */;

	return (res);
}
