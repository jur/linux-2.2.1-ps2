/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: rtc.c,v 1.4 2000/09/26 05:42:36 takemura Exp $
 */
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <asm/io.h>
#include <asm/ps2/irq.h>

#define PS2RTC_DEBUG
#ifdef PS2RTC_DEBUG
int ps2rtc_debug = 0;
#define DPRINT(fmt, args...) \
	if (ps2rtc_debug) printk("ps2 rtc: " fmt, ## args)
#define DPRINTK(fmt, args...) \
	if (ps2rtc_debug) printk(fmt, ## args)
#else
#define DPRINT(fmt, args...)
#endif

void mkdate(unsigned long elapse, unsigned int *year, unsigned int *mon,
	    unsigned int *day, unsigned int *hour,
	    unsigned int *min, unsigned int *sec, unsigned int *dayofweek);

static unsigned int regs[14] = {
[RTC_REG_A] = RTC_REF_CLCK_32KHZ ,
[RTC_REG_B] = (RTC_DM_BINARY | RTC_24H),
};

static char *reg_names[14] = {
[RTC_SECONDS] =		"sec",
[RTC_SECONDS_ALARM] =	"sec alm",
[RTC_MINUTES] =		"min",
[RTC_MINUTES_ALARM] =	"min alm",
[RTC_HOURS] =		"hours",
[RTC_HOURS_ALARM] =	"hours alm",
[RTC_DAY_OF_WEEK] =	"day of week",
[RTC_DAY_OF_MONTH] =	"day",
[RTC_MONTH] =		"mon",
[RTC_YEAR] =		"year",
[RTC_REG_A] =		"reg A",
[RTC_REG_B] =		"reg B",
[RTC_REG_C] =		"reg C",
[RTC_REG_D] =		"reg D",
};

static unsigned char ps2_rtc_read_data(unsigned long addr)
{
	struct timeval tv;
	unsigned char res;

	if (13 < addr) {
		DPRINT("RTC: read rtc[%11s(%ld)]=0(ignored)\n", "???", addr);
		return (0);
	}

	do_gettimeofday(&tv);
	mkdate(tv.tv_sec,
	       &regs[RTC_YEAR], &regs[RTC_MONTH], &regs[RTC_DAY_OF_MONTH],
	       &regs[RTC_HOURS], &regs[RTC_MINUTES], &regs[RTC_SECONDS],
	       &regs[RTC_DAY_OF_WEEK]);

	regs[RTC_YEAR] -= 1952; /* Digital UNIX epoch */
	regs[RTC_YEAR] %= 100;
	res = (regs[addr] & 0xff);

	if (regs[RTC_CONTROL] & RTC_DM_BINARY) {
		DPRINT(" read rtc[%11s(%2ld)]=%d\n",
		       reg_names[addr], addr, res);
	} else {
		BIN_TO_BCD(res);
		DPRINT(" read rtc[%11s(%2ld)]=0x%02x\n",
		       reg_names[addr], addr, res);
	}

	return (res);
}

static void ps2_rtc_write_data(unsigned char data, unsigned long addr)
{
	DPRINT("write rtc[%11s(%2ld)]=0x%02d (ignored)\n",
	       reg_names[addr], addr, data);
}

static int ps2_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops ps2_rtc_ops = {
	&ps2_rtc_read_data,
	&ps2_rtc_write_data,
	&ps2_rtc_bcd_mode
};
