/*
 * linux/include/asm-mips/ps2/sound.h
 *
 *        Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sound.h,v 1.2 2001/07/24 08:27:48 takemura Exp $
 */

#ifndef __ASM_PS2_SOUND_H
#define __ASM_PS2_SOUND_H

typedef struct ps2sd_voice_data {
    int spu2_addr;
    int len;
    unsigned char *data;
} ps2sd_voice_data;

#if 0 /* This function isn't implemented yet */
#define PS2SDCTL_VOICE_GET		_IOWR('V', 0, ps2sd_voice_data)
#endif
#define PS2SDCTL_VOICE_PUT		_IOW ('V', 1, ps2sd_voice_data)
#define PS2SDCTL_SET_INTMODE		_IOW ('V', 2, int)
#define PS2SD_INTMODE_NORMAL		0
#define PS2SD_INTMODE_512		1

#endif /* __ASM_PS2_SOUND_H */
