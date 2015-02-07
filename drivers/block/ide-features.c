/*
 *	(derived from Linux 2.4.0)
 */
/*
 * linux/drivers/block/ide-features.c	Version 0.04	June 9, 2000
 *
 *  Copyright (C) 1999-2000	Linus Torvalds & authors (see below)
 *  
 *  Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 *
 *  Extracts if ide.c to address the evolving transfer rate code for
 *  the SETFEATURES_XFER callouts.  Various parts of any given function
 *  are credited to previous ATA-IDE maintainers.
 *
 *  Auto-CRC downgrade for Ultra DMA(ing)
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ide.h"

#define CONFIG_DMA_NONPCI
#define SELECT_MASK(x, y, z)

/*
 * Similar to ide_wait_stat(), except it never calls ide_error internally.
 * This is a kludge to handle the new ide_config_drive_speed() function,
 * and should not otherwise be used anywhere.  Eventually, the tuneproc's
 * should be updated to return ide_startstop_t, in which case we can get
 * rid of this abomination again.  :)   -ml
 *
 * It is gone..........
 *
 * const char *msg == consider adding for verbose errors.
 */
int ide_config_drive_speed (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif = HWIF(drive);
	int	i, error = 1;
	byte stat;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	byte unit = (drive->select.b.unit & 0x01);
	outb(inb(hwif->dma_base+2) & ~(1<<(5+unit)), hwif->dma_base+2);
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */
        /*
         * Select the drive, and issue the SETFEATURES command
         */
	disable_irq(hwif->irq);	/* disable_irq_nosync ?? */
	udelay(1);
	SELECT_DRIVE(HWIF(drive), drive);
	SELECT_MASK(HWIF(drive), drive, 0);
	udelay(1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
	OUT_BYTE(speed, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
#if 0
	if ((IDE_CONTROL_REG) && (drive->quirk_list == 2))
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);
#endif
	udelay(1);
	/*
	 * Wait for drive to become non-BUSY
	 */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		unsigned long flags, timeout;
		__save_flags(flags);	/* local CPU only */
		ide__sti();		/* local CPU only -- for jiffies */
		timeout = jiffies + WAIT_CMD;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (0 < (signed long)(jiffies - timeout))
				break;
		}
		__restore_flags(flags); /* local CPU only */
	}

	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT)) {
			error = 0;
			break;
		}
	}

	SELECT_MASK(HWIF(drive), drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	if (speed > XFER_PIO_4) {
		outb(inb(hwif->dma_base+2)|(1<<(5+unit)), hwif->dma_base+2);
	} else {
		outb(inb(hwif->dma_base+2) & ~(1<<(5+unit)), hwif->dma_base+2);
	}
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	switch(speed) {
		case XFER_UDMA_7:   drive->id->dma_ultra |= 0x8080; break;
		case XFER_UDMA_6:   drive->id->dma_ultra |= 0x4040; break;
		case XFER_UDMA_5:   drive->id->dma_ultra |= 0x2020; break;
		case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
		case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
		case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
		case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
		case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
		case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
		case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
		case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
		case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
		case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
		case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		default: break;
	}
	return error;
}

EXPORT_SYMBOL(ide_config_drive_speed);
