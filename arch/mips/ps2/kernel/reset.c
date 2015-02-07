/*
 *  reset.c: Reset a PlayStation 2
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 *  $Id: reset.c,v 1.13 2001/04/11 06:32:45 nakamura Exp $
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <asm/system.h>

int mips_r5900_perf_reg_init(void);
asmlinkage void sys_sync(void);
void ps2_halt(int);

static void internal_halt(int mode)
{
	switch (mode) {
	case 0:
		printk("\nBye.\n");
		break;
	case 1:
		printk("\nHalt.\n");
		break;
	case 2:
		printk("\nPush RESET button to reboot the system.\n");
		break;
	}
	sys_sync();
	udelay(1000 * 1000);

	ps2_halt(mode);
}

void ps2_machine_restart(char *command)
{
	mips_r5900_perf_reg_init();
	internal_halt(2); /* This won't return */
}

void ps2_machine_halt(void)
{
	mips_r5900_perf_reg_init();
	internal_halt(1); /* This won't return */
}

void ps2_machine_power_off(void)
{
	mips_r5900_perf_reg_init();
	internal_halt(0); /* This won't return */
}
