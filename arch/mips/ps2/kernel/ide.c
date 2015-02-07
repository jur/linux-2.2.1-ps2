/*
 * ide.c: machine dependent IDE support routines
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: ide.c,v 1.18 2001/03/27 03:26:08 nakamura Exp $
 */
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mc146818rtc.h>
#include <asm/spinlock.h>
#include <asm/ptrace.h>
#include <asm/ide.h>
#include <asm/io.h>
#include <asm/ps2/irq.h>
#include "../../../../drivers/block/ide.h"

#ifdef DEBUG
#define DPRINT(fmt, args...) \
	do { \
		printk("ps2ide: " fmt, ## args); \
	} while (0)
#define DPRINTK(fmt, args...) \
	do { \
		printk(fmt, ## args); \
	} while (0)
#else
#define DPRINT(fmt, args...) do {} while (0)
#define DPRINTK(fmt, args...) do {} while (0)
#endif

/*
 * IDE port base address
 */
#define AIF_IDE		((ide_ioreg_t)0xb8000060)
#define PCCARD_IDE	((ide_ioreg_t)0xb40001f0)
#define PCCARD_IDE2	((ide_ioreg_t)0xb4000170)
#define PCCARD_IDE3	((ide_ioreg_t)0xb4000180)
#define PS2HDD_IDE	((ide_ioreg_t)0xb4000040)

#ifdef CONFIG_T10000_AIFHDD
void ps2_ide_tune_drive(ide_drive_t *drive, byte mode_wanted);
#endif

#ifdef CONFIG_BLK_DEV_IDEDMA_PS2
void ps2_ide_setup_dma(ide_hwif_t *hwif);
#endif

static ide_ioreg_t ps2_ide_bases[3];
static int ps2_ide_nbases = 0;
static spinlock_t spinlock;
static int init_flag = 0;

void ps2_ide_port_found(ide_ioreg_t base)
{
	DPRINT("IDE port found at %08lx\n", base);
	ps2_ide_bases[ps2_ide_nbases++] = base;
}

#if defined(CONFIG_PS2_PCCARDIDE) || defined(CONFIG_PS2_HDD)
static void ps2_ide_select(ide_drive_t *drive)
{
	int retry;
	unsigned long timeout;
	ide_hwif_t *hwif = HWIF(drive);

	if (drive->drive_data || drive->select.b.unit)
		return;

	DPRINT("waiting for spinup... 0x%x", hwif->io_ports[0]);
	for (retry = 0; retry < 160; retry++) { /* 50msec X 160 = 8sec */
		/* write select command */
		OUT_BYTE(drive->select.all, hwif->io_ports[IDE_SELECT_OFFSET]);

		/* wait 50msec */
		timeout = jiffies + ((HZ + 19)/20) + 1;
		while (0 < (signed long)(timeout - jiffies));

		/* see if we've select successfully */
		if (IN_BYTE(hwif->io_ports[IDE_SELECT_OFFSET]) ==
		    drive->select.all) {
			DPRINTK(": OK");
			break;
		}
	}
	DPRINTK("\n");
	drive->drive_data = 1;
}
#endif

static int ps2_ide_default_irq(ide_ioreg_t base)
{
	switch (base) {
#ifdef CONFIG_T10000_AIFHDD
	case AIF_IDE:
		return (IRQ_SBUS_AIF);
#endif
#if defined(CONFIG_PS2_PCCARDIDE) || defined(CONFIG_PS2_HDD)
	case PCCARD_IDE:
	case PCCARD_IDE2:
	case PCCARD_IDE3:
	case PS2HDD_IDE:
		return (IRQ_SBUS_PCIC);
#endif
	default:
		return 0;
	}
}

static ide_ioreg_t ps2_ide_default_io_base(int index)
{
	if (0 <= index && index < ps2_ide_nbases) {
		DPRINT("ps2_ide_default_io_base: %lx\n", ps2_ide_bases[index]);
		return (ps2_ide_bases[index]);
	} else {
		return 0;
	}
}

static void ps2_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base,
				    int *irq)
{
	int i;
	ide_ioreg_t port;
	ide_hwif_t *hwif;

	if (!init_flag) {
		init_flag = 1;
		spin_lock_init(&spinlock);
	}

	/*
	 * XXX, very ugly...
	 * It might be better to make new IDE chip routine set.
	 */
	hwif = (ide_hwif_t *)((void *)p -
			      ((void *)&((ide_hwif_t *)0)->io_ports));

	port = base;
	switch (base) {
#ifdef CONFIG_T10000_AIFHDD
	case AIF_IDE:
		for (i = 0; i < 8; i++) {
			*p++ = port;
			port += 2;
		}
		*p++ = base + 0x1c;
		if (irq != NULL)
			*irq = IRQ_SBUS_AIF;

		hwif->tuneproc = (ide_tuneproc_t *)ps2_ide_tune_drive;
		for (i = 0; i < MAX_DRIVES; i++)
			hwif->drives[i].autotune = 1;
		break;
#endif /*  CONFIG_T10000_AIFHDD */
#ifdef CONFIG_PS2_PCCARDIDE
	case PCCARD_IDE:
	case PCCARD_IDE2:
	case PCCARD_IDE3:
		for (i = 0; i < 8; i++)
			*p++ = port++;
		*p++ = base + 0x206;
		if (irq != NULL)
			*irq = IRQ_SBUS_PCIC;

		hwif->selectproc = ps2_ide_select;
		break;
#endif /* CONFIG_PS2_PCCARDIDE */
#ifdef CONFIG_PS2_HDD
	case PS2HDD_IDE:
		for (i = 0; i < 8; i++) {
			*p++ = port;
			port += 2;
		}
		*p++ = base + 0x1c;
		if (irq != NULL)
			*irq = IRQ_SBUS_PCIC;

		hwif->selectproc = ps2_ide_select;
#ifdef CONFIG_BLK_DEV_IDEDMA_PS2
		ps2_ide_setup_dma(hwif);
#endif
		break;
#endif /*  CONFIG_PS2_HDD */
	default:
		if (base != 0) {
			printk(KERN_CRIT
			       "ps2 ide: unknown base address %lx\n", base);
			for (i = 0; i < 9; i++) {
				*p++ = 0;
			}
			if (irq != NULL)
				*irq = 0;
		}
		break;
	}
}

static int ps2_ide_request_irq(unsigned int irq,
			       void (*handler)(int,void *, struct pt_regs *),
			       unsigned long flags, const char *device,
			       void *dev_id)
{
	return request_irq(irq, handler, flags, device, dev_id);
}			

static void ps2_ide_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}

static int ps2_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	return check_region(from, extent);
}

static void ps2_ide_request_region(ide_ioreg_t from, unsigned int extent,
				   const char *name)
{
	request_region(from, extent, name);
}

static void ps2_ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	release_region(from, extent);
}

struct ide_ops ps2_ide_ops = {
	&ps2_ide_default_irq,
	&ps2_ide_default_io_base,
	&ps2_ide_init_hwif_ports,
	&ps2_ide_request_irq,
	&ps2_ide_free_irq,
	&ps2_ide_check_region,
	&ps2_ide_request_region,
	&ps2_ide_release_region
};
