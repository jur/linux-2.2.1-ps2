/*
 * original file: cacheops.h
 *
 * Cache operations for the cache instruction.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1996, 1997 by Ralf Baechle
 *
 * modified for R5900(EE core) by SCEI.
 */
#ifndef	__ASM_MIPS_R5900CACHEOPS_H
#define	__ASM_MIPS_R5900CACHEOPS_H

/*
 * Cache Operations
 */
#define Index_Invalidate_I      0x07
#define Index_Load_Tag_I	0x00
#define Index_Store_Tag_I	0x04
#define Hit_Invalidate_I	0x0b
#define Fill			0x0e

#define Index_Writeback_Inv_D   0x14
#define Index_Load_Tag_D	0x10
#define Index_Store_Tag_D	0x12
#define Hit_Invalidate_D	0x1a
#define Hit_Writeback_Inv_D	0x18
#define Hit_Writeback_D		0x1c

#endif	/* __ASM_MIPS_R5900CACHEOPS_H */
