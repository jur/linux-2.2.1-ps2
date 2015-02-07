/*
 * symlink.c
 *
 * PURPOSE
 *	Symlink handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2000 Ben Fennema
 *  (C) 1999 Stelias Computing Inc 
 *
 * HISTORY
 *
 *  04/16/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/udf_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include "udf_i.h"

static void udf_pc_to_char(char *from, int fromlen, char *to)
{
	struct PathComponent *pc;
	int elen = 0;
	char *p = to;

	while (elen < fromlen)
	{
		pc = (struct PathComponent *)(from + elen);
		switch (pc->componentType)
		{
			case 1:
				if (pc->lengthComponentIdent == 0)
				{
					p = to;
					*p++ = '/';
				}
				break;
			case 3:
				memcpy(p, "../", 3);
				p += 3;
				break;
			case 4:
				memcpy(p, "./", 2);
				p += 2;
				/* that would be . - just ignore */
				break;
			case 5:
				memcpy(p, pc->componentIdent, pc->lengthComponentIdent);
				p += pc->lengthComponentIdent;
				*p++ = '/';
		}
		elen += sizeof(struct PathComponent) + pc->lengthComponentIdent;
	}
	if (p > to+1)
		p[-1] = '\0';
	else
		p[0] = '\0';
}

static struct dentry * udf_follow_link(struct dentry * dentry,
	struct dentry * base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh = NULL;
	char *symlink, *tmpbuf;
	
	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		bh = udf_tread(inode->i_sb, inode->i_ino, inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data + udf_file_entry_alloc_offset(inode);
	}
	else
	{
		bh = bread(inode->i_dev, udf_bmap(inode, 0), inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data;
	}
	if ((tmpbuf = (char *)kmalloc(inode->i_size, GFP_KERNEL)))
	{
		udf_pc_to_char(symlink, inode->i_size, tmpbuf);
		base = lookup_dentry(tmpbuf, base, follow);
		kfree(tmpbuf);
		return base;
	}
	else
		return ERR_PTR(-ENOMEM);
}

static int udf_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh = NULL;
	char *symlink, *tmpbuf;
	int len;
	
	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		bh = udf_tread(inode->i_sb, inode->i_ino, inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data + udf_file_entry_alloc_offset(inode);
	}
	else
	{
		bh = bread(inode->i_dev, udf_bmap(inode, 0), inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data;
	}

	if ((tmpbuf = (char *)kmalloc(inode->i_size, GFP_KERNEL)))
	{
		udf_pc_to_char(symlink, inode->i_size, tmpbuf);
		if ((len = strlen(tmpbuf)) > buflen)
			len = buflen;
		if (copy_to_user(buffer, tmpbuf, len))
			len = -EFAULT;
		kfree(tmpbuf);
	}
	else
		len = -ENOMEM;

	UPDATE_ATIME(inode);
	if (bh)
		udf_release_data(bh);
	return len;
}

/*
 * symlinks can't do much...
 */
struct inode_operations udf_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	udf_readlink,	/* readlink */
	udf_follow_link,/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};
