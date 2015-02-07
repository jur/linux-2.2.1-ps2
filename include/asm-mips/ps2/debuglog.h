/*
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: debuglog.h,v 1.1 2000/09/25 12:22:34 takemura Exp $
 */
#ifndef PS2DEBUGLOG_H
#define PS2DEBUGLOG_H

struct debuglog {
	int size;
	int count;
	int truncated;
	int head, tail;
	int newline;
	char *buf;
	spinlock_t lock;
};

extern int debuglog_init(struct debuglog*, char* buf, int size);
extern int debuglog_free(struct debuglog*);
extern void debuglog(struct debuglog*, const char * fmt, ...);
extern int debuglog_flush(struct debuglog*);

#endif /* PS2DEBUGLOG_H */
