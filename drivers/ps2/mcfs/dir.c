/*
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: dir.c,v 1.13 2001/03/26 08:33:37 shin Exp $
 */
#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/bitops.h>

#include "mcfs.h"
#include "mcfs_debug.h"

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))

static int ps2mcfs_dir_readdir(struct file *, void *, filldir_t);
static int ps2mcfs_dir_lookup(struct inode *, struct dentry *);
static int ps2mcfs_dir_mkdir(struct inode *, struct dentry *, int);
static int ps2mcfs_dir_delete(struct inode *, struct dentry *);
static int ps2mcfs_dir_create(struct inode *, struct dentry *, int);
static int ps2mcfs_dir_rename(struct inode *, struct dentry *,
			      struct inode *, struct dentry *);
static int ps2mcfs_dentry_revalidate(struct dentry *);

static struct file_operations ps2mcfs_dir_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	ps2mcfs_dir_readdir,	/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* can't fsync */
};

struct inode_operations ps2mcfs_dir_inode_operations = {
	&ps2mcfs_dir_operations,	/* root directory file-ops */
	ps2mcfs_dir_create,	/* create */
	ps2mcfs_dir_lookup,	/* lookup */
	NULL,			/* link */
	ps2mcfs_dir_delete,	/* unlink */
	NULL,			/* symlink */
	ps2mcfs_dir_mkdir,	/* mkdir */
	ps2mcfs_dir_delete,	/* rmdir */
	NULL,			/* mknod */
	ps2mcfs_dir_rename,	/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

struct dentry_operations ps2mcfs_dentry_operations = {
	ps2mcfs_dentry_revalidate,	/* d_validate*/
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	NULL,			/* d_delete */
	NULL,			/* d_release */
	NULL			/* d_iput */
};

static int
ps2mcfs_fullpath(struct inode *dir, struct dentry *dentry, char *path)
{
	int res;
	const char *parentpath;
	struct ps2mcfs_dirent *parent = dir->u.generic_ip;

	if (dentry->d_name.len > PS2MC_NAME_MAX)
		return -ENAMETOOLONG;
	parentpath = ps2mcfs_get_path(parent);
	if (*parentpath == '\0') {
		return (-ENAMETOOLONG); /* path name might be too long */
	}
	if (PS2MC_PATH_MAX <= strlen(parentpath) + dentry->d_name.len + 1) {
		res = -ENOENT;
		goto out;
	}

	sprintf(path, "%s%s%s", parentpath,
		parentpath[1] != '\0' ? "/" : "",
		ps2mcfs_terminate_name(dentry->d_name.name,
				       dentry->d_name.len));
	res = 0;

 out:
	ps2mcfs_put_path(parent, parentpath);

	return (res);
}

int
ps2mcfs_countdir(struct ps2mcfs_dirent *de)
{
	const char *path;
	int count, res;
	struct ps2mc_dirent buf;

	path = ps2mcfs_get_path(de);
	if (*path == '\0')
		return -ENAMETOOLONG; /* path name might be too long */
	count = 0;
	res = 0;
	for ( ; ; ) {
		res = ps2mc_readdir(de->root->portslot, path, count, &buf, 1);
		if (res <= 0)
			break; /* error or no more entries */ 
		/* read an entry successfully */
		count++;
	}

	ps2mcfs_put_path(de, path);

	return ((res == 0) ? count : res);
}

static int
ps2mcfs_newentry(struct inode *dir, struct dentry *dentry, const char *path, struct inode **inodep, int force_alloc_dirent)
{
	int res;
	struct ps2mcfs_dirent *parent = dir->u.generic_ip;
	struct ps2mcfs_dirent *newent;
	struct ps2mc_dirent buf;

	TRACE("ps2mcfs_newentry(dir=%p, dentry=%p): %s\n", dir, dentry, path);

	res = ps2mc_getdir(parent->root->portslot, path, &buf);
	if (res < 0)
		return res;

	*inodep = NULL;
	if (res != 0 || force_alloc_dirent) {
		/* there is real entry */

		newent = ps2mcfs_alloc_dirent(parent, dentry->d_name.name,
					      dentry->d_name.len);
		if (newent == NULL)
			return -ENOMEM;

		if ((*inodep = iget(dir->i_sb, newent->ino)) == NULL) {
			ps2mcfs_free_dirent(newent);
			return -EINVAL; /* ??? */
		}
	}

	return (0);
}

static int
ps2mcfs_dir_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;
	char path[PS2MC_PATH_MAX + 1];
	struct ps2mcfs_dirent *de = dir->u.generic_ip;
	struct inode *inode;
	struct ps2mc_dirent mcdirent;

	if ((res = ps2mcfs_fullpath(dir, dentry, path)) < 0)
		return res;

	TRACE("ps2mcfs_dir_create(%s)\n", path);

	mcdirent.mode = mode;
	res = ps2mc_setdir(de->root->portslot, path,
			   PS2MC_SETDIR_MODE, &mcdirent);
	if (res < 0)
		return res;

	res = ps2mcfs_newentry(dir, dentry, path, &inode, 1);
	if (res < 0)
		return (res);

	/*
	 * XXX, You can't create real body of the file
	 * without the struct ps2mc_dirent
	 */
	res = ps2mcfs_create(inode->u.generic_ip);
	if (res < 0) {
		inode->i_nlink = 0;
		ps2mcfs_free_dirent(inode->u.generic_ip);
		return (res);
	}
	ps2mcfs_update_inode(inode);

	dir->i_nlink++;
	d_instantiate(dentry, inode);

	/* update directory size */
	if ((res = ps2mcfs_update_inode(dir)) < 0)
		return (res);

	return (0);
}

static int
ps2mcfs_dir_rename(struct inode *old_dir, struct dentry *old_dentry,
		   struct inode *new_dir, struct dentry *new_dentry)
{
	int res;
	char path[PS2MC_PATH_MAX + 1];
	char name[PS2MC_PATH_MAX + 1];
	struct ps2mcfs_dirent *parent = old_dir->u.generic_ip;

	if ((res = ps2mcfs_fullpath(old_dir, old_dentry, path)) < 0)
		return res;

	strcpy(name, ps2mcfs_terminate_name(new_dentry->d_name.name,
					    new_dentry->d_name.len));
#ifdef PS2MCFS_DEBUG
	{
		char path2[PS2MC_PATH_MAX + 1];
		if ((res = ps2mcfs_fullpath(new_dir, new_dentry, path2)) < 0)
			return res;
		TRACE("dir rename('%s'(%p)->'%s'(%p))\n",
		      path, old_dentry->d_inode, 
		      path2, new_dentry->d_inode);
	}
#endif

	if (old_dir != new_dir)
		return -EPERM;

	if ((res = ps2mc_rename(parent->root->portslot, path, name)) == 0)
		d_delete(old_dentry);

	return (res);
}

static int
ps2mcfs_dir_lookup(struct inode *dir, struct dentry *dentry)
{
	int res;
	char path[PS2MC_PATH_MAX + 1];
	struct inode *inode;

	TRACE("ps2mcfs_dir_lookup(dir=%p, dent=%p): %s\n", dir, dentry,
	      ps2mcfs_terminate_name(dentry->d_name.name, dentry->d_name.len));

	if ((res = ps2mcfs_fullpath(dir, dentry, path)) < 0 ||
	    (res = ps2mcfs_newentry(dir, dentry, path, &inode, 0)) < 0)
		return (res);
	dentry->d_op = &ps2mcfs_dentry_operations;
	d_add(dentry, inode);

	return (0);
}

static int
ps2mcfs_dir_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int pos, res;
	struct ps2mc_dirent buf;
	struct ps2mcfs_dirent *de = inode->u.generic_ip;
	const char *path;

	TRACE("ps2mcfs_dir_readdir(filp=%p): inode=%p pos=%ld dirent=%p\n",
	      filp, inode, (long)filp->f_pos, de);

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	pos = filp->f_pos;
	if (de->parent == NULL && *ps2mcfs_basedir == '\0') {
		/* root directory of PS2 Memory Card doesn't
		   have '.' and '..' */
		if (filp->f_pos == 0) {
			if (filldir(dirent, ".", 1, filp->f_pos,
				    de->ino) < 0)
				return 0;
			filp->f_pos++;
		}
		if (filp->f_pos == 1) {
			if (filldir(dirent, "..", 2, filp->f_pos,
				    ps2mcfs_pseudo_ino()) < 0)
				return 0;
			filp->f_pos++;
		}
	}
	for ( ; ; ) {
		path = ps2mcfs_get_path(de);
		if (*path == '\0')
			return -ENAMETOOLONG; /* path name might be too long */
		res = ps2mc_readdir(de->root->portslot, path, pos,
				    &buf, 1);
		ps2mcfs_put_path(de, path);
		if (res < 0)
			return 0; /* XXX, error, try again ??? */
		if (res == 0)
			return 1; /* no more entries */

		/* read an entry successfully */
		pos++;

		/* copy directory information */
		res = filldir(dirent, buf.name, buf.namelen,
			      filp->f_pos, ps2mcfs_pseudo_ino());
		if (res < 0)
			return 0;
		filp->f_pos++;
	}

	return 1;
}

static int
ps2mcfs_dir_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;
	struct ps2mcfs_dirent *parent = dir->u.generic_ip;
	char path[PS2MC_PATH_MAX + 1];
	struct inode *inode;
	struct ps2mc_dirent mcdirent;

	TRACE("ps2mcfs_dir_mkdir(%s)\n",
	      ps2mcfs_terminate_name(dentry->d_name.name, dentry->d_name.len));

	if ((res = ps2mcfs_fullpath(dir, dentry, path)) < 0)
		return res;

	res = ps2mc_mkdir(parent->root->portslot, path);
	if (res < 0)
		return (res);

	mcdirent.mode = mode;
	res = ps2mc_setdir(parent->root->portslot, path,
			   PS2MC_SETDIR_MODE, &mcdirent);
	if (res < 0)
		return res;

	res = ps2mcfs_newentry(dir, dentry, path, &inode, 0);
	if (res < 0) {
		ps2mc_delete(parent->root->portslot, path);
		return (res);
	}

	dir->i_nlink++;
	d_instantiate(dentry, inode);

	/* update directory size */
	if ((res = ps2mcfs_update_inode(dir)) < 0)
		return (res);

	return (0);
}

static int
ps2mcfs_dir_delete(struct inode *inode, struct dentry *dentry)
{
	int res;
	char path[PS2MC_PATH_MAX + 1];
	struct ps2mcfs_dirent *de = inode->u.generic_ip;

	if ((res = ps2mcfs_fullpath(inode, dentry, path)) < 0)
		return res;

	TRACE("ps2mcfs_dir_delete(%s): inode=%p dentry=%p\n",
	      path, inode, dentry);

	if ((res = ps2mc_delete(de->root->portslot, path)) < 0)
		return (res);

	/* decrement parent directory's link count */
	inode->i_nlink--;

	/* release inode */
	dentry->d_inode->i_nlink = 0;
	d_delete(dentry);

	/* update directory size */
	if ((res = ps2mcfs_update_inode(inode)) < 0)
		return (res);

	return (0);
}

static int
ps2mcfs_dentry_revalidate(struct dentry *dentry)
{
	struct inode *inode;
	struct ps2mcfs_dirent *de;

	if ((inode = dentry->d_inode) == NULL)
		return (0);
	if ((de = inode->u.generic_ip) == NULL)
		return (0);
#ifdef PS2MCFS_DEBUG
	{
		const char *path;
		path = ps2mcfs_get_path(de);
		TRACE("ps2mcfs_dentry_revalidate(%s): inode=%p dentry=%p %s\n",
		      path, inode, dentry,
		      (de->flags & PS2MCFS_DIRENT_INVALID) ? "<invalid>" : "");
		ps2mcfs_put_path(de, path);
	}
#endif
	if (de->flags & PS2MCFS_DIRENT_INVALID)
		return (0);

	return (1); /* this entry is valid */
}
