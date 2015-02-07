/*
 * linux/include/asm-mips/ps2/siflock.h
 *
 *        Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: siflock.h,v 1.5 2001/03/16 05:41:26 takemura Exp $
 */

#ifndef __ASM_PS2_SIFLOCK_H
#define __ASM_PS2_SIFLOCK_H

#define PS2SIF_LOCK_QUEUING	(1<<0)

#define PS2LOCK_CDVD	0
#define PS2LOCK_SOUND	1
#define PS2LOCK_PAD	2
#define PS2LOCK_MC	3
#define PS2LOCK_RTC	4
#define PS2LOCK_POWER	5

typedef struct ps2siflock_queue {
	struct ps2siflock_queue *prev;
	struct ps2siflock_queue *next;
	int (*routine)(void*);
	void *arg;
	char *name;
} ps2sif_lock_queue_t;

typedef struct ps2siflock {
	volatile int locked;
	volatile pid_t owner;
	volatile void *lowlevel_owner;
	volatile int waiting;
	char *ownername;
	struct ps2siflock_queue low_level_waitq;
	struct wait_queue *waitq;
	spinlock_t spinlock;
} ps2sif_lock_t;

ps2sif_lock_t *ps2sif_getlock(int);
void ps2sif_lockinit(ps2sif_lock_t *l);
void ps2sif_lockqueueinit(ps2sif_lock_queue_t *q);
int ps2sif_lock(ps2sif_lock_t *l, char*);
void ps2sif_unlock(ps2sif_lock_t *l);
int ps2sif_lowlevel_lock(ps2sif_lock_t *, ps2sif_lock_queue_t *, int);
void ps2sif_lowlevel_unlock(ps2sif_lock_t *l, ps2sif_lock_queue_t *);
int ps2sif_iswaiting(ps2sif_lock_t *l);

#endif /* __ASM_PS2_SIFLOCK_H */
