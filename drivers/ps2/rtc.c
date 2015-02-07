/*
 *  PlayStation 2 Real Time Clock driver
 *
 *        Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: rtc.c,v 1.1.4.3 2001/09/19 10:42:05 nakamura Exp $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>
#include <linux/rtc.h>
#include <asm/uaccess.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/siflock.h>
#include "cdvdcall.h"

ps2sif_lock_t *ps2rtc_lock;

extern unsigned long mktime(unsigned int year, unsigned int mon,
				unsigned int day, unsigned int hour,
				unsigned int min, unsigned int sec);
extern void mkdate(unsigned long elapse, unsigned int *year, unsigned int *mon,
				unsigned int *day, unsigned int *hour,
				unsigned int *min, unsigned int *sec,
				unsigned int *dayofweek);

#define PS2_RTC_TZONE	(9 * 60 * 60)

static int bcd_to_bin(int val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}

static int bin_to_bcd(int val)
{
	return ((val / 10) << 4) + (val % 10);
}

static int read_rtc(struct rtc_time *tm)
{
	int res;
	struct sbr_cdvd_rtc_arg rtc_arg;
	unsigned long elapse;

	if (ps2sif_lock(ps2rtc_lock, "read rtc") < 0)
		return -EIO;
	res = ps2cdvdcall_readrtc(&rtc_arg);
	ps2sif_unlock(ps2rtc_lock);

	if (res != 1)
		return -EIO;

	if (rtc_arg.stat != 0)
		return -EIO;

	tm->tm_sec = bcd_to_bin(rtc_arg.second);
	tm->tm_min = bcd_to_bin(rtc_arg.minute);
	tm->tm_hour = bcd_to_bin(rtc_arg.hour);
	tm->tm_mday = bcd_to_bin(rtc_arg.day);
	tm->tm_mon = bcd_to_bin(rtc_arg.month);
	tm->tm_year = bcd_to_bin(rtc_arg.year);

	/* convert PlayStation 2 system time (JST) to UTC */
	elapse = mktime(tm->tm_year + 2000, tm->tm_mon, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	elapse -= PS2_RTC_TZONE;
	mkdate(elapse, &tm->tm_year, &tm->tm_mon, &tm->tm_mday,
			&tm->tm_hour, &tm->tm_min, &tm->tm_sec, 0);
	tm->tm_year -= 1900;
	tm->tm_mon--; 

	return 0;
}


static int ps2rtc_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int res;
	struct rtc_time wtime;

	switch (cmd) {
	case RTC_RD_TIME:
		res = read_rtc(&wtime);
		if (res < 0)
			return res;
		return copy_to_user((void *)arg, &wtime, sizeof(wtime)) ? -EFAULT : 0;
	}
	return -EINVAL;
}

static int ps2rtc_open(struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int ps2rtc_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static loff_t ps2rtc_llseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;		/* cannot seek */
}

static struct file_operations ps2rtc_fops = {
	ps2rtc_llseek,		/* llseek (error) */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	ps2rtc_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ps2rtc_open,		/* open */
	NULL,			/* flush */
	ps2rtc_release,		/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
};

static struct miscdevice rtc_dev = {
	RTC_MINOR,
	"rtc",
	&ps2rtc_fops
};

static void ps2rtc_cleanup(void)
{
	misc_deregister(&rtc_dev);
}

__initfunc(int ps2rtc_init(void))
{
	if ((ps2rtc_lock = ps2sif_getlock(PS2LOCK_RTC)) == NULL) {
		printk(KERN_ERR "ps2rtc: Can't get lock\n");
		return -EINVAL;
	}

	if (ps2cdvdcall_init()) {
		printk(KERN_ERR "ps2rtc: Can't initialize CD/DVD-ROM subsystem\n");
		return -EIO;
	}

	printk(KERN_INFO "PlayStation 2 Real Time Clock driver\n");
	misc_register(&rtc_dev);

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return ps2rtc_init();
}

void cleanup_module(void)
{
	ps2rtc_cleanup();
}
#endif /* MODULE */
