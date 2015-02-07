/*
 * ps2_ksyms.c: Export PS2 functions needed for loadable modules.
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: ps2_ksyms.c,v 1.15 2001/03/26 08:33:34 shin Exp $
 */
#include <linux/config.h>
#include <linux/module.h>

unsigned long mktime(unsigned int, unsigned int, unsigned int, unsigned int,
		     unsigned int, unsigned int);
void mkdate(unsigned long, unsigned int *, unsigned int *,
	    unsigned int *, unsigned int *, unsigned int *, unsigned int *);
EXPORT_SYMBOL(mktime);
EXPORT_SYMBOL(mkdate);

extern int ps2_pccard_present;
EXPORT_SYMBOL(ps2_pccard_present);

#ifdef CONFIG_T10000_DEBUG_HOOK
extern void (*ps2_debug_hook[0x80])(int c);
EXPORT_SYMBOL(ps2_debug_hook);
#endif
