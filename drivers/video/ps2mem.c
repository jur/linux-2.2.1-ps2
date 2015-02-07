/*
 *  linux/drivers/video/ps2mem.c
 *  PlayStation 2 DMA buffer memory allocation interface (/dev/ps2mem)
 *
 *	Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/addrspace.h>

#include <linux/ps2/dev.h>
#include "ps2dma.h"

static unsigned long ps2mem_vma_nopage(struct vm_area_struct *vma, unsigned long addr, int write)
{
    struct page_list *list, *newlist;
    unsigned long offset, page;
    int index;

    list = vma->vm_file->private_data;
    offset = addr - vma->vm_start + vma->vm_offset;
    index = offset >> PAGE_SHIFT;
    if (list->pages >= index) {
	/* access to unallocated area - extend buffer */
	if ((newlist = ps2pl_realloc(list, index + 1)) == NULL)
	    return 0;		/* no memory - SIGBUS */
	list = vma->vm_file->private_data = newlist;
    }
    page = list->page[index];
    atomic_inc(&mem_map[MAP_NR(page)].count);	/* increment reference count */
    return page;
}

static struct vm_operations_struct ps2mem_vmops = {
    NULL,		/* open */
    NULL,		/* close */
    NULL,		/* unmap */
    NULL,		/* protect */
    NULL,		/* sync */
    NULL,		/* advise */
    ps2mem_vma_nopage,	/* nopage */
};

static int ps2mem_open(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}

static int ps2mem_release(struct inode *inode, struct file *file)
{
    if (file->private_data)
	ps2pl_free((struct page_list *)file->private_data);
    return 0;
}

static int ps2mem_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct page_list *list, *newlist;
    int pages;

    if (vma->vm_offset & (PAGE_SIZE - 1))
	return -ENXIO;
    pages = (vma->vm_end - vma->vm_start + vma->vm_offset) >> PAGE_SHIFT;
    if (file->private_data == NULL) {
	/* 1st mmap ... allocate buffer */
	if ((list = ps2pl_alloc(pages)) == NULL)
	    return -ENOMEM;
	file->private_data = list;
    } else {
	list = (struct page_list *)file->private_data;
	if (list->pages < pages) {		/* extend buffer */
	    if ((newlist = ps2pl_realloc(list, pages)) == NULL)
		return -ENOMEM;
	    file->private_data = newlist;
	}	
    }

    vma->vm_ops = &ps2mem_vmops;
    vma->vm_file = file;
    file->f_count++;
    return 0;
}

static loff_t ps2mem_llseek(struct file *file, loff_t offset, int orig)
{
    return -ESPIPE;	/* cannot lseek */
}

struct file_operations ps2mem_fops = {
    ps2mem_llseek,	/* llseek (error) */
    NULL,		/* read */
    NULL,		/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    NULL,		/* ioctl */
    ps2mem_mmap,	/* mmap */
    ps2mem_open,	/* open */
    NULL,		/* flush */
    ps2mem_release,	/* release */
    NULL,		/* fsync */
    NULL,		/* fasync */
};
