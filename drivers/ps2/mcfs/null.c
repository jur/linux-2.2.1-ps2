/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: null.c,v 1.3 2000/09/26 04:21:11 takemura Exp $
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/init.h>

#include "mcfs.h"
#include "mcfs_debug.h"

static int ps2mcfs_null_lookup(struct inode *, struct dentry *);

static struct file_operations ps2mcfs_null_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	NULL,			/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations ps2mcfs_null_inode_operations = {
	&ps2mcfs_null_operations,	/* root directory file-ops */
	NULL,			/* create */
	ps2mcfs_null_lookup,	/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int
ps2mcfs_null_lookup(struct inode *dir, struct dentry *dentry)
{
	return -ENOENT;
}
