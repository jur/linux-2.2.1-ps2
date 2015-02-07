/*
 *  PlayStation 2 CD/DVD driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: cdvd.h,v 1.14.6.1 2001/09/19 10:08:22 takemura Exp $
 */

#ifndef PS2CDVD_H
#define PS2CDVD_H

/*
 * macro defines
 */
#define PS2CDVD_DEBUG
#ifdef PS2CDVD_DEBUG
#define DBG_VERBOSE	(1<< 0)
#define DBG_DIAG	(1<< 1)
#define DBG_READ	(1<< 2)
#define DBG_INFO	(1<< 3)
#define DBG_STATE	(1<< 4)
#define DBG_LOCK	(1<< 5)
#define DBG_DLOCK	(1<< 6)
#define DBG_IOPRPC	(1<< 7)

#define DBG_LOG_LEVEL	KERN_CRIT

#define DPRINT(mask, fmt, args...) \
	if (ps2cdvd_debug & (mask)) printk(DBG_LOG_LEVEL "ps2cdvd: " fmt, ## args)
#define DPRINTK(mask, fmt, args...) \
	if (ps2cdvd_debug & (mask)) printk(fmt, ## args)
#else
#define DPRINT(mask, fmt, args...)
#endif

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))
#define ALIGN(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))

#define SEND_BUSY	0
#define SEND_NOWAIT	1
#define SEND_BLOCK	2

/*
 * types
 */
struct ps2cdvd_tocentry {
	unsigned char addr:4;
	unsigned char ctrl:4;
	unsigned char trackno;
	unsigned char indexno;
	unsigned char rel_msf[3];
	unsigned char zero;
	unsigned char abs_msf[3];
};

/*
 * function prototypes
 */
char* ps2cdvd_geterrorstr(int);
char* ps2cdvd_getdisctypestr(int);
void ps2cdvd_tocdump(char*, struct ps2cdvd_tocentry *);
void ps2cdvd_hexdump(char*, unsigned char *, int);

int ps2cdvd_cleanupiop(void);
void ps2cdvd_intr(void*, int);

int ps2cdvd_send_ready(int);
int ps2cdvd_send_read(u_int, u_int, void *, struct sceCdRMode *);
int ps2cdvd_send_read_dvd(u_int, u_int, void *, struct sceCdRMode *);
int ps2cdvd_send_stop(void);
int ps2cdvd_send_gettoc(u_char *, int);
int ps2cdvd_send_mmode(int media);
int ps2cdvd_send_geterror(void);
int ps2cdvd_send_gettype(void);
int ps2cdvd_send_trayreq(int);

/*
 * variables
 */
extern unsigned long ps2cdvd_debug;
extern ps2sif_lock_t *ps2cdvd_lock;

#endif /* PS2CDVD_H */
