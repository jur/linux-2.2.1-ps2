/*
 * linux/include/asm-mips/ps2/dmarelay.h
 *
 *        Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: dmarelay.h,v 1.1 2001/02/14 05:36:21 nakamura Exp $
 */

#ifndef __ASM_PS2_DMARELAY_H
#define __ASM_PS2_DMARELAY_H

#define SIFNUM_ATA_DMA_BEGIN	0x2000
#define SIFNUM_GetBufAddr	0
#define SIFNUM_DmaRead          1
#define SIFNUM_DmaWrite         2
#define SIFNUM_ATA_DMA_END	0x2001

#define ATA_MAX_ENTRIES		256
#define ATA_BUFFER_SIZE		(512 * ATA_MAX_ENTRIES)

struct ata_dma_request {
    int command;
    int size;
    int count;
    int devctrl;
    ps2sif_dmadata_t sdd[ATA_MAX_ENTRIES];
};

#endif /* __ASM_PS2_DMARELAY_H */
