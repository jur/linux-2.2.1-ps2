/*
 *  PlayStation 2 CD/DVD driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: cdvdcall.h,v 1.2.6.4 2001/09/19 10:42:04 nakamura Exp $
 */

#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>

static __inline__ int ps2cdvdcall_init(void)
{
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0200)
		return -1;
#endif

	do {
		if (sbios_rpc(SBR_CDVD_INIT, NULL, &res) < 0)
			return (-1);
	} while (res == -1);

	return (res);
}

static __inline__ int ps2cdvdcall_reset(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_RESET, NULL, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_ready(int mode)
{
	struct sbr_cdvd_ready_arg arg;
	int res;

	arg.mode = mode;

	if (sbios_rpc(SBR_CDVD_READY, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_read(u_int lbn, u_int sectors, void *buf,
				       struct sceCdRMode *rmode)
{
	struct sbr_cdvd_read_arg arg;
	int res;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;

	if (sbios_rpc(SBR_CDVD_READ, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_read_dvd(u_int lbn, u_int sectors, void *buf,
				       struct sceCdRMode *rmode)
{
	struct sbr_cdvd_read_arg arg;
	int res;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;

	if (sbios_rpc(SBR_CDVD_READ_DVD, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_stop(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_STOP, NULL, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_gettoc(u_char *buf, int *len, int *media)
{
	struct sbr_cdvd_gettoc_arg arg;
	int res;

	arg.buf = buf;
	arg.len = *len;
	if (sbios_rpc(SBR_CDVD_GETTOC, &arg, &res) < 0)
		return (-1);
	*len = arg.len;
	*media = arg.media;

	return (res);
}

static __inline__ int ps2cdvdcall_readrtc(struct sbr_cdvd_rtc_arg *rtc)
{
	int res;

	if (sbios_rpc(SBR_CDVD_READRTC, rtc, &res) < 0)
		return (-1);

	return (res);
}


static __inline__ int ps2cdvdcall_mmode(int media)
{
	struct sbr_cdvd_mmode_arg arg;
	int res;

	arg.media = media;
	if (sbios_rpc(SBR_CDVD_MMODE, &arg, &res) < 0)
		return (-1);

	return (res);
}

#ifdef PS2LIBCDVD_H
static __inline__ int ps2cdvdcall_geterror(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_GETERROR, NULL, &res) < 0)
		return (SCECdErFAIL);

	return (res);
}
#endif

static __inline__ int ps2cdvdcall_gettype(int *type)
{
	return sbios_rpc(SBR_CDVD_GETTYPE, NULL, type);
}

static __inline__ int ps2cdvdcall_trayreq(int req, int *traycount)
{
	struct sbr_cdvd_trayreq_arg arg;
	int res;

	arg.req = req;
	if (sbios_rpc(SBR_CDVD_TRAYREQ, &arg, &res) < 0)
		return (-1);
	if (traycount)
		*traycount = arg.traycount; 

	return (res);
}
