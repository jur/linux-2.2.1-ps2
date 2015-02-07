/*
 * tst_dev.c - Test and Set device for mips which has not LL/SC. 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <linux/autoconf.h>

#ifndef __mips
#error "Sorry, this device is for MIPS only."
#endif
#ifdef CONFIG_SMP
#error "Not on this device"
#endif

/*
 * 	Setup/Clean up Driver Module
 */

#ifdef MODULE

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#endif /* MODULE */

#include <linux/init.h>

#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* file op. */
#include <linux/proc_fs.h>	/* proc fs file op. */
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/uaccess.h>	/* copy to/from user space */
#include <asm/page.h>		/* page size */
#include <asm/pgtable.h>	/* PAGE_READONLY */

#include <linux/tst_dev.h>

#include <linux/module.h>

#include <linux/major.h>

#ifndef TSTDEV_MAJOR
#define TSTDEV_MAJOR    123
#endif

static int tst_major=TSTDEV_MAJOR;	

EXPORT_SYMBOL(tst_major);	/* export symbole */
MODULE_PARM(tst_major,"i");	/* as parameter on loaing */

/*
 *
 */


/*
 * File Operations table
 *	please refer <linux/fs.h> for other methods.
 */

static struct file_operations  tst_fops; 


#ifndef TST_DEVICE_NAME
#define  TST_DEVICE_NAME "tst"
#endif 



static unsigned long tst_code_buffer = 0 ;
static const __u32 tst_code[] = {
 /*  0:*/   0x0080d821,        //move    $k1,$a0
 /*  4:*/   0x8c820000,        //lw      $v0,0($a0)
 /*  8:*/   0x24080001,        //li      $t0,1
 /*  c:*/   0x14400004,        //bnez    $v0,20
 /* 10:*/   0x00000000,        //nop
 /* 14:*/   0x1764fffa,        //bne     $k1,$a0,0
 /* 18:*/   0x00000000,        //nop
 /* 1c:*/   0xaf680000,        //sw      $t0,0($k1)
 /* 20:*/   0x03e00008,        //jr      $ra
 /* 24:*/   0x00000000,        //nop
			0 };

EXPORT_SYMBOL(tst_code_buffer);	/* export symbole */

/********************

#include<asm/regdef.h>

        .set noreorder
0:
        move    k1,a0
        lw      v0,0(a0)
        li      t0,1
        bnez    v0,1f
        nop
        bne     k1,a0,0b
        nop
        sw      t0,0(k1)
1:
        jr      ra
        nop

*********************/




static 
int try_init_code_buffer(unsigned int gfp_flag)
{
	if (!tst_code_buffer) {
		tst_code_buffer = __get_free_page(gfp_flag);
		if (!tst_code_buffer)
			return -EBUSY;

		memcpy ((void *)tst_code_buffer, (void *)tst_code, 
			sizeof (tst_code) * sizeof (tst_code[0]));

	}
	return 0;
}

static 
void try_free_code_buffer(void)
{
	if (tst_code_buffer) {
		free_page (tst_code_buffer);
		tst_code_buffer=0;
	}
}

static 
int get_tst_info(char *buf, char **start, off_t pos, int count, int wr);

static
struct proc_dir_entry proc_mod = {
	low_ino:0,		/* inode # will be dynamic assgined */
	namelen:sizeof(TST_DEVICE_NAME)-1, 
	name:TST_DEVICE_NAME,
	mode:(S_IFREG | S_IRUGO),	/* in <linux/stat.h> */
	nlink:1,		/* 1 for FILE, 2 for DIR */
	uid:0, gid:0,		/* owner and group */
	size:0, 		/* inode size */
	ops:NULL,		/* use default procs for leaf */
	get_info:get_tst_info,
};

/*
 * Caller of (*get_info)() is  proc_file_read() in fs/proc/generic.c
 */
static 
int
get_tst_info(char *buf, 	/*  allocated area for info */
	       char **start, 	/*  return youown area if you allocate */
	       off_t pos,	/*  pos arg of vfs read */
	       int count,	/*  readable bytes */
	       int wr)		/*  1, for O_RDWR  */
{

/* SPRINTF does not exist in the kernel */
#define MY_BUFSIZE 256
#define MARGIN 16
	char mybuf[MY_BUFSIZE+MARGIN];

	int len;

	len = sprintf(mybuf,
		      "_TST_INFO_MAGIC:\t0x%8.8x\n"
		      "_TST_START_MAGIC:\t0x%8.8x\n"
		      "_TST_ACCESS_MAGIC:\t0x%8.8x\n",
		      _TST_INFO_MAGIC,
		      _TST_START_MAGIC,
		      _TST_ACCESS_MAGIC
		      );
	if (len >= MY_BUFSIZE) mybuf[MY_BUFSIZE] = '\0';

	if ( pos+count >= len ) {
		count = len-pos;
	}
	memcpy (buf, mybuf+pos, count);
	return count;
}

#ifdef MODULE
int
init_module (void)
{

	int result;
	result = register_chrdev(tst_major, TST_DEVICE_NAME , &tst_fops);
	if (result < 0) {
		printk(KERN_WARNING 
		       TST_DEVICE_NAME ": can't get major %d\n",tst_major);
		return result;
	}
	if (tst_major == 0) tst_major = result; /* dynamic */

	/*
	 * register /proc entry, if you want.
	 */
	result=proc_register(&proc_root, &proc_mod);
	if (result < 0)  {
		printk(KERN_WARNING 
		       TST_DEVICE_NAME ": can't get proc entry\n");
		unregister_chrdev(tst_major, TST_DEVICE_NAME);
		return result;
	}

	(void) try_init_code_buffer(GFP_KERNEL);

	return 0;
}
void
cleanup_module (void)
{
	/* free code buffer */
	try_free_code_buffer();

	/* unregister /proc entry */
	(void) proc_unregister(&proc_root, proc_mod.low_ino);

	/* unregister chrdev */
	unregister_chrdev(tst_major, TST_DEVICE_NAME);
}


#endif /* MODULE */

__initfunc(void tst_dev_init(void))
{
	int result;

	result = register_chrdev(tst_major,TST_DEVICE_NAME,&tst_fops);
	if (result < 0) {
		printk(KERN_WARNING 
			TST_DEVICE_NAME ": can't get major %d\n",tst_major);
		return;
	}
	if (tst_major == 0) tst_major = result; /* dynamic */
	result = proc_register(&proc_root, &proc_mod);
	if (result < 0)  {
		printk(KERN_WARNING 
			TST_DEVICE_NAME ": can't get proc entry\n");
			unregister_chrdev(tst_major, TST_DEVICE_NAME);
	}
}


//========================================================================

/*
 * VMA Opreations
 */

static void tst_vma_open(struct vm_area_struct *vma)
{
    MOD_INC_USE_COUNT;
}

static void tst_vma_close(struct vm_area_struct *vma)
{
    MOD_DEC_USE_COUNT;
}

static
unsigned long tst_vma_nopage (struct vm_area_struct * area, 
			unsigned long address, int write_access)
{
	if ( address  != _TST_START_MAGIC 
	    || area->vm_start  != _TST_START_MAGIC
	    || area->vm_offset != 0 )
		return 0;

	atomic_inc(&mem_map[MAP_NR(tst_code_buffer)].count);
	return tst_code_buffer;
}


static struct vm_operations_struct tst_vm_ops = {
	open:tst_vma_open,
	close:tst_vma_close,
	nopage:tst_vma_nopage,
};

//========================================================================

/*
 * 	Device File Operations
 */


/*
 * Open and Close
 */

static int tst_open (struct inode *p_inode, struct file *p_file)
{
	
	int ret_code;
        if ( p_file->f_mode & FMODE_WRITE ) {
                return -EPERM;
        }
	
	ret_code =  try_init_code_buffer (GFP_KERNEL);
	if (ret_code) {
		return ret_code;
	}

	/* 
	 * if you want store something for later processing, do it on
	 * p_file->private_data .
	 */
        MOD_INC_USE_COUNT;
        return 0;          /* success */
}

static int tst_release (struct inode *p_inode, struct file *p_file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


/*
 * Mmap
 */
static int tst_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size;

	if (vma->vm_start != _TST_START_MAGIC)
		return -ENXIO;

	if (vma->vm_offset != 0)
		return -ENXIO;

	size = vma->vm_end - vma->vm_start;
	if (size != PAGE_SIZE)
		return -EINVAL;

	vma->vm_ops = &tst_vm_ops;
	vma->vm_file = file;
	file->f_count++;

	tst_vma_open(vma);

	return 0;
}


/*
 * Read
 */
static ssize_t tst_read(struct file *p_file, char * p_buff, size_t count, 
		   loff_t * p_pos)
{
	
	struct _tst_area_info info;
	int data;
	struct inode * p_inode;
	int info_size = sizeof(info);

	p_inode = p_file->f_dentry->d_inode;
	data = MAJOR(p_inode->i_rdev);

	info.magic = _TST_INFO_MAGIC;
	info.pad1 = 0;
	info.map_addr = (void *)_TST_START_MAGIC;
#if _MIPS_SZPTR==32
	info.pad2 = 0;
#endif

	if (*p_pos + count >= info_size){
		count = info_size - *p_pos;
	}
	if(copy_to_user(p_buff,((char *)&info)+*p_pos, count)) {
		return -EFAULT;
	}
	*p_pos += count;
	return count;
}

static
struct file_operations  tst_fops = {
	/* ssize_t (*read) (struct file *, char *, size_t, loff_t *); */
	read:tst_read,
	/* int (*open) (struct inode *, struct file *); */
	open:tst_open,
	/* int (*release) (struct inode *, struct file *);*/
	release:tst_release, 
	/* int (*mmap) (struct file *, struct vm_area_struct *); */
	mmap:tst_mmap,
};
