/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: filedesc.c,v 1.3 2000/09/26 04:21:11 takemura Exp $
 */
#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/list.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <asm/bitops.h>

#include "mcfs.h"
#include "mcfs_debug.h"

static struct ps2mcfs_filedesc {
	struct ps2mcfs_dirent *dirent;
	struct list_head link;
	int inuse;
	int fd;
	int rwmode;
	int expire_time;
} *items;

static struct list_head lru;
static struct wait_queue *waitq = NULL;
struct semaphore ps2mcfs_fdsem;

void __free_fd(struct ps2mcfs_dirent *);

/*
 * Insert a new entry at tail of the specified list
 */
static __inline__ void list_addtail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

int
ps2mcfs_init_fdcache()
{
	int i;
	int dtabsz = ps2mc_getdtablesize();

#if defined(PS2MCFS_DEBUG) && defined(CONFIG_T10000_DEBUG_HOOK)
	if (ps2mcfs_debug & DBG_DEBUGHOOK) {
		static void dump(void);
		extern void (*ps2_debug_hook[0x80])(int c);
		ps2_debug_hook['D'] = (void(*)(int))dump;
	}
#endif

	sema_init(&ps2mcfs_fdsem, 1);
	items = kmalloc(sizeof(struct ps2mcfs_filedesc) * dtabsz, GFP_KERNEL);
	if (items == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&lru);
	for (i = 0; i < dtabsz; i++) {
		items[i].dirent = NULL;
		items[i].inuse = 0;
		items[i].fd = -1;
		list_add(&items[i].link, &lru);
	}

	return (0);
}

int
ps2mcfs_exit_fdcache()
{
	if (items != NULL)
		kfree(items);
	items = NULL;

#if defined(PS2MCFS_DEBUG) && defined(CONFIG_T10000_DEBUG_HOOK)
	if (ps2mcfs_debug & DBG_DEBUGHOOK) {
		static void dump(void);
		extern void (*ps2_debug_hook[0x80])(int c);
		ps2_debug_hook['D'] = (void(*)(int))NULL;
	}
#endif

	return (0);
}


#ifdef PS2MCFS_DEBUG
static void dump(void)
{
	struct list_head *p;
	const char *path;

	for (p = lru.next; p != &lru; p = p->next) {
		struct ps2mcfs_filedesc *ent;

		ent = list_entry(p, struct ps2mcfs_filedesc, link);
		if (ent->dirent)
			path = ps2mcfs_get_path(ent->dirent);
		else
			path = "-";
		printk(" %d: count=%d '%s' fd=%d\n",
		       ent - items, ent->inuse, path, ent->fd);
		if (ent->dirent)
			ps2mcfs_put_path(ent->dirent, path);
	}
}
#endif

int
ps2mcfs_get_fd(struct ps2mcfs_dirent *dirent, int rwmode)
{
	int res;
	struct ps2mcfs_filedesc *ent;
#ifdef PS2MCFS_DEBUG
	const char *path;

	path = ps2mcfs_get_path(dirent);
	if (*path == '\0')
		return -ENAMETOOLONG; /* path name might be too long */
#endif

 retry:
	down(&ps2mcfs_fdsem);
	ent = dirent->fd;
	if (ent != NULL) {
		if (ent->rwmode == rwmode) {
			list_del(&ent->link);
			list_add(&ent->link, &lru);
			ent->inuse++;
			ent->expire_time = PS2MCFS_FD_EXPIRE_TIME;
			DPRINT(DBG_FILECACHE, "get_fd: %s(count=%d)\n",
			       path, ent->inuse);
			res = ent->fd;
			goto out;
		} else {
			if (ent->inuse) {
				interruptible_sleep_on_timeout(&waitq, HZ);
				if (signal_pending(current)) {
					res = -EINTR;
					goto out;
				}
			}
			if (!ent->inuse)
				__free_fd(dirent);
			up(&ps2mcfs_fdsem);
			goto retry;
		}
	}

	ent = list_entry(lru.prev, struct ps2mcfs_filedesc, link);
	if (ent->inuse) {
#ifdef PS2MCFS_DEBUG
		DPRINT(DBG_FILECACHE, "get_fd(%s): resource not available\n",
		       path);
		dump();
#endif
		interruptible_sleep_on_timeout(&waitq, HZ);
		if (signal_pending(current)) {
			res = -EINTR;
			goto out;
		}
		up(&ps2mcfs_fdsem);
		goto retry;
	}

	if (ent->dirent != NULL)
		__free_fd(ent->dirent);
	list_del(&ent->link);
	list_add(&ent->link, &lru);
	ent->inuse = 1;
	ent->expire_time = PS2MCFS_FD_EXPIRE_TIME;
	dirent->fd = ent;
	ent->dirent = dirent;
	ent->rwmode = rwmode;
	DPRINT(DBG_FILECACHE, "get_fd: ps2mc_open(%s)\n", path);
	ent->fd = ps2mc_open(dirent->root->portslot, path, rwmode);
	res = ent->fd;
	if (ent->fd < 0) {
		__free_fd(dirent);
	}
 out:
	up(&ps2mcfs_fdsem);
	DPRINT(DBG_FILECACHE, "get_fd: fd=%d %s\n", ent->fd, path);
	ps2mcfs_put_path(dirent, path);

	return (res);
}

void
ps2mcfs_put_fd(struct ps2mcfs_dirent *dirent, int fd)
{
	if (dirent->fd == NULL)
		return;

	dirent->fd->inuse--;
	wake_up_interruptible(&waitq);
#ifdef PS2MCFS_DEBUG
	{
		const char *path;

		path = ps2mcfs_get_path(dirent);
		DPRINT(DBG_FILECACHE, "put_fd: %s(count=%d)\n",
		       path, dirent->fd->inuse);
		ps2mcfs_put_path(dirent, path);
	}
#endif
}

void
ps2mcfs_free_fd(struct ps2mcfs_dirent *dirent)
{
	down(&ps2mcfs_fdsem);
	__free_fd(dirent);
	up(&ps2mcfs_fdsem);
}

void
__free_fd(struct ps2mcfs_dirent *dirent)
{
	struct ps2mcfs_filedesc *ent;

	if (dirent->fd == NULL) {
		return;
	}
	ent = dirent->fd;
	dirent->fd = NULL;
#ifdef PS2MCFS_DEBUG
	{
		const char *path;
		
		path = ps2mcfs_get_path(dirent);
		DPRINT(DBG_FILECACHE, "ps2mcfs_free_fd: %s(count=%d, fd=%d)\n",
		       path, ent->inuse, ent->fd);
		if (0 <= ent->fd)
			DPRINT(DBG_FILECACHE,
			       "free_fd: ps2mc_close(%s)\n", path);
		ps2mcfs_put_path(dirent, path);
	}
#endif
	if (0 <= ent->fd)
		ps2mc_close(ent->fd);
	ent->dirent = NULL;
	ent->inuse = 0;
	ent->fd = -1;
	list_del(&ent->link);
	list_addtail(&ent->link, &lru);
	wake_up_interruptible(&waitq);
}

/*
 * ps2mcfs_check_fd() is called from daemon thread (ps2mcfs_thread)
 * periodically.
 */
void
ps2mcfs_check_fd()
{
	struct list_head *p;

	down(&ps2mcfs_fdsem);
	for (p = lru.next; p != &lru; p = p->next) {
		struct ps2mcfs_filedesc *ent;

		ent = list_entry(p, struct ps2mcfs_filedesc, link);
		if (ent->dirent && !ent->inuse) {
			ent->expire_time -= PS2MCFS_CHECK_INTERVAL;
			if (ent->expire_time < 0) {
#ifdef PS2MCFS_DEBUG
				const char *path = NULL;
				path = ps2mcfs_get_path(ent->dirent);
				DPRINT(DBG_FILECACHE,
				       "check_fd: expire '%s'\n", path);
				ps2mcfs_put_path(ent->dirent, path);
#endif
				__free_fd(ent->dirent);
			}

		}
	}
	up(&ps2mcfs_fdsem);
}
