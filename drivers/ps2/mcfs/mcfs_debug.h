/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: mcfs_debug.h,v 1.8 2000/10/12 12:23:57 takemura Exp $
 */
#define PS2MCFS_DEBUG
#ifdef PS2MCFS_DEBUG

extern unsigned long ps2mcfs_debug;

#define DBG_INFO	(1<< 0)
#define DBG_TRACE	(1<< 1)
#define DBG_PATHCACHE	(1<< 2)
#define DBG_FILECACHE	(1<< 3)
#define DBG_DEBUGHOOK	(1<< 4)
#define DBG_READPAGE	(1<< 5)
#define DBG_BLOCKRW	(1<< 6)
#define DBG_LOG		(1<<31)
#define DBG_LOG_LEVEL	KERN_CRIT
/*
#define DEBUGLOG(fmt, args...)		debuglog(NULL, fmt, ## args)
*/
#define DEBUGLOG(fmt, args...)		do {} while (0)
#define DPRINT(mask, fmt, args...) \
	do { \
		if ((ps2mcfs_debug & (mask)) == (mask)) { \
			if (ps2mcfs_debug & DBG_LOG) \
				DEBUGLOG(fmt, ## args); \
			else \
				printk(DBG_LOG_LEVEL "ps2mcfs: " fmt, ## args); \
		} \
	} while (0)
#define DPRINTK(mask, fmt, args...) \
	do { \
		if ((ps2mcfs_debug & (mask)) == (mask)) { \
			if (ps2mcfs_debug & DBG_LOG) \
				DEBUGLOG(fmt, ## args); \
			else \
				printk(fmt, ## args); \
		} \
	} while (0)
#else
#define DPRINT(mask, fmt, args...) do {} while (0)
#define DPRINTK(mask, fmt, args...) do {} while (0)
#endif

#define TRACE(fmt, args...) DPRINT(DBG_TRACE, fmt, ## args)
