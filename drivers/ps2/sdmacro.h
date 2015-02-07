/*
 *  PlayStation 2 Sound driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * derived from common/include/sdmacro.h (release 2.0.3)
 *
 * $Id: sdmacro.h,v 1.3.6.2 2001/09/19 10:08:23 takemura Exp $
 */

#ifndef PS2SDMACRO_H
#define PS2SDMACRO_H

/*
 * SPDIF OUT
 */
#define SD_SPDIF_OUT_PCM	0
#define SD_SPDIF_OUT_BITSTREAM  1
#define SD_SPDIF_OUT_OFF	2

#define SD_SPDIF_COPY_NORMAL    0x00
#define SD_SPDIF_COPY_PROHIBIT  0x80

#define SD_SPDIF_MEDIA_CD       0x000
#define SD_SPDIF_MEDIA_DVD      0x800

#define SD_BLOCK_MEM_DRY        0  /* no use */

/*
 * MIX
 */
#define SD_MMIX_SINER  (1 <<  0)
#define SD_MMIX_SINEL  (1 <<  1)
#define SD_MMIX_SINR   (1 <<  2)
#define SD_MMIX_SINL   (1 <<  3)
#define SD_MMIX_MINER  (1 <<  4)
#define SD_MMIX_MINEL  (1 <<  5)
#define SD_MMIX_MINR   (1 <<  6)
#define SD_MMIX_MINL   (1 <<  7)
#define SD_MMIX_MSNDER (1 <<  8)
#define SD_MMIX_MSNDEL (1 <<  9)
#define SD_MMIX_MSNDR  (1 << 10)
#define SD_MMIX_MSNDL  (1 << 11)

/*
 * transfer
 */
#define SD_TRANS_MODE_WRITE 0
#define SD_TRANS_MODE_READ  1
#define SD_TRANS_MODE_STOP  2
#define SD_TRANS_MODE_WRITE_FROM 3

#define SD_TRANS_BY_DMA     (0x0<<3)
#define SD_TRANS_BY_IO      (0x1<<3)

#define SD_BLOCK_ONESHOT (0<<4) 
#define SD_BLOCK_LOOP (1<<4) 

#define SD_TRANS_STATUS_WAIT  1
#define SD_TRANS_STATUS_CHECK 0

/*
 * 32bit mode: with `channel' argument
 */
#define SD_BLOCK_MEMIN0 (1<<2) 
#define SD_BLOCK_MEMIN1 (1<<3) 
#define SD_BLOCK_NORMAL (0<<4) 
#define SD_BLOCK_M32    (1<<4) 
#define SD_BLOCK_M32A   (1<<6) 
#define SD_BLOCK_M32D   (1<<7) 

#endif /* PS2SDMACRO_H */
