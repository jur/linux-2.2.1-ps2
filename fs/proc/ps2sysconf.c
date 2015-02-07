/*
 *  linux/fs/proc/ps2sysconf.c
 *
 *	Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: ps2sysconf.c,v 1.2.6.1 2001/08/21 06:21:50 takemura Exp $
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/ps2/sysconf.h>

extern int ps2_pccard_present;

int get_ps2sysconf(char *buffer)
{
	return sprintf(buffer,
		"EXDEVICE=0x%04x\n"
		"RGBYC=%d\n"
		"SPDIF=%d\n"
		"ASPECT=%d\n"
		"LANGUAGE=%d\n"
		"TIMEZONE=%d\n"
		"SUMMERTIME=%d\n"
		"DATENOTATION=%d\n"
		"TIMENOTATION=%d\n",
		ps2_pccard_present,
		ps2_sysconf->video,
		ps2_sysconf->spdif,
		ps2_sysconf->aspect,
		ps2_sysconf->language,
		ps2_sysconf->timezone,
		ps2_sysconf->summertime,
		ps2_sysconf->datenotation,
		ps2_sysconf->timenotation);
}
