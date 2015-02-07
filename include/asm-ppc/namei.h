/* $Id: namei.h,v 1.4 1998/12/08 20:52:03 ralf Exp $
 * linux/include/asm-ppc/namei.h
 * Adapted from linux/include/asm-alpha/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __PPC_NAMEI_H
#define __PPC_NAMEI_H

/* This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_lookup_dentry(name, lookup_flags) \
	do {} while (0)

#endif /* __PPC_NAMEI_H */
