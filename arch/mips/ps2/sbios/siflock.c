/*
 * siflock.c
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: siflock.c,v 1.3 2001/03/26 08:33:35 shin Exp $
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/spinlock.h>

#include <asm/ps2/siflock.h>

/*
 * debug stuff
 */
//#define PS2SIFLOCK_DEBUG
#ifdef PS2SIFLOCK_DEBUG
#define DBG_LOCK	(1<< 0)

#define DBG_LOG_LEVEL	KERN_CRIT

#define DPRINT(mask, fmt, args...) \
	if (ps2siflock_debug & (mask)) printk(DBG_LOG_LEVEL "ps2siflock: " fmt, ## args)
#define DPRINTK(mask, fmt, args...) \
	if (ps2siflock_debug & (mask)) printk(fmt, ## args)
#else
#define DPRINT(mask, fmt, args...)
#define DPRINTK(mask, fmt, args...)
#endif

unsigned long ps2siflock_debug = 0;

MODULE_PARM(ps2siflock_debug, "i");
EXPORT_SYMBOL(ps2sif_lockinit);
EXPORT_SYMBOL(ps2sif_lockqueueinit);
EXPORT_SYMBOL(ps2sif_lock);
EXPORT_SYMBOL(ps2sif_unlock);
EXPORT_SYMBOL(ps2sif_lowlevel_lock);
EXPORT_SYMBOL(ps2sif_lowlevel_unlock);
EXPORT_SYMBOL(ps2sif_iswaiting);
EXPORT_SYMBOL(ps2sif_getlock);

static ps2sif_lock_t lock_cdvd;
static ps2sif_lock_t lock_sound;
static ps2sif_lock_t lock_pad;
static ps2sif_lock_t lock_mc;

static ps2sif_lock_t *locks[] = {
  [PS2LOCK_CDVD]	= &lock_cdvd,
  [PS2LOCK_SOUND]	= &lock_sound,
  [PS2LOCK_PAD]		= &lock_pad,
  [PS2LOCK_MC]		= &lock_mc,
  [PS2LOCK_RTC]		= &lock_cdvd,
  [PS2LOCK_POWER]	= &lock_cdvd,
};

/*
 * utility functions
 */
static inline void
qadd(ps2sif_lock_queue_t *q, ps2sif_lock_queue_t *i)
{
	if (q->prev)
		q->prev->next = i;
	i->prev = q->prev;
	q->prev = i;
	i->next = q;
}

static inline ps2sif_lock_queue_t *
qpop(ps2sif_lock_queue_t *q)
{
	ps2sif_lock_queue_t *i;
	if (q->next == q) {
		return NULL;
	}
	i = q->next;
	q->next = q->next->next;
	q->next->prev = q;
	i->next = NULL; /* failsafe */
	i->prev = NULL; /* failsafe */

	return (i);
}

/*
 * functions
 */
ps2sif_lock_t *
ps2sif_getlock(int lockid)
{
	int i;
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		/* XXX, some items in locks will be initialized twice */
		for (i = 0; i < sizeof(locks)/sizeof(*locks); i++)
			ps2sif_lockinit(locks[i]);
	}

	if (lockid < 0 || sizeof(locks)/sizeof(*locks) <= lockid)
		return (NULL);
	else
		return (locks[lockid]);
}

void
ps2sif_lockinit(ps2sif_lock_t *l)
{
	l->locked = 0;
	l->owner = 0;
	l->lowlevel_owner = NULL;
	l->waitq = NULL;
	l->waiting = 0;
	l->ownername = NULL;
	l->low_level_waitq.prev = &l->low_level_waitq;
	l->low_level_waitq.next = &l->low_level_waitq;
	spin_lock_init(l->spinlock);
}

void
ps2sif_lockqueueinit(ps2sif_lock_queue_t *q)
{
	q->prev = NULL;
	q->next = NULL;
	q->routine = NULL;
}

int
ps2sif_lock(ps2sif_lock_t *l, char *name)
{
	unsigned long flags;

	for ( ; ; ) {
		spin_lock_irqsave(l->spinlock, flags);
		if (!l->locked || l->owner == current->pid) {
			if (l->locked++ == 0) {
				DPRINT(DBG_LOCK, "  LOCK: pid=%ld\n",
				       current->pid);
				l->owner = current->pid;
				l->ownername = name;
			} else {
				DPRINT(DBG_LOCK, "  lock: pid=%ld count=%d\n",
				       current->pid, l->locked);
			}
			spin_unlock_irqrestore(l->spinlock, flags);
			return 0;
		}
		l->waiting++;
		DPRINT(DBG_LOCK, "sleep: pid=%ld\n", current->pid);
		interruptible_sleep_on(&l->waitq);
		DPRINT(DBG_LOCK, "waken up: pid=%ld\n", current->pid);
		l->waiting--;
		spin_unlock_irqrestore(l->spinlock, flags);
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
}

static inline void
clear_lock(ps2sif_lock_t *l)
{
	l->locked = 0;
	l->owner = 0;
	l->ownername = NULL;
	l->lowlevel_owner = NULL;
}

static inline void
give_lowlevel_lock(ps2sif_lock_t *l, ps2sif_lock_queue_t *q)
{
	if (l->locked++ == 0) {
		DPRINT(DBG_LOCK, "  LOCK: qi=%p\n", q);
	} else {
		DPRINT(DBG_LOCK, "  lock: qi=%p  count=%d\n", q, l->locked);
	}
	l->owner = -1;
	l->lowlevel_owner = q;
	l->ownername = q->name;
	if (q->routine != NULL && q->routine(q->arg) == -1)
		if (--l->locked <= 0)
			clear_lock(l);
}

void
ps2sif_unlock(ps2sif_lock_t *l)
{
	unsigned long flags;
	ps2sif_lock_queue_t *qi;

	spin_lock_irqsave(l->spinlock, flags);
	if (l->owner != current->pid) {
		spin_unlock_irqrestore(l->spinlock, flags);
		printk(KERN_CRIT "ps2cdvd: invalid unlock operation\n");
		return;
	}
	if (--l->locked <= 0) {
		clear_lock(l);
		DPRINT(DBG_LOCK, "UNLOCK: pid=%ld\n", current->pid);
		while (!l->locked && (qi = qpop(&l->low_level_waitq))) {
			give_lowlevel_lock(l, qi);
		}
		if (!l->locked && l->waiting)
			wake_up_interruptible(&l->waitq);
	} else {
		DPRINT(DBG_LOCK, "unlock: pid=%ld count=%d\n",
		       current->pid, l->locked);
	}
	spin_unlock_irqrestore(l->spinlock, flags);
}

int
ps2sif_lowlevel_lock(ps2sif_lock_t *l, ps2sif_lock_queue_t *qi, int opt)
{
	int res;
	unsigned long flags;

	if (qi == NULL)
		return (-1);

	spin_lock_irqsave(l->spinlock, flags);
	if (!l->locked || l->lowlevel_owner == qi) {
		give_lowlevel_lock(l, qi);
		res = 0;
	} else {
		if (opt & PS2SIF_LOCK_QUEUING) {
			DPRINT(DBG_LOCK, "enqueue: qi=%p\n", qi);
			qadd(&l->low_level_waitq, qi);
		}
		res = -1;
	}
	spin_unlock_irqrestore(l->spinlock, flags);

	return res;
}

void
ps2sif_lowlevel_unlock(ps2sif_lock_t *l, ps2sif_lock_queue_t *qi)
{
	unsigned long flags;

	if (qi == NULL)
		return;

	spin_lock_irqsave(l->spinlock, flags);
	if (l->locked && l->lowlevel_owner == qi && l->owner == -1) {
		if (--l->locked <= 0) {
			clear_lock(l);
			DPRINT(DBG_LOCK, "UNLOCK: qi=%p\n", qi);
			while (!l->locked &&
			       (qi = qpop(&l->low_level_waitq))) {
				give_lowlevel_lock(l, qi);
			}
			if (!l->locked && l->waiting) {
				DPRINT(DBG_LOCK, "wakeup upper level\n");
				wake_up_interruptible(&l->waitq);
			}
		} else {
			DPRINT(DBG_LOCK, "unlock: qi=%p  count=%d\n",
			       qi, l->locked);
		}
	} else {
		printk(KERN_CRIT
		       "ps2sif_lock: low level locking violation\n");
	}
	spin_unlock_irqrestore(l->spinlock, flags);
}

int
ps2sif_iswaiting(ps2sif_lock_t *l)
{
	return (l->waiting);
}

__initfunc(int ps2lock_init(void))
{
	return (0);
}

#ifdef MODULE
int
init_module(void)
{
	DPRINT(DBG_LOCK, "ps2siflock init\n");
	ps2lock_init(void);
	return (0);
}

void
cleanup_module(void)
{
	DPRINT(DBG_LOCK, "ps2siflock exit\n");
}
#endif /* MODULE */
