/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: mcdev.c,v 1.10.6.1 2001/09/19 10:08:22 takemura Exp $
 */

#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/ps2/mcio.h>
#include <linux/major.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/siflock.h>
#include "mc.h"
#include "mccall.h"

#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

/*
 * macro defines
 */
#define MIN(a, b)	((a) < (b) ? (a) : (b))

/*
 * block device stuffs
 */
#define MAJOR_NR PS2MC_MAJOR
#define DEVICE_NAME "ps2mc"
#define DEVICE_REQUEST do_ps2mc_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM
#include <linux/blk.h>

/*
 * data types
 */

/*
 * function prototypes
 */
static int ps2mc_devioctl(struct inode *, struct file *, u_int, u_long);
static int ps2mc_devopen(struct inode *, struct file *);
static int ps2mc_devrelease(struct inode *, struct file *);
static ssize_t ps2mc_devread(struct file *, char *, size_t, loff_t *);
static int ps2mc_devcheck(kdev_t);
static void do_ps2mc_request(void);

/*
 * variables
 */
struct file_operations ps2mc_fops = {
	NULL,			/* lseek	*/
	ps2mc_devread,		/* read		*/
	NULL,			/* write	*/
	NULL,			/* readdir	*/
	NULL,			/* poll		*/
	ps2mc_devioctl,		/* ioctl	*/
	NULL,			/* mmap		*/
	ps2mc_devopen,		/* open		*/
	NULL,			/* flush	*/
	ps2mc_devrelease,	/* release	*/
	NULL,			/* fsync	*/
	NULL,			/* fasync	*/
	ps2mc_devcheck,		/* check_media_change	*/
	NULL,			/* revalidate	*/
	NULL,			/* lock		*/
};

int ps2mc_opened[PS2MC_NPORTS][PS2MC_NSLOTS];
int (*ps2mc_blkrw_hook)(int, int, void*, int);

/*
 * function bodies
 */
int
ps2mc_devinit(void)
{
	int res;

	/*
	 * register block device entry
	 */
	if ((res = register_blkdev(PS2MC_MAJOR, "ps2mc", &ps2mc_fops)) < 0) {
		printk(KERN_ERR "Unable to get major %d for PS2 Memory Card\n",
		       PS2MC_MAJOR);
                return -1;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	blk_size[MAJOR_NR] = NULL;
	blksize_size[MAJOR_NR] = NULL;

	return (0);
}

int
ps2mc_devexit(void)
{
	/*
	 * unregister block device entry
	 */
	unregister_blkdev(PS2MC_MAJOR, "ps2mc");

	return (0);
}

static int
ps2mc_devioctl(struct inode *inode, struct file *filp, u_int cmd, u_long arg)
{
	kdev_t devno = inode->i_rdev;
	int portslot = MINOR(devno);
	int n, res, fd;
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	struct ps2mc_cardinfo info;
	struct ps2mc_arg cmdarg;
	char path[PS2MC_NAME_MAX+1];

	switch (cmd) {
	case PS2MC_IOCGETINFO:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=GETINFO\n",
		       port, slot);
		if ((res = ps2mc_getinfo(portslot, &info)) != 0)
			return (res);
		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;

	case PS2MC_IOCFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=FORMAT\n",
		       port, slot);
		ps2mc_dircache_invalidate(portslot);
		return ps2mc_format(portslot);

	case PS2MC_IOCSOFTFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=SOFTFORMAT\n",
		       port, slot);
		if (ps2mc_basedir_len == 0)
			return (0);

		sprintf(path, "/%s", PS2MC_BASEDIR);
		if ((res = ps2mc_delete_all(portslot, path)) != 0 &&
		    res != -ENOENT) {
			return (res);
		}
		if ((res = ps2mc_mkdir(portslot, path)) != 0)
			return (res);
		return (0);

	case PS2MC_IOCUNFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=UNFORMAT\n",
		       port, slot);
		ps2mc_dircache_invalidate(portslot);
		return ps2mc_unformat(portslot);

	case PS2MC_IOCWRITE:
	case PS2MC_IOCREAD:
		ps2mc_dircache_invalidate(portslot);

		/* get arguments */
		if (copy_from_user(&cmdarg, (void *)arg, sizeof(cmdarg)))
			return -EFAULT;
		sprintf(path, "%s%s", ps2mc_basedir_len ? "/" : "",
			PS2MC_BASEDIR);
		n = strlen(path);
		if (PS2MC_NAME_MAX < cmdarg.pathlen + n)
			return -ENAMETOOLONG;
		if (copy_from_user(&path[n], cmdarg.path, cmdarg.pathlen))
			return -EFAULT;
		path[cmdarg.pathlen + n] = '\0';

		DPRINT(DBG_DEV,
		       "device ioctl: card%x%x cmd=%s path=%s pos=%d\n",
		       port, slot, cmd == PS2MC_IOCWRITE ? "WRITE" : "READ",
		       path, cmdarg.pos);

		if ((fd = ps2mc_open(portslot, path, cmdarg.mode)) < 0)
			return (fd);

		if ((res = ps2mc_lseek(fd, cmdarg.pos, 0 /* SEEK_SET */)) < 0)
			goto rw_out;

		res = 0;
		while (0 < cmdarg.count) {
			n = MIN(cmdarg.count, PS2MC_RWBUFSIZE);
			if (cmd == PS2MC_IOCWRITE) {
			    if (copy_from_user(ps2mc_rwbuf, cmdarg.data, n)) {
				res = res ? res : -EFAULT;
				goto rw_out;
			    }
			    if ((n = ps2mc_write(fd, ps2mc_rwbuf, n)) <= 0) {
				res = res ? res : n;
				goto rw_out;
			    }
			} else {
			    if ((n = ps2mc_read(fd, ps2mc_rwbuf, n)) <= 0) {
				res = res ? res : n;
				goto rw_out;
			    }
			    if (copy_to_user(cmdarg.data, ps2mc_rwbuf, n)) {
				res = res ? res : -EFAULT;
				goto rw_out;
			    }
			}
			cmdarg.data += n;
			cmdarg.count -= n;
			res += n;
		}
	rw_out:
		ps2mc_close(fd);
		return (res);

	case PS2MC_IOCNOTIFY:
		ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
		return (0);

	}

	return -EINVAL;
}

static int
ps2mc_devopen(struct inode *inode, struct file *filp)
{
	kdev_t devno = inode->i_rdev;
	int portslot = MINOR(devno);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	DPRINT(DBG_DEV, "device open: card%d%d\n", port, slot);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);

	ps2mc_opened[port][slot]++;
	filp->private_data = (void*)portslot;

	return (0);
}

static int
ps2mc_devrelease(struct inode *inode, struct file *filp)
{
	kdev_t devno = inode->i_rdev;
	int portslot = MINOR(devno);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	DPRINT(DBG_DEV, "device release: card%d%d\n", port, slot);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);

	ps2mc_opened[port][slot]--;

	return (0);
}

static void
do_ps2mc_request(void)
{
	struct request *current_request;
	char *cmd;
	int res;
	kdev_t dev;

repeat:
	INIT_REQUEST;
	current_request=CURRENT;
	CURRENT=current_request->next;
	spin_unlock_irq(&io_request_lock);

	if (current_request->cmd == WRITE) {
		cmd = "write";
	} else
	if (current_request->cmd == READ) {
		cmd = "read";
	} else {
		printk(KERN_ERR "unknown command (%d)?!?",
		       current_request->cmd);
		goto error_out_lock;
	}

	dev = current_request->rq_dev;
	if (ps2mc_opened[PS2MC_PORT(dev)][PS2MC_SLOT(dev)] == 0)
		printk("ps2mc: %s dev=%x sect=%lx\n",
		       cmd, dev, current_request->sector);

	DPRINT(DBG_DEV, "%s sect=%lx, len=%ld, addr=%p\n",
	       cmd, current_request->sector,
	       current_request->current_nr_sectors,
	       current_request->buffer);

	res = -1;
	if (ps2mc_blkrw_hook)
		res = (*ps2mc_blkrw_hook)(current_request->cmd == READ ? 0 : 1,
					  current_request->sector,
					  current_request->buffer,
					  current_request->current_nr_sectors);

	spin_lock_irq(&io_request_lock);
	current_request->next=CURRENT;
	CURRENT=current_request;
	end_request(res == 0 ? 1 : 0);

	goto repeat;

error_out_lock:
	spin_lock_irq(&io_request_lock);
	
//error_out:
	current_request->next=CURRENT;
	CURRENT=current_request;
	end_request(0);
	goto repeat;
}

static ssize_t
ps2mc_devread(struct file *filp, char *buffer, size_t count, loff_t *ppos)
{
	int res, n;
	char zero[256];

	memset(zero, 0, sizeof(zero));

	res = 0;
	while (0 < count) {
		n = MIN(count, sizeof(zero));
		if (copy_to_user(buffer, zero, n))
			return res ? res : -EFAULT;
		buffer += n;
		count -= n;
		res += n;
	}
	return res;
}

static int
ps2mc_devcheck(kdev_t dev)
{
	int portslot = MINOR(dev);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	static int gens[PS2MC_NPORTS][PS2MC_NSLOTS];

return (1);

	if (0 <= port && port < PS2MC_NPORTS &&
	    0 <= slot && slot < PS2MC_NSLOTS &&
	    gens[port][slot] != ps2mc_cardgens[port][slot]) {
		DPRINT(DBG_DEV, "card%d%d was changed\n", port, slot);
		gens[port][slot] = ps2mc_cardgens[port][slot];
		return (1);
	}
	return (0);
}
