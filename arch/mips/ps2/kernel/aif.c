/*
 * 	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: aif.c,v 1.17 2001/04/11 06:33:41 nakamura Exp $
 *
 */
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mc146818rtc.h>
#include <asm/ptrace.h>
#include <asm/ide.h>
#include <asm/io.h>
#include <asm/ps2/irq.h>
#include "../../../../drivers/block/ide.h"

#define CONFIG_IDE_CHIPSETS
#define _IDE_C
#include "../../../../drivers/block/ide_modes.h"

/* AIF register mapping (half word) */

#define AIF_IDENT	0x00
#define AIF_REVISION	0x01
#define AIF_INTSR	0x02
#define AIF_INTCL	0x02
#define AIF_INTEN	0x03
#define AIF_TIMCFG	0x04
#define AIF_COUNT_L	0x08
#define AIF_COUNT_H	0x09
#define AIF_ATA_TCFG	0x20
#define AIF_ATA		0x30
#define AIF_ATACTL	0x3c
#define AIF_RTC		0x80

#define AIF	((volatile u16 *)0xb8000000)
#define ATA	(&AIF[AIF_ATA])
#define RTC	(&AIF[AIF_RTC])

int ps2_aif_probe = 0;
extern void ps2_ide_port_found(ide_ioreg_t base);
#ifdef CONFIG_T10000_AIFRTC
static struct rtc_ops ps2_aif_rtc_ops;
#endif

__initfunc(void ps2_aif_init(void))
{
    if (AIF[AIF_IDENT] == 0xa1) {
	printk("AIF: controller revision %d", AIF[AIF_REVISION]);
#ifdef CONFIG_T10000_AIFHDD
	printk(", use HDD");
#endif
#ifdef CONFIG_T10000_AIFRTC
	printk(", use RTC");
#endif
	printk("\n");
	ps2_aif_probe++;
	AIF[AIF_INTCL] = 7;	/* clear interrupt flag */
	AIF[AIF_ATA_TCFG] = 0;	/* ATA timing configuration */
#ifdef CONFIG_T10000_AIFRTC
	rtc_ops = &ps2_aif_rtc_ops;
#endif
    } else {
	printk("AIF: controller not found\n");
    }
}

#ifdef CONFIG_T10000_AIFRTC
unsigned char ps2_aif_rtc_read_data(unsigned long addr)
{
    return RTC[addr & 0x7f] & 0xff;
}

void ps2_aif_rtc_write_data(unsigned char data, unsigned long addr)
{
    RTC[addr & 0x7f] = data;
}

static int ps2_aif_rtc_bcd_mode(void)
{
	return 1;
}

static struct rtc_ops ps2_aif_rtc_ops = {
	&ps2_aif_rtc_read_data,
	&ps2_aif_rtc_write_data,
	&ps2_aif_rtc_bcd_mode
};
#endif /* CONFIG_T10000_AIFRTC */

#ifdef CONFIG_T10000_AIFHDD
#ifdef CONFIG_BLK_DEV_IDE

/*
 * AIF IDE I/F operations
 */
void ps2_ide_tune_drive(ide_drive_t *drive, byte mode_wanted)
{
    ide_pio_data_t d;
    int mode = 0;
    int unit = -1;
    int i;

    if (IDE_DATA_REG != (long)ATA)
	return;

    for (i = 0; i < MAX_DRIVES; i++) {
	if (&((ide_hwif_t *)drive->hwif)->drives[i] == drive) {
	    unit = i;
	    break;
	}
    }

    ide_get_best_pio_mode(drive, mode_wanted, 4, &d);
    mode = (d.use_iordy << 3) | d.pio_mode;
    printk("%s: AIF tune: unit%d, mode=%d\n", drive->name, unit, mode);
    switch (unit) {
    case 0:
	AIF[AIF_ATA_TCFG] = (AIF[AIF_ATA_TCFG] & 0xf0) + mode;
	break;
    case 1:
	AIF[AIF_ATA_TCFG] = (AIF[AIF_ATA_TCFG] & 0x0f) + (mode << 4);
	break;
    default:
	break;
    }
}

#endif	/* CONFIG_BLKDEV_IDE */
#endif	/* CONFIG_T10000_AIFHDD */
