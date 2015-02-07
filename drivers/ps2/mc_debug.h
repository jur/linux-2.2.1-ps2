/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: mc_debug.h,v 1.6.6.1 2001/09/19 10:08:22 takemura Exp $
 */

#ifndef PS2MC_DEBUG_H
#define PS2MC_DEBUG_H

#ifdef PS2MC_DEBUG

#define DBG_POLLING	(1<< 0)
#define DBG_INFO	(1<< 1)
#define DBG_DIRCACHE	(1<< 2)
#define DBG_PATHCHECK	(1<< 3)
#define DBG_DEV		(1<< 4)
#define DBG_FILESEM	(1<< 5)
#define DBG_LOG_LEVEL	KERN_CRIT

#define DPRINT(mask, fmt, args...) \
	do { \
		if ((ps2mc_debug & (mask)) == (mask)) \
			printk(DBG_LOG_LEVEL "ps2mc: " fmt, ## args); \
	} while (0)
#define DPRINTK(mask, fmt, args...) \
	do { \
		if ((ps2mc_debug & (mask)) == (mask)) \
			printk(fmt, ## args); \
	} while (0)
#else
#define DPRINT(mask, fmt, args...) do {} while (0)
#define DPRINTK(mask, fmt, args...) do {} while (0)
#endif

#endif /* PS2MC_DEBUG_H */
