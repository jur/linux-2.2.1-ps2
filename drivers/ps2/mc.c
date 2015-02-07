/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: mc.c,v 1.21.6.3 2001/09/21 11:40:42 nakamura Exp $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/ps2/mcio.h>
#include <asm/smplock.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sifutil.h>
#include <asm/ps2/siflock.h>
#include "mc.h"
#include "mccall.h"

#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

/*
 * macro defines
 */
#define ALIGN(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))

/*
 * data types
 */

/*
 * function prototypes
 */
/* mktime and mkdate are defined in arch/mips/ps2/kernel/time.c */
unsigned long mktime(unsigned int, unsigned int, unsigned int, unsigned int,
		     unsigned int, unsigned int);
void mkdate(unsigned long, unsigned int *, unsigned int *, unsigned int *,
	    unsigned int *, unsigned int *, unsigned int *, unsigned int *);

/*
 * variables
 */
int ps2mc_debug = 0;
ps2sif_lock_t ps2mc_lock;	/* the lock which is need to invoke RPC */
static unsigned char *dmabuf = NULL;
McDirEntry *dirbuf;
char *ps2mc_rwbuf;
int ps2mc_basedir_len;
struct semaphore ps2mc_filesem;

/* card status wacthing thread stuff */
static struct semaphore *thread_sem = NULL;
static struct task_struct *thread_task = NULL;
static struct wait_queue *thread_wq = NULL;
static struct list_head listeners;
static int cardstates[PS2MC_NPORTS][PS2MC_NSLOTS];
int ps2mc_cardgens[PS2MC_NPORTS][PS2MC_NSLOTS];

static char *ps2mc_type_names[] = {
[PS2MC_TYPE_EMPTY]		= "empty",
[PS2MC_TYPE_PS1]		= "PS1 memory card",
[PS2MC_TYPE_PS2]		= "PS2 memory card",
[PS2MC_TYPE_POCKETSTATION]	= "Pocket Station",
};

/*
 * export symbols
 */
EXPORT_SYMBOL(ps2mc_add_listener);
EXPORT_SYMBOL(ps2mc_del_listener);
EXPORT_SYMBOL(ps2mc_getinfo);
EXPORT_SYMBOL(ps2mc_readdir);
EXPORT_SYMBOL(ps2mc_getdir);
EXPORT_SYMBOL(ps2mc_setdir);
EXPORT_SYMBOL(ps2mc_mkdir);
EXPORT_SYMBOL(ps2mc_rename);
EXPORT_SYMBOL(ps2mc_delete);
EXPORT_SYMBOL(ps2mc_getdtablesize);
EXPORT_SYMBOL(ps2mc_close);
EXPORT_SYMBOL(ps2mc_lseek);
EXPORT_SYMBOL(ps2mc_open);
EXPORT_SYMBOL(ps2mc_read);
EXPORT_SYMBOL(ps2mc_write);
EXPORT_SYMBOL(ps2mc_checkdev);
EXPORT_SYMBOL(ps2mc_blkrw_hook);

MODULE_PARM(ps2mc_debug, "i");

/*
 * function bodies
 */
char*
ps2mc_terminate_name(const char *name, int namelen)
{
	static char buf[PS2MC_NAME_MAX+1];
	memcpy(buf, name, MIN(namelen, PS2MC_NAME_MAX));
	buf[MIN(namelen, PS2MC_NAME_MAX)] = '\0';
	return (buf);
}

/*
 * format memory card
 */
int
ps2mc_format(int portslot)
{
	int res, result;

	if ((res = ps2sif_lock(&ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	res = ps2mclib_Format(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), &result);
	if (res != 0 || result != 0) {
		/* error */
		printk("ps2mclib_Format() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "format(): card%d%d result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

out:
	ps2sif_unlock(&ps2mc_lock);

	ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);

	return (res);
}

/*
 * unformat memory card
 */
int
ps2mc_unformat(int portslot)
{
	int res, result;

	if ((res = ps2sif_lock(&ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	res = ps2mclib_Unformat(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), &result);
	if (res != 0 || result != 0) {
		/* error */
		printk("ps2mclib_Unformat() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "unformat(): card%d%d result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

out:
	ps2sif_unlock(&ps2mc_lock);

	ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);

	return (res);
}

/*
 * get memory card info
 */
int
ps2mc_getinfo(int portslot, struct ps2mc_cardinfo *info)
{
	int res;
	int result, type, free, format;

	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);

	res = ps2mc_getinfo_sub(portslot, &result, &type, &free, &format);
	if (res < 0)
		return (res);

	memset(info, 0, sizeof(struct ps2mc_cardinfo));
	info->type = PS2MC_TYPE_EMPTY;

	switch (result) {
	case 0:
	case -1:
	case -2:
		/* succeeded normaly */
		break;
	default:
		return (0);
	}

	info->type = type;
	info->busy = ps2mc_opened[port][slot];
	if (type == PS2MC_TYPE_PS2) {
		info->blocksize = 1024;		/* XXX, I donna */
		info->totalblocks = 1024 * 8;	/* XXX, 8MB */
		info->freeblocks = free;
		info->formatted = format;
		info->generation = ps2mc_cardgens[port][slot];
	}

	return (0);
}

int
ps2mc_checkdev(kdev_t dev)
{
	int portslot = MINOR(dev);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	if (MAJOR(dev) != PS2MC_MAJOR)
		return (-ENODEV);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);
	return (0);
}

int
ps2mc_getinfo_sub(int portslot, int *result, int *type, int *free, int *format)
{
	int res;
	static int formatted[PS2MC_NPORTS][PS2MC_NSLOTS];
	int port = PS2MC_PORT(portslot), slot = PS2MC_SLOT(portslot);

	if ((res = ps2sif_lock(&ps2mc_lock, "mc get info")) < 0) {
		return (res);
	}

	res = 0;
	if (ps2mclib_GetInfo(port, slot, type, free, format, result) != 0) {
		/* error */
		printk("ps2mclib_GetInfo() failed\n");
		res = -EIO;
	}

	DPRINT(DBG_POLLING,
	       "getinfo(): card%d%d result=%d type=%d format=%d\n",
	       port, slot,
	       result != NULL ? *result : 0,
	       type != NULL ? *type : 0,
	       format != NULL ? *format : 0);

	ps2sif_unlock(&ps2mc_lock);

	if (res == 0 && (*result == -1 || *result == -2)) {
		/* card was replaced */
		ps2mc_cardgens[port][slot]++;

		/* XXX, save if the card is formated or not */
		if (*result == -1)
			formatted[port][slot] = 1;
		else
			formatted[port][slot] = 0;
	}

	if (format != NULL)
		*format = formatted[port][slot];

	return (res);
}

/*
 * make directory
 */
int
ps2mc_mkdir(int portslot, const char *path)
{
	int res, result;

	/*
	 * XXX, recent PS2 Runtime library does not allow to
	 * make a subdirectory.
	 */
	if (ps2mc_basedir_len != 0 && path[ps2mc_basedir_len + 1] != '\0')
		return (-EPERM);

	if ((res = ps2sif_lock(&ps2mc_lock, "mc mkdir")) < 0) {
		return (res);
	}

	/* invalidate directory cache */
	ps2mc_dircache_invalidate(portslot);

	res = 0;
	if (ps2mclib_Mkdir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			   (char*)path, &result) != 0) {
		/* error */
		printk("ps2mclib_Mkdir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "mkdir(%s): card%d%d result=%d\n",
	       path, PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

	switch (result) {
	case 0:		res = 0;	break;
	case -2:	res = -EINVAL;	break;	/* not formatted */
	case -3:	res = -ENOSPC;	break;	/* no space left on device */
	case -4:	res = -ENOENT;	break;	/* no such file or directory */
	default:	res = -EIO;	break;
	}

 out:
	ps2sif_unlock(&ps2mc_lock);

	return (res);
}


/*
 * rename directory entry
 */
int
ps2mc_rename(int portslot, const char *path, char *newname)
{
	int res, result;

	if ((res = ps2sif_lock(&ps2mc_lock, "mc rename")) < 0) {
		return (res);
	}

	res = 0;
	if (ps2mclib_Rename(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), (char*)path, newname, &result) != 0) {
		/* error */
		printk("ps2mclib_Rename() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "rename(): result=%d\n", result);

	switch (result) {
	case 0: /* succeeded */
		res = 0;
		ps2mc_dircache_invalidate(portslot);
		break;
	case -4: /* File not found */
		res = -ENOENT;
		break;
	default:
		res = -EIO;
		break;
	}

 out:
	ps2sif_unlock(&ps2mc_lock);

	return (res);
}

/*
 * delete directory or file
 */
int
ps2mc_delete(int portslot, const char *path)
{
	int res, result;

	if ((res = ps2sif_lock(&ps2mc_lock, "mc delete")) < 0) {
		return (res);
	}

	/* invalidate directory cache */
	ps2mc_dircache_invalidate(portslot);

	res = 0;
	if (ps2mclib_Delete(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			    (char*)path, &result) != 0) {
		/* error */
		printk("ps2mclib_Delete() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "delete(%s): card%d%d result=%d\n",
	       path, PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

	switch (result) {
	case 0:		res = 0;	break;
	case -2:	res = -EINVAL;	break;	/* not formatted */
	case -4:	res = -ENOENT;	break;	/* no such file or directory */
	case -5:	res = -EBUSY;	break;	/* device or resource busy */
	case -6:	res = -ENOTEMPTY;break;	/* directory not empty */
	default:	res = -EIO;	break;
	}

 out:
	ps2sif_unlock(&ps2mc_lock);

	return (res);
}

/*
 * delete directory and all files which belong to the directory
 */
int
ps2mc_delete_all(int portslot, const char *path)
{
	struct ps2mc_dirent dirent;
	int res;
	static char path2[PS2MC_NAME_MAX+1];

	DPRINT(DBG_INFO, "delete all(): card%d%d %s\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path);

	res = ps2mc_getdir_sub(portslot, path, 0, 1, &dirent);
	if (res < 0)
		return (res);
	if (res == 0)
		return (-ENOENT);

	if (S_ISDIR(dirent.mode)) {
		while (0 < ps2mc_readdir(portslot, path, 2, &dirent, 1)) {
		    sprintf(path2, "%s/%s", path,
			    ps2mc_terminate_name(dirent.name, dirent.namelen));
		    if ((res = ps2mc_delete(portslot, path2)) != 0)
			return (res);
		}
	}

	return ps2mc_delete(portslot, path);
}

int
ps2mc_getdir(int portslot, const char *path, struct ps2mc_dirent *buf)
{
	int count, res, entspace;

	DPRINT(DBG_INFO, "getdir(): card%d%d %s\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path);

	if (strcmp(path, "/") == 0) {
		/*
		 * PS2 memory card has no entry of root directry itself.
		 */
		buf->name[0] = '/';
		buf->namelen = 1;
		buf->mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUGO; /* 0777 */
		buf->mtime = CURRENT_TIME;
		buf->ctime = CURRENT_TIME;
	} else {
		if ((res = ps2mc_getdir_sub(portslot, path, 0, 1, buf)) <= 0)
			return (res);
	}

	if (S_ISDIR(buf->mode)) {
		count = 0;
		for ( ; ; ) {
			struct ps2mc_dirent tmpbuf;
			res = ps2mc_readdir(portslot, path, count, &tmpbuf, 1);
			if (res < 0)
				return (res); /* error */ 
			if (res == 0)
				break; /* no more entries */
			/* read an entry successfully */
			count++;
		}
		if ((res = ps2sif_lock(&ps2mc_lock, "mc getentspace")) < 0) {
			return (res);
		}
		res = ps2mclib_GetEntSpace(PS2MC_PORT(portslot),
					   PS2MC_SLOT(portslot),
					   (char*)path, &entspace);
		ps2sif_unlock(&ps2mc_lock);
		if (res < 0)
			return -EIO;
		count += entspace;
		buf->size = ALIGN(count, 2) * 512;
	}

	return (1); /* succeeded */
}

/*
 * get directory infomation
 *
 * return value:
 *   < 0 error
 *   0   no more entries
 *   0 < succeeded
 *       if return value equals maxent, there might be more entries.
 */
int
ps2mc_getdir_sub(int portslot, const char *path, int mode, int maxent,
		 struct ps2mc_dirent *buf)
{
	int i, res, result;

	if ((res = ps2sif_lock(&ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	res = 0;
	if (ps2mclib_GetDir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			    (char*)path, mode, maxent, dirbuf, &result) != 0) {
		/* error */
		printk("ps2mclib_GetDir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_DIRCACHE, "getdir_sub(): card%d%d %s result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path, result);
	if (result < 0) {
		res = -EIO;
		goto out;
	}
	res = result;

	/*
	 * convert from ps2mclib_TblGetDir into ps2mc_dirent
	 */
	for (i = 0; i < res; i++) {
		/* name */
		memcpy(buf[i].name, dirbuf[i].EntryName, sizeof(buf[i].name));
		buf[i].namelen = MIN(sizeof(buf[i].name),
				     strlen(dirbuf[i].EntryName));

		/* mode */
		if (dirbuf[i].AttrFile & McFileAttrSubdir)
			buf[i].mode = S_IFDIR;
		else
			buf[i].mode = S_IFREG;
		if (dirbuf[i].AttrFile & McFileAttrReadable)
			buf[i].mode |= S_IRUGO;
		if (dirbuf[i].AttrFile & McFileAttrWriteable)
			buf[i].mode |= S_IWUGO;
		if (dirbuf[i].AttrFile & McFileAttrExecutable)
			buf[i].mode |= S_IXUGO;

		/* size */
		buf[i].size = dirbuf[i].FileSizeByte;

		/* create time */
		buf[i].ctime = mktime(dirbuf[i]._Create.Year,
				      dirbuf[i]._Create.Month,
				      dirbuf[i]._Create.Day,
				      dirbuf[i]._Create.Hour,
				      dirbuf[i]._Create.Min,
				      dirbuf[i]._Create.Sec) - McTZONE;

		/* modify time */
		buf[i].mtime = mktime(dirbuf[i]._Modify.Year,
				      dirbuf[i]._Modify.Month,
				      dirbuf[i]._Modify.Day,
				      dirbuf[i]._Modify.Hour,
				      dirbuf[i]._Modify.Min,
				      dirbuf[i]._Modify.Sec) - McTZONE;
	}

	for (i = 0; i < res; i++) {
	    DPRINT(DBG_DIRCACHE, "%3d: %04lx %ld %ld %ld %s\n", i,
		   buf[i].mode,
		   buf[i].ctime,
		   buf[i].mtime,
		   buf[i].size,
		   ps2mc_terminate_name(buf[i].name, buf[i].namelen));
	}

 out:
	ps2sif_unlock(&ps2mc_lock);

	return (res);
}

int
ps2mc_setdir(int portslot, const char *path, int flags,
	     struct ps2mc_dirent *buf)
{
	int res, result;
	unsigned valid = 0;

	if (strcmp(path, "/") == 0) {
		/*
		 * PS2 memory card has no entry of root directry itself.
		 * Just ignore.
		 */
		return (0);
	}

	if ((res = ps2sif_lock(&ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	if (flags & PS2MC_SETDIR_MODE) {
		int rwx;

		/*
		 * first, you should retrieve current mode of the entry.
		 */
		res = ps2mclib_GetDir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
				  (char*)path, 0, 1, dirbuf, &result);
		if (res != 0 || result < 0) {
			/* error */
			printk("setdir: ps2mclib_GetDir() failed\n");
			res = -EIO;
			goto out;
		}

		/* fix the mode bits in buf-> */
		rwx = (buf->mode & 0700) >> 6;
		buf->mode = (buf->mode & ~0777) |
				(rwx << 6) | (rwx << 3) | (rwx << 0);
		buf->mode &= (S_IFDIR | S_IFREG | S_IRUGO | S_IWUGO | S_IXUGO);

		dirbuf[0].AttrFile &= ~(McFileAttrReadable |
					McFileAttrWriteable |
					McFileAttrExecutable);
		if (buf->mode & S_IRUSR)
			dirbuf->AttrFile |= McFileAttrReadable;
		if (buf->mode & S_IWUSR)
			dirbuf->AttrFile |= McFileAttrWriteable;
		if (buf->mode & S_IXUSR)
			dirbuf->AttrFile |= McFileAttrExecutable;
		valid |= McFileInfoAttr;
	}

	if (flags & PS2MC_SETDIR_CTIME) { /* create time */
		unsigned int year, mon, day, hour, min, sec;
		mkdate(buf->ctime + McTZONE,
		       &year, &mon, &day, &hour, &min, &sec, NULL);
		dirbuf[0]._Create.Year = year;
		dirbuf[0]._Create.Month = mon;
		dirbuf[0]._Create.Day = day;
		dirbuf[0]._Create.Hour = hour;
		dirbuf[0]._Create.Min = min;
		dirbuf[0]._Create.Sec = sec;
		valid |= McFileInfoCreate;
	}

	if (flags & PS2MC_SETDIR_MTIME) { /* modify time */
		unsigned int year, mon, day, hour, min, sec;
		mkdate(buf->mtime + McTZONE,
		       &year, &mon, &day, &hour, &min, &sec, NULL);
		dirbuf[0]._Modify.Year = year;
		dirbuf[0]._Modify.Month = mon;
		dirbuf[0]._Modify.Day = day;
		dirbuf[0]._Modify.Hour = hour;
		dirbuf[0]._Modify.Min = min;
		dirbuf[0]._Modify.Sec = sec;
		valid |= McFileInfoModify;
	}

	res = ps2mclib_SetFileInfo(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			       (char*)path, (char*)dirbuf, valid, &result);
	if (res != 0 || result < 0) {
		/* error */
		printk("ps2mclib_SetDir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_DIRCACHE, "setdir(): card%d%d %s result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path, result);

 out:
	ps2sif_unlock(&ps2mc_lock);

	return (res);
}

void
ps2mc_add_listener(struct ps2mc_listener *listener)
{
	while (ps2sif_lock(&ps2mc_lock, "ps2mc add listener") < 0) ;
	list_add(&listener->link, &listeners);
	ps2sif_unlock(&ps2mc_lock);
}

void
ps2mc_del_listener(struct ps2mc_listener *listener)
{
	while (ps2sif_lock(&ps2mc_lock, "ps2mc del listener") < 0) ;
	list_del(&listener->link);
	ps2sif_unlock(&ps2mc_lock);
}

void
ps2mc_set_state(int portslot, int state)
{
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	struct list_head *p;

	if (cardstates[port][slot] != state) {
		DPRINT(DBG_INFO, "card%d%d: %s -> %s\n",
		       port, slot,
		       ps2mc_type_names[cardstates[port][slot]],
		       ps2mc_type_names[state]);

		ps2mc_dircache_invalidate(PS2MC_PORTSLOT(port, slot));

		/*
		 * notify all listeners
		 */
		while (ps2sif_lock(&ps2mc_lock, "ps2mc del listener") < 0) ;
		for (p = listeners.next; p != &listeners; p = p->next) {
			struct ps2mc_listener *listener;
			listener = list_entry(p, struct ps2mc_listener,link);
			if (listener->func != NULL)
				(*listener->func)(listener->ctx,
						  PS2MC_PORTSLOT(port, slot),
						  cardstates[port][slot],
						  state);
		}
		cardstates[port][slot] = state;
		ps2sif_unlock(&ps2mc_lock);
	}
}

void
ps2mc_check(void)
{
	int res;
	int port, slot;
	int result, type;
	static int gens[PS2MC_NPORTS][PS2MC_NSLOTS];

	for (port = 0; port < PS2MC_NPORTS; port++) {
	  for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
	    int portslot = PS2MC_PORTSLOT(port, slot);
	    res = ps2mc_getinfo_sub(portslot, &result, &type, NULL, NULL);
	    if (res < 0 || result < -2) {
	      /* error */
	      ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
	    } else {
	      if (gens[port][slot] != ps2mc_cardgens[port][slot]) {
		ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
		gens[port][slot] = ps2mc_cardgens[port][slot];
		invalidate_buffers(MKDEV(PS2MC_MAJOR, portslot));
	      }

	      if (result == 0 || result == -1)
		      ps2mc_set_state(portslot, type);
	    }
	  }
	}
}

int
ps2mc_thread(void *arg)
{

	DPRINT(DBG_INFO, "start thread\n");

	lock_kernel();

	/*
	 * If we were started as result of loading a module, close all of the
	 * user space pages.  We don't need them, and if we didn't close them
	 * they would be locked into memory.
	 */
	exit_mm(current);

	current->session = 1;
	current->pgrp = 1;
        /*
         * FIXME(eric) this is still a child process of the one that did the insmod.
         * This needs to be attached to task[0] instead.
         */

	siginitsetinv(&current->blocked,
		      sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM));
        current->fs->umask = 0;

	/*
	 * Set the name of this process.
	 */
	sprintf(current->comm, "ps2mc");
        
	unlock_kernel();

	thread_task = current;
	if (thread_sem != NULL)
		up(thread_sem); /* notify that we are ready */

	/*
	 * loop
	 */
	while(1) {
		ps2mc_check();

		interruptible_sleep_on_timeout(&thread_wq, PS2MC_CHECK_INTERVAL);

		if (signal_pending(current) )
			break;
	}

	DPRINT(DBG_INFO, "exit thread\n");

	thread_task = NULL;
	if (thread_sem != NULL)
		up(thread_sem); /* notify that we've exited */

	return (0);
}

int
ps2mc_init(void)
{
	ps2mc_basedir_len = strlen(PS2MC_BASEDIR);
	sema_init(&ps2mc_filesem, ps2mc_getdtablesize());

	/*
	 * allocate DMA buffers
	 */
	if ((dmabuf = kmalloc(1500 + PS2MC_RWBUFSIZE, GFP_KERNEL)) == NULL) {
		return (-ENOMEM);
	}
	PS2SIF_ALLOC_BEGIN(dmabuf, 1500 + PS2MC_RWBUFSIZE);
	PS2SIF_ALLOC(dirbuf, sizeof(McDirEntry) * PS2MC_DIRCACHESIZE, 64);
	PS2SIF_ALLOC(ps2mc_rwbuf, PS2MC_RWBUFSIZE, 64);
	PS2SIF_ALLOC_END("memory card\n");

	/*
	 * initialize lock
	 */
	ps2sif_lockinit(&ps2mc_lock);

	/*
	 * initialize event lister list
	 */
	INIT_LIST_HEAD(&listeners);

	/*
	 * initialize IOP access library
	 */
	if (ps2mclib_Init() < 0) {
		printk(KERN_CRIT "ps2mc: can't initialize memory card system\n");
		return -1;
	}

	/*
	 * register block device entry
	 */
	ps2mc_devinit();

	/*
	 * create and start thread
	 */
	{
            struct semaphore sem = MUTEX_LOCKED;
            
            thread_sem = &sem;
	    kernel_thread(ps2mc_thread, NULL, 0);

	    /* wait the thread ready */
            down(&sem);
            thread_sem = NULL;
	}
                
	return (0);
}

int
ps2mc_cleanup(void)
{

	/*
	 * stop the thread
	 */
	if (thread_task != NULL) {
            struct semaphore sem = MUTEX_LOCKED;
            
            thread_sem = &sem;
            send_sig(SIGKILL, thread_task, 1);

	    /* wait the thread exit */
            down(&sem);
            thread_sem = NULL;
	}

	/*
	 * unregister block device entry
	 */
	ps2mc_devexit();

//	ps2mclib_Exit();

	/* free DMA buffer */
	if (dmabuf != NULL) {
		kfree(dmabuf);
		dmabuf = NULL;
	}

	return (0);
}

#ifdef MODULE
int
init_module(void)
{
	DPRINT(DBG_INFO, "load\n");
	return ps2mc_init();
}

void
cleanup_module(void)
{
	DPRINT(DBG_INFO, "unload\n");
	ps2mc_cleanup();
}
#endif /* MODULE */
