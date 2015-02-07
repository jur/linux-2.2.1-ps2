/*
 * linux/include/asm-mips/ps2/sysconf.h
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sysconf.h,v 1.2.6.1 2001/08/21 06:21:50 takemura Exp $
 */

#ifndef __ASM_PS2_SYSCONF_H
#define __ASM_PS2_SYSCONF_H

struct ps2_sysconf {
    short timezone;
    u_char aspect;
    u_char datenotation;
    u_char language;
    u_char spdif;
    u_char summertime;
    u_char timenotation;
    u_char video;
};

extern struct ps2_sysconf *ps2_sysconf;

#endif /* __ASM_PS2_SYSCONF_H */
