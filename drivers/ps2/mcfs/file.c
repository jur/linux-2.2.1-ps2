/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: file.c,v 1.15 2000/10/12 12:23:57 takemura Exp $
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
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <asm/bitops.h>

#include "mcfs.h"
#include "mcfs_debug.h"

//#define PS2MCFS_USE_SEM
#define PS2MCFS_FILEBUFSIZE	1024
#define ALIGN(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

#ifdef PS2MCFS_USE_SEM
struct semaphore ps2mcfs_filesem;
#endif
char *ps2mcfs_filebuf;
void *dmabuf;

static ssize_t ps2mcfs_read(struct file *, char *, size_t, loff_t *);
static ssize_t ps2mcfs_write(struct file *, const char *, size_t, loff_t *);
static void ps2mcfs_truncate(struct inode *);
static int ps2mcfs_open(struct inode *, struct file *);
static int ps2mcfs_release(struct inode *, struct file *);
static int ps2mcfs_readpage(struct file *, struct page *);
static int ps2mcfs_bmap(struct inode *, int);

static struct file_operations ps2mcfs_file_operations = {
	NULL,			/* lseek	*/
	ps2mcfs_read,		/* read		*/
	ps2mcfs_write,		/* write	*/
	NULL,			/* readdir	*/
	NULL,			/* poll		*/
	NULL,			/* ioctl	*/
	generic_file_mmap,	/* mmap		*/
	ps2mcfs_open,		/* open		*/
	NULL,			/* flush	*/
	ps2mcfs_release,	/* release	*/
	NULL			/* fsync	*/
};

struct inode_operations ps2mcfs_file_inode_operations = {
	&ps2mcfs_file_operations,	/* file-ops	*/
	NULL,			/* create	*/
	NULL,			/* lookup	*/
	NULL,			/* link		*/
	NULL,			/* unlink	*/
	NULL,			/* symlink	*/
	NULL,			/* mkdir	*/
	NULL,			/* rmdir	*/
	NULL,			/* mknod	*/
	NULL,			/* rename	*/
	NULL,			/* readlink	*/
	NULL,			/* follow_link	*/
	ps2mcfs_readpage,	/* readpage	*/
	NULL,			/* writepage	*/
	ps2mcfs_bmap,		/* bmap		*/
	ps2mcfs_truncate,	/* truncate	*/
	NULL			/* permission	*/
};

int
ps2mcfs_init_filebuf()
{
	int dtabsz = ps2mc_getdtablesize();

	TRACE("ps2mcfs_init_filebuf()\n");

#if 0
	dtabsz = 1; /* XXX */
#endif

#ifdef PS2MCFS_USE_SEM
	sema_init(&ps2mcfs_filesem, dtabsz);
#endif

	dmabuf = kmalloc(PS2MCFS_FILEBUFSIZE * dtabsz + 64, GFP_KERNEL);
	if (dmabuf == NULL)
		return -ENOMEM;
	ps2mcfs_filebuf = ALIGN(dmabuf, 64);

	return (0);
}

int
ps2mcfs_exit_filebuf()
{

	TRACE("ps2mcfs_exit_filebuf()\n");
	if (dmabuf != NULL)
		kfree(dmabuf);
	dmabuf = NULL;

	return (0);
}

#define READ_MODE	0
#define WRITE_MODE	1
#define USER_COPY	2

static ssize_t
ps2mcfs_rw(struct inode *inode, char *buf, size_t nbytes, loff_t *ppos, int mode)
{
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	int fd = -1;
	int res, nreads, pos;
	char *filebuf;

	if (de->flags & PS2MCFS_DIRENT_INVALID)
		return -EIO;

#ifdef PS2MCFS_USE_SEM
	down(&ps2mcfs_filesem);
#endif

	/*
	 * get full-path-name and open file
	 */
	res = ps2mcfs_get_fd(de, (mode & WRITE_MODE) ? O_WRONLY : O_RDONLY);
	if (res < 0)
		goto out;
	fd = res;

	/*
	 * XXX, It asummes that (0 <= fd) and (fd < descriptor table size)
	 */
	filebuf = &ps2mcfs_filebuf[PS2MCFS_FILEBUFSIZE * fd];

	/*
	 * seek
	 */
	if (mode & WRITE_MODE) {
		if (inode->i_size < *ppos)
			pos = inode->i_size;
		else
			pos = *ppos;
	} else {
			pos = *ppos;
	}
	if ((res = ps2mc_lseek(fd, pos, 0 /* SEEK_SET */)) < 0)
		goto out;
	if (res != pos) {
		res = -EIO;
		goto out;
	}
	if ((mode & WRITE_MODE) && inode->i_size < *ppos) {
		int pad = *ppos - inode->i_size;
		memset(filebuf, 0, MIN(pad, PS2MCFS_FILEBUFSIZE));
		while (0 < pad) {
			int n = MIN(pad, PS2MCFS_FILEBUFSIZE);
			res = ps2mc_write(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				goto out;
			pad -= res;
			inode->i_size += res;
		}
	}

	/*
	 * read/write
	 */
	nreads = 0;
	res = 0;
	while (0 < nbytes) {
		int n = MIN(nbytes, PS2MCFS_FILEBUFSIZE);
		if (mode & WRITE_MODE) {
			/* write */
			if (mode & USER_COPY) {
				if (copy_from_user(filebuf, buf, n)) {
					res = -EFAULT;
					goto out;
				}
			} else {
				memcpy(filebuf, buf, n);
			}
			res = ps2mc_write(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				break;
		} else {
			/* read */
			res = ps2mc_read(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				break;
			if (mode & USER_COPY) {
				if (copy_to_user(buf, filebuf, res)) {
					res = -EFAULT;
					goto out;
				}
			} else {
				memcpy(buf, filebuf, res);
			}
		}
		nreads += res;
		buf += res;
		nbytes -= res;
		*ppos += res;
	}
	res = (nreads == 0) ? res : nreads;

 out:
	if (0 <= fd)
		ps2mcfs_put_fd(de, fd);
#ifdef PS2MCFS_USE_SEM
	up(&ps2mcfs_filesem);
#endif

	TRACE("ps2mcfs_rw(): res=%d\n", res);

	return (res);
}

int
ps2mcfs_create(struct ps2mcfs_dirent *de)
{
	int res;
	const char *path;

	path = ps2mcfs_get_path(de);
	TRACE("ps2mcfs_create(%s)\n", path);
	ps2mcfs_put_path(de, path);

#ifdef PS2MCFS_USE_SEM
	down(&ps2mcfs_filesem);
#endif
	if (0 <= (res = ps2mcfs_get_fd(de, O_RDWR | O_CREAT))) {
		ps2mcfs_put_fd(de, res);
		res = 0; /* succeeded */
	}
#ifdef PS2MCFS_USE_SEM
	up(&ps2mcfs_filesem);
#endif

	return (res);
}

static ssize_t
ps2mcfs_read(struct file *filp, char * buf, size_t count, loff_t *ppos)
{
	TRACE("ps2mcfs_read(filp=%p, pos=%ld)\n", filp, (long)*ppos);

	return ps2mcfs_rw(filp->f_dentry->d_inode, buf,
			  count, ppos, READ_MODE|USER_COPY);
}

static ssize_t
ps2mcfs_write(struct file *filp, const char * buf, size_t count, loff_t *ppos)
{
	int res;
	struct inode *inode = filp->f_dentry->d_inode;

	TRACE("ps2mcfs_write(filp=%p, pos=%ld)\n", filp, (long)*ppos);

	res = ps2mcfs_rw(filp->f_dentry->d_inode, (char*)buf,
			 count, ppos, WRITE_MODE|USER_COPY);
	if (res < 0)
		return (res);

	if (inode->i_size < *ppos)
		inode->i_size = *ppos; /* file size was extended */
	inode->i_mtime = CURRENT_TIME;

	return (res);
}

static int
ps2mcfs_open(struct inode *inode, struct file *filp)
{
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	const char *path;

	path = ps2mcfs_get_path(de);

	TRACE("ps2mcfs_open(%path)\n", path);

	if (*path == '\0')
		return -ENAMETOOLONG; /* path name might be too long */

	ps2mcfs_put_path(de, path);

	return (0);
}

static int
ps2mcfs_release(struct inode *inode, struct file *filp)
{
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	const char *path;

	path = ps2mcfs_get_path(de);

	TRACE("ps2mcfs_release(%path)\n", path);

	ps2mcfs_free_fd(de);
	ps2mcfs_put_path(de, path);

	return (0);
}

static int
ps2mcfs_readpage(struct file *filp, struct page *page)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	int res;
	loff_t pos;

	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);
#ifdef PS2MCFS_DEBUG
	{
		const char *path;
		path = ps2mcfs_get_path(de);
		DPRINT(DBG_READPAGE, "ps2mcfs_readpage(%s, %lx)\n",
		       path, page->offset);
		ps2mcfs_put_path(de, path);
	}
#endif

	pos = page->offset;
	res = ps2mcfs_rw(filp->f_dentry->d_inode, (void*)page_address(page),
			 PAGE_SIZE, &pos, READ_MODE);
	if (res < 0)
		set_bit(PG_error, &page->flags);
	if (0 <= res && res != PAGE_SIZE)
		memset((void*)(page_address(page) + res), 0, PAGE_SIZE - res);

	set_bit(PG_uptodate, &page->flags);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	__free_page(page);

	return (res < 0) ? res : 0;
}

static int
ps2mcfs_bmap(struct inode *inode, int block)
{
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	int block_shift;
	int sector;

	block_shift = de->root->block_shift;
	if ((1 << PS2MCFS_SECTOR_BITS) <= (block << block_shift)) {
		printk("ps2mcfs_bmap(): block number is too big\n");
		return (-1);
	}

	if (de->flags & PS2MCFS_DIRENT_INVALID)
		return (-1);

	sector = ((de->no << PS2MCFS_SECTOR_BITS) >> block_shift) | block;
	if (!(de->flags & PS2MCFS_DIRENT_BMAPPED)) {
		ps2mcfs_ref_dirent(de);
		de->flags |= PS2MCFS_DIRENT_BMAPPED;
	}

#ifdef PS2MCFS_DEBUG
	{
	const char *path;
	path = ps2mcfs_get_path(de);
	DPRINT(DBG_BLOCKRW, "ps2mcfs_bmap(%s): block=%x -> %x\n",
	       path, block, sector);
	ps2mcfs_put_path(de, path);
	}
#endif

	return (sector);
}

int
ps2mcfs_blkrw(int rw, int sector, void *buffer, int nsectors)
{
	struct ps2mcfs_dirent *de;
	int dno, res;
	loff_t pos;

	dno = ((sector >> PS2MCFS_DIRENT_SHIFT) & PS2MCFS_DIRENT_MASK);
	sector = ((sector >> PS2MCFS_SECTOR_SHIFT) & PS2MCFS_SECTOR_MASK);
        DPRINT(DBG_BLOCKRW, "ps2mcfs: %s dirent=%d sect=%x, len=%d, addr=%p\n",
	       rw ? "write" : "read", dno, sector, nsectors, buffer);

	if ((de = ps2mcfs_find_dirent_no(dno)) == NULL ||
	    de->inode == NULL)
		return (-ENOENT);

	if (de->inode->i_size < (sector + nsectors) * 512)
		return (-EIO);

	pos = sector * 512;
	res = ps2mcfs_rw(de->inode, buffer, nsectors * 512, &pos,
			 rw ? WRITE_MODE : READ_MODE);

	return (res == nsectors * 512 ? 0 : -EIO);
}

static void
ps2mcfs_truncate(struct inode *inode)
{
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	const char *path;
	struct ps2mc_dirent mcdirent;

	path = ps2mcfs_get_path(de);
	if (*path == '\0')
		return; /* path name might be too long */

	TRACE("ps2mcfs_truncate(%s): size=%ld\n", path, inode->i_size);

	if (de->flags & PS2MCFS_DIRENT_INVALID)
		goto out;

	/*
	 * the size must be zero.
	 * please see ps2mcfs_notify_change() in inode.c.
	 */
	if (inode->i_size != 0)
		goto out;

	/*
	 * save mode and time of creation 
	 */
	if (ps2mc_getdir(de->root->portslot, path, &mcdirent) < 0)
		goto out;

	/*
	 * remove the entry
	 */
	if (ps2mc_delete(de->root->portslot, path) < 0)
		goto out;

	/*
	 * create new one
	 */
	if (ps2mcfs_create(de) < 0)
		goto out;

	/*
	 * restore mode and time of creation 
	 */
	if (ps2mc_setdir(de->root->portslot, path,
			   PS2MC_SETDIR_CTIME, &mcdirent) < 0)
		goto out;

 out:
	ps2mcfs_put_path(de, path);
	return;
}
