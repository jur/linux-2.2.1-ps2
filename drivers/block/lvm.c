/*
 * kernel/lvm.c
 *
 * Copyright (C) 1997 - 1999  Heinz Mauelshagen, Germany
 *
 * February-November 1997
 * April-May,July-August,November 1998
 * January-March,May,July,September,October 1999
 *
 *
 * LVM driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * LVM driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 *
 */

/*
 * Changelog
 *
 *    09/11/1997 - added chr ioctls VG_STATUS_GET_COUNT
 *                 and VG_STATUS_GET_NAMELIST
 *    18/01/1998 - change lvm_chr_open/close lock handling
 *    30/04/1998 - changed LV_STATUS ioctl to LV_STATUS_BYNAME and
 *               - added   LV_STATUS_BYINDEX ioctl
 *               - used lvm_status_byname_req_t and
 *                      lvm_status_byindex_req_t vars
 *    04/05/1998 - added multiple device support
 *    08/05/1998 - added support to set/clear extendable flag in volume group
 *    09/05/1998 - changed output of lvm_proc_get_info() because of
 *                 support for free (eg. longer) logical volume names
 *    12/05/1998 - added spin_locks (thanks to Pascal van Dam
 *                 <pascal@ramoth.xs4all.nl>)
 *    25/05/1998 - fixed handling of locked PEs in lvm_map() and lvm_chr_ioctl()
 *    26/05/1998 - reactivated verify_area by access_ok
 *    07/06/1998 - used vmalloc/vfree instead of kmalloc/kfree to go
 *                 beyond 128/256 KB max allocation limit per call
 *               - #ifdef blocked spin_lock calls to avoid compile errors
 *                 with 2.0.x
 *    11/06/1998 - another enhancement to spinlock code in lvm_chr_open()
 *                 and use of LVM_VERSION_CODE instead of my own macros
 *                 (thanks to  Michael Marxmeier <mike@msede.com>)
 *    07/07/1998 - added statistics in lvm_map()
 *    08/07/1998 - saved statistics in do_lv_extend_reduce()
 *    25/07/1998 - used __initfunc macro
 *    02/08/1998 - changes for official char/block major numbers
 *    07/08/1998 - avoided init_module() and cleanup_module() to be static
 *    30/08/1998 - changed VG lv_open counter from sum of LV lv_open counters
 *                 to sum of LVs open (no matter how often each is)
 *    01/09/1998 - fixed lvm_gendisk.part[] index error
 *    07/09/1998 - added copying of lv_current_pe-array
 *                 in LV_STATUS_BYINDEX ioctl
 *    17/11/1998 - added KERN_* levels to printk
 *    13/01/1999 - fixed LV index bug in do_lv_create() which hit lvrename
 *    07/02/1999 - fixed spinlock handling bug in case of LVM_RESET
 *                 by moving spinlock code from lvm_chr_open()
 *                 to lvm_chr_ioctl()
 *               - added LVM_LOCK_LVM ioctl to lvm_chr_ioctl()
 *               - allowed LVM_RESET and retrieval commands to go ahead;
 *                 only other update ioctls are blocked now
 *               - fixed pv->pe to NULL for pv_status
 *               - using lv_req structure in lvm_chr_ioctl() now
 *               - fixed NULL ptr reference bug in do_lv_extend_reduce()
 *                 caused by uncontiguous PV array in lvm_chr_ioctl(VG_REDUCE)
 *    09/02/1999 - changed BLKRASET and BLKRAGET in lvm_char_ioctl() to
 *                 handle lgoical volume private read ahead sector
 *               - implemented LV read_ahead handling with lvm_blk_read()
 *                 and lvm_blk_write()
 *    10/02/1999 - implemented 2.[12].* support function lvm_hd_name()
 *                 to be used in drivers/block/genhd.c by disk_name()
 *    12/02/1999 - fixed index bug in lvm_blk_ioctl(), HDIO_GETGEO
 *               - enhanced gendisk insert/remove handling
 *    16/02/1999 - changed to dynamic block minor number allocation to
 *                 have as much as 99 volume groups with 256 logical volumes
 *                 as the grand total; this allows having 1 volume group with
 *                 up to 256 logical volumes in it
 *    21/02/1999 - added LV open count information to proc filesystem
 *               - substituted redundant LVM_RESET code by calls
 *                 to do_vg_remove()
 *    22/02/1999 - used schedule_timeout() to be more responsive
 *                 in case of do_vg_remove() with lots of logical volumes
 *    19/03/1999 - fixed NULL pointer bug in module_init/lvm_init
 *    17/05/1999 - used DECLARE_WAIT_QUEUE_HEAD macro (>2.3.0)
 *               - enhanced lvm_hd_name support
 *    03/07/1999 - avoided use of KERNEL_VERSION macro based ifdefs and
 *                 memcpy_tofs/memcpy_fromfs macro redefinitions
 *    06/07/1999 - corrected reads/writes statistic counter copy in case
 *                 of striped logical volume
 *    28/07/1999 - implemented snapshot logical volumes
 *                 - lvm_chr_ioctl
 *                   - LV_STATUS_BYINDEX
 *                   - LV_STATUS_BYNAME
 *                 - do_lv_create
 *                 - do_lv_remove
 *                 - lvm_map
 *                 - new lvm_snapshot_remap_block
 *                 - new lvm_snapshot_remap_new_block
 *
 */

/*
 * TODO
 *
 *   - implement special handling of unavailable physical volumes
 *
 */

char *lvm_version = "LVM version 0.8i  by Heinz Mauelshagen  (02/10/1999)\n";
char *lvm_short_version = "version 0.8i  (02/10/1999)";

#include <linux/config.h>
#include <linux/version.h>

#ifdef MODVERSIONS
#undef MODULE
#define MODULE
#include <linux/modversions.h>
#endif

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include <linux/hdreg.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/ioctl.h>
#include <asm/segment.h>

#include <asm/uaccess.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

#include <linux/errno.h>
#include <linux/lvm.h>

#define	LVM_CORRECT_READ_AHEAD( a) \
   if      ( a < LVM_MIN_READ_AHEAD || \
             a > LVM_MAX_READ_AHEAD) a = LVM_MAX_READ_AHEAD;

#define	suser()	( current->uid == 0 && current->euid == 0)


/*
 * External function prototypes
 */
#ifdef MODULE
int init_module(void);
void cleanup_module(void);
#else
extern int lvm_init(void);
#endif

static void lvm_dummy_device_request(void);
static int lvm_blk_ioctl(struct inode *, struct file *, unsigned int,
			 unsigned long);
static int lvm_blk_open(struct inode *, struct file *);

static ssize_t lvm_blk_read(struct file *, char *, size_t, loff_t *);
static ssize_t lvm_blk_write(struct file *, const char *, size_t, loff_t *);

static int lvm_chr_open(struct inode *, struct file *);

static int lvm_chr_close(struct inode *, struct file *);
static int lvm_blk_close(struct inode *, struct file *);

static int lvm_chr_ioctl(struct inode *, struct file *, unsigned int,
			 unsigned long);

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
static int lvm_proc_get_info(char *, char **, off_t, int, int);
#endif

#ifdef LVM_HD_NAME
void lvm_hd_name(char *, int);
#endif
/* End external function prototypes */


/*
 * Internal function prototypes
 */
static void lvm_init_vars(void);
extern int (*lvm_map_ptr) (int, kdev_t *, unsigned long *,
			   unsigned long, int);
static int lvm_snapshot_remap_block(ulong *, kdev_t *, int);
static int lvm_snapshot_remap_new_block(ulong *, kdev_t *, int);

#ifdef LVM_HD_NAME
extern void (*lvm_hd_name_ptr) (char *, int);
#endif
static int lvm_map(int, kdev_t *, unsigned long *, unsigned long, int);
static int do_vg_create(int, void *);
static int do_vg_remove(int);
static int do_lv_create(int, char *, lv_t *);
static int do_lv_remove(int, char *, int);
static int do_lv_extend_reduce(int, char *, lv_t *);
static void lvm_geninit(struct gendisk *);
#ifdef LVM_GET_INODE
static struct inode *lvm_get_inode(int);
#endif
inline int lvm_strlen(char *);
inline void lvm_memcpy(char *, char *, int);
inline int lvm_strcmp(char *, char *);
inline char *lvm_strrchr(char *, char c);
/* END Internal function prototypes */


/* volume group descriptor area pointers */
static vg_t *vg[ABS_MAX_VG + 1];
static pv_t *pvp = NULL;
static lv_t *lvp = NULL;
static pe_t *pep = NULL;
static pe_t *pep1 = NULL;


/* map from block minor number to VG and LV numbers */
typedef struct {
	int vg_number;
	int lv_number;
} vg_lv_map_t;
static vg_lv_map_t vg_lv_map[ABS_MAX_LV];


/* Request structures (lvm_chr_ioctl()) */
static pv_change_req_t pv_change_req;
static pv_flush_req_t pv_flush_req;
static pv_status_req_t pv_status_req;
static pe_lock_req_t pe_lock_req;
static le_remap_req_t le_remap_req;
static lv_req_t lv_req;

#ifdef LVM_TOTAL_RESET
static int lvm_reset_spindown = 0;
#endif

static char pv_name[NAME_LEN];
/* static char rootvg[NAME_LEN] = { 0, }; */
static uint lv_open = 0;
static const char *const lvm_name = LVM_NAME;
static int lock = 0;
static int loadtime = 0;
static uint vg_count = 0;
static long lvm_chr_open_count = 0;
static ushort lvm_iop_version = LVM_DRIVER_IOP_VERSION;
#if LINUX_VERSION_CODE > KERNEL_VERSION ( 2, 3, 0)
static DECLARE_WAIT_QUEUE_HEAD(lvm_wait);
static DECLARE_WAIT_QUEUE_HEAD(lvm_map_wait);
#else
struct wait_queue *lvm_wait = NULL;
struct wait_queue *lvm_map_wait = NULL;
#endif

static spinlock_t lvm_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t __lvm_dummy_lock = SPIN_LOCK_UNLOCKED;


#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
static struct proc_dir_entry lvm_proc_entry =
{
	0, 3, LVM_NAME, S_IFREG | S_IRUGO,
	1, 0, 0, 0,
	NULL,
	lvm_proc_get_info,
	NULL, NULL, NULL, NULL, NULL,
};
#endif

static struct file_operations lvm_chr_fops =
{
	NULL,			/* No lseek              */
	NULL,			/* No read               */
	NULL,			/* No write              */
	NULL,			/* No readdir            */
	NULL,			/* No select             */
	lvm_chr_ioctl,
	NULL,			/* No mmap               */
	lvm_chr_open,
	NULL,			/* No flush              */
	lvm_chr_close,
	NULL,			/* No fsync              */
	NULL,			/* No fasync             */
	NULL,			/* No check_media_change */
	NULL,			/* No revalidate         */
	NULL,			/* No lock               */
};

static struct file_operations lvm_blk_fops =
{
	NULL,			/* No lseek             */
	lvm_blk_read,		/* read                 */
	lvm_blk_write,		/* write                 */
	NULL,			/* No readdir            */
	NULL,			/* No select             */
	lvm_blk_ioctl,
	NULL,			/* No mmap               */
	lvm_blk_open,
	NULL,			/* No flush              */
	lvm_blk_close,
	block_fsync,		/* fsync                 */
	NULL,			/* No fasync             */
	NULL,			/* No check_media_change */
	NULL,			/* No revalidate         */
	NULL			/* No lock               */
};


/* gendisk structures */
static struct hd_struct lvm_hd_struct[MAX_LV];
static int lvm_blocksizes[MAX_LV] =
{0,};
static int lvm_size[MAX_LV] =
{0,};
static struct gendisk lvm_gendisk =
{
	LVM_BLOCK_MAJOR,	/* major # */
	LVM_NAME,		/* name of major */
	0,			/* number of times minor is shifted
				   to get real minor */
	1,			/* maximum partitions per device */
	MAX_LV,			/* maximum number of real devices */
	lvm_geninit,		/* initialization called before we
				   do other things */
	lvm_hd_struct,		/* partition table */
	lvm_size,		/* device size in blocks, copied
				   to block_size[] */
	MAX_LV,			/* number or real devices */
	NULL,			/* internal */
	NULL,			/* pointer to next gendisk struct (internal) */
};


#ifdef MODULE
/*
 * Module initialization...
 */
int init_module(void)
#else
/*
 * Driver initialization...
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION ( 2, 1 ,0)
__initfunc ( extern int lvm_init ( void))
#else
extern int lvm_init ( void)
#endif
#endif				/* #ifdef MODULE */
{
	struct gendisk *gendisk_ptr = NULL;

	lvm_init_vars();

	if (register_chrdev(LVM_CHAR_MAJOR, lvm_name, &lvm_chr_fops) < 0) {
		printk(KERN_ERR "%s -- register_chrdev failed\n", lvm_name);
		return -EIO;
	}
	if (register_blkdev(LVM_BLOCK_MAJOR, lvm_name, &lvm_blk_fops) < 0) {
		printk("%s -- register_blkdev failed\n", lvm_name);
		if (unregister_chrdev(LVM_CHAR_MAJOR, lvm_name) < 0)
			printk(KERN_ERR "%s -- unregister_chrdev failed\n", lvm_name);
		return -EIO;
	}
#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	proc_register(&proc_root, &lvm_proc_entry);
#endif

#ifdef DEBUG
	printk(KERN_INFO "%s -- registered\n", lvm_name);
#endif

	blk_dev[LVM_BLOCK_MAJOR].request_fn = lvm_dummy_device_request;
	blk_dev[LVM_BLOCK_MAJOR].current_request = NULL;

	/* insert our gendisk at the corresponding major */
	lvm_geninit(&lvm_gendisk);
	if (gendisk_head != NULL) {
		gendisk_ptr = gendisk_head;
		while (gendisk_ptr->next != NULL &&
		       gendisk_ptr->major > lvm_gendisk.major) {
			gendisk_ptr = gendisk_ptr->next;
		}
		lvm_gendisk.next = gendisk_ptr->next;
		gendisk_ptr->next = &lvm_gendisk;
	} else {
		gendisk_head = &lvm_gendisk;
		lvm_gendisk.next = NULL;
	}

	/* reference from drivers/block/ll_rw_blk.c */
	lvm_map_ptr = lvm_map;

#ifdef LVM_HD_NAME
	/* reference from drivers/block/genhd.c */
	lvm_hd_name_ptr = lvm_hd_name;
#endif

	/* optional read root VGDA */
/*
   if ( *rootvg != 0) {
   vg_read_with_pv_and_lv ( rootvg, &vg);
   }
 */

	printk(KERN_INFO
	       "%s%s -- "
#ifdef MODULE
	       "Module"
#else
	       "Driver"
#endif
	       " successfully initialized\n",
	       lvm_version, lvm_name);

	return 0;
}				/* init_module () / lvm_init () */


#ifdef MODULE
/*
 * Module cleanup...
 */
void cleanup_module(void)
{
	struct gendisk *gendisk_ptr = NULL, *gendisk_ptr_prev = NULL;

	gendisk_ptr = gendisk_ptr_prev = gendisk_head;
	while (gendisk_ptr != NULL) {
		if (gendisk_ptr == &lvm_gendisk)
			break;
		gendisk_ptr_prev = gendisk_ptr;
		gendisk_ptr = gendisk_ptr->next;
	}
	/* delete our gendisk from chain */
	if (gendisk_ptr == &lvm_gendisk)
		gendisk_ptr_prev->next = gendisk_ptr->next;

	blk_size[LVM_BLOCK_MAJOR] = NULL;
	blksize_size[LVM_BLOCK_MAJOR] = NULL;

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	proc_unregister(&proc_root, lvm_proc_entry.low_ino);
#endif

	if (unregister_chrdev(LVM_CHAR_MAJOR, lvm_name) < 0) {
		printk(KERN_ERR "%s -- unregister_chrdev failed\n", lvm_name);
	}
	if (unregister_blkdev(LVM_BLOCK_MAJOR, lvm_name) < 0) {
		printk(KERN_ERR "%s -- unregister_blkdev failed\n", lvm_name);
	}
	/* reference from linux/drivers/block/ll_rw_blk.c */
	lvm_map_ptr = NULL;

#ifdef LVM_HD_NAME
	/* reference from linux/drivers/block/genhd.c */
	lvm_hd_name_ptr = NULL;
#endif

	blk_dev[LVM_BLOCK_MAJOR].request_fn = NULL;
	blk_dev[LVM_BLOCK_MAJOR].current_request = NULL;

	printk(KERN_INFO "%s -- Module successfully deactivated\n", lvm_name);

	return;
}				/* void cleanup_module () */
#endif				/* #ifdef MODULE */


/*
 * support function to initialize lvm variables
 */
__initfunc(static void lvm_init_vars(void))
{
	int v;

	loadtime = CURRENT_TIME;

	pe_lock_req.lock = UNLOCK_PE;
	pe_lock_req.data.lv_dev = \
	    pe_lock_req.data.pv_dev = \
	    pe_lock_req.data.pv_offset = 0;

	/* Initialize VG pointers */
	for (v = 0; v <= ABS_MAX_VG; v++)
		vg[v] = NULL;

	/* Initialize LV -> VG association */
	for (v = 0; v < ABS_MAX_LV; v++) {
		/* index ABS_MAX_VG never used for real VG */
		vg_lv_map[v].vg_number = ABS_MAX_VG;
		vg_lv_map[v].lv_number = -1;
	}

	return;
}				/* lvm_init_vars () */


/********************************************************************
 *
 * Character device functions
 *
 ********************************************************************/

/*
 * character device open routine
 */
static int lvm_chr_open(struct inode *inode,
			struct file *file)
{
	int minor = MINOR(inode->i_rdev);

#ifdef DEBUG
	printk(KERN_DEBUG
	 "%s -- lvm_chr_open MINOR: %d  VG#: %d  mode: 0x%X  lock: %d\n",
	       lvm_name, minor, VG_CHR(minor), file->f_mode, lock);
#endif

	/* super user validation */
	if (!suser())
		return -EACCES;

	/* Group special file open */
	if (VG_CHR(minor) > MAX_VG)
		return -ENXIO;

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif

	lvm_chr_open_count++;
	return 0;
}				/* lvm_chr_open () */


/*
 * character device i/o-control routine
 *
 * Only one changing process can do ioctl at one time, others will block.
 *
 */
static int lvm_chr_ioctl(struct inode *inode, struct file *file,
			 uint command, ulong a)
{
	int minor = MINOR(inode->i_rdev);
	int extendable;
	ulong l, le, p, v;
	ulong size;
	void *arg = (void *) a;
#ifdef LVM_GET_INODE
	struct inode *inode_sav;
#endif
	lv_status_byname_req_t lv_status_byname_req;
	lv_status_byindex_req_t lv_status_byindex_req;
	lv_t lv;

	/* otherwise cc will complain about unused variables */
	(void) lvm_lock;


#ifdef DEBUG_IOCTL
	printk(KERN_DEBUG
	       "%s -- lvm_chr_ioctl: command: 0x%X  MINOR: %d  "
	       "VG#: %d  mode: 0x%X\n",
	       lvm_name, command, minor, VG_CHR(minor), file->f_mode);
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0)
		return -EACCES;
#endif


	/* Main command switch */
	switch (command) {
		/* lock the LVM */
	case LVM_LOCK_LVM:
	      lock_try_again:
		spin_lock(&lvm_lock);
		if (lock != 0 && lock != current->pid) {
#ifdef DEBUG_IOCTL
			printk(KERN_INFO "lvm_chr_ioctl: %s is locked by pid %d ...\n",
			       lvm_name, lock);
#endif
			spin_unlock(&lvm_lock);
			interruptible_sleep_on(&lvm_wait);
			if (current->sigpending != 0)
				return -EINTR;
#ifdef LVM_TOTAL_RESET
			if (lvm_reset_spindown > 0)
				return -EACCES;
#endif
			goto lock_try_again;
		}
		lock = current->pid;
		spin_unlock(&lvm_lock);
		return 0;


		/* check lvm version to ensure driver/tools+lib interoperability */
	case LVM_GET_IOP_VERSION:
		if (copy_to_user(arg, &lvm_iop_version, sizeof(ushort)) != 0)
			return -EFAULT;
		return 0;


#ifdef LVM_TOTAL_RESET
		/* lock reset function */
	case LVM_RESET:
		lvm_reset_spindown = 1;
		for (v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] != NULL) {
				do_vg_remove(v);
			}
		}

#ifdef MODULE
		while (GET_USE_COUNT(&__this_module) < 1)
			MOD_INC_USE_COUNT;
		while (GET_USE_COUNT(&__this_module) > 1)
			MOD_DEC_USE_COUNT;
#endif				/* MODULE */
		lock = 0;	/* release lock */
		wake_up_interruptible(&lvm_wait);
		return 0;
#endif				/* LVM_TOTAL_RESET */


		/* lock/unlock i/o to a physical extent to move it to another
		   physical volume (move's done in user space's pvmove) */
	case PE_LOCK_UNLOCK:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&pe_lock_req, arg, sizeof(pe_lock_req_t)) != 0)
			return -EFAULT;

		switch (pe_lock_req.lock) {
		case LOCK_PE:
			for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
				if (vg[VG_CHR(minor)]->pv[p] != NULL &&
				    pe_lock_req.data.pv_dev ==
				    vg[VG_CHR(minor)]->pv[p]->pv_dev)
					break;
			}

			if (p == vg[VG_CHR(minor)]->pv_max)
				return -ENXIO;

			pe_lock_req.lock = UNLOCK_PE;
			fsync_dev(pe_lock_req.data.lv_dev);
			pe_lock_req.lock = LOCK_PE;
			break;

		case UNLOCK_PE:
			pe_lock_req.lock = UNLOCK_PE;
			pe_lock_req.data.lv_dev = \
			    pe_lock_req.data.pv_dev = \
			    pe_lock_req.data.pv_offset = 0;
			wake_up(&lvm_map_wait);
			break;

		default:
			return -EINVAL;
		}

		return 0;


		/* remap a logical extent (after moving the physical extent) */
	case LE_REMAP:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&le_remap_req, arg,
				   sizeof(le_remap_req_t)) != 0)
			return -EFAULT;

		for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
			if (vg[VG_CHR(minor)]->lv[l] != NULL &&
			    lvm_strcmp(vg[VG_CHR(minor)]->lv[l]->lv_name,
				       le_remap_req.lv_name) == 0) {
				for (le = 0; le < vg[VG_CHR(minor)]->lv[l]->lv_allocated_le;
				     le++) {
					if (vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev ==
					    le_remap_req.old_dev &&
					    vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].pe ==
					    le_remap_req.old_pe) {
						vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev =
						    le_remap_req.new_dev;
						vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].pe =
						    le_remap_req.new_pe;
						return 0;
					}
				}
				return -EINVAL;
			}
		}

		return -ENXIO;


		/* create a VGDA */
	case VG_CREATE:
		return do_vg_create(minor, arg);


		/* remove an inactive VGDA */
	case VG_REMOVE:
		return do_vg_remove(minor);


		/* extend a volume group */
	case VG_EXTEND:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (vg[VG_CHR(minor)]->pv_cur < vg[VG_CHR(minor)]->pv_max) {
			for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
				if (vg[VG_CHR(minor)]->pv[p] == NULL) {
					if ((vg[VG_CHR(minor)]->pv[p] =
					vmalloc(sizeof(pv_t))) == NULL) {
						printk(KERN_CRIT
						       "%s -- VG_EXTEND: vmalloc error PV\n", lvm_name);
						return -ENOMEM;
					}
					if (copy_from_user(vg[VG_CHR(minor)]->pv[p], arg,
						      sizeof(pv_t)) != 0)
						return -EFAULT;

					vg[VG_CHR(minor)]->pv[p]->pv_status = PV_ACTIVE;
					/* We don't need the PE list
					   in kernel space like LVs pe_t list */
					vg[VG_CHR(minor)]->pv[p]->pe = NULL;
					vg[VG_CHR(minor)]->pv_cur++;
					vg[VG_CHR(minor)]->pv_act++;
					vg[VG_CHR(minor)]->pe_total +=
					    vg[VG_CHR(minor)]->pv[p]->pe_total;
#ifdef LVM_GET_INODE
					/* insert a dummy inode for fs_may_mount */
					vg[VG_CHR(minor)]->pv[p]->inode =
					    lvm_get_inode(vg[VG_CHR(minor)]->pv[p]->pv_dev);
#endif
					return 0;
				}
			}
		}
		return -EPERM;


		/* reduce a volume group */
	case VG_REDUCE:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(pv_name, arg, sizeof(pv_name)) != 0)
			return -EFAULT;

		for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
			if (vg[VG_CHR(minor)]->pv[p] != NULL &&
			    lvm_strcmp(vg[VG_CHR(minor)]->pv[p]->pv_name,
				       pv_name) == 0) {
				if (vg[VG_CHR(minor)]->pv[p]->lv_cur > 0)
					return -EPERM;
				vg[VG_CHR(minor)]->pe_total -=
				    vg[VG_CHR(minor)]->pv[p]->pe_total;
				vg[VG_CHR(minor)]->pv_cur--;
				vg[VG_CHR(minor)]->pv_act--;
#ifdef DEBUG_VFREE
				printk(KERN_DEBUG
				 "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
#ifdef LVM_GET_INODE
				clear_inode(vg[VG_CHR(minor)]->pv[p]->inode);
#endif
				vfree(vg[VG_CHR(minor)]->pv[p]);
				/* Make PV pointer array contiguous */
				for (; p < vg[VG_CHR(minor)]->pv_max - 1; p++)
					vg[VG_CHR(minor)]->pv[p] = vg[VG_CHR(minor)]->pv[p + 1];
				vg[VG_CHR(minor)]->pv[p + 1] = NULL;
				return 0;
			}
		}
		return -ENXIO;


		/* set/clear extendability flag of volume group */
	case VG_SET_EXTENDABLE:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&extendable, arg, sizeof(extendable)) != 0)
			return -EFAULT;

		if (extendable == VG_EXTENDABLE ||
		    extendable == ~VG_EXTENDABLE) {
			if (extendable == VG_EXTENDABLE)
				vg[VG_CHR(minor)]->vg_status |= VG_EXTENDABLE;
			else
				vg[VG_CHR(minor)]->vg_status &= ~VG_EXTENDABLE;
		} else
			return -EINVAL;
		return 0;


		/* get volume group data (only the vg_t struct) */
	case VG_STATUS:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_to_user(arg, vg[VG_CHR(minor)], sizeof(vg_t)) != 0)
			return -EFAULT;

		return 0;


		/* get volume group count */
	case VG_STATUS_GET_COUNT:
		if (copy_to_user(arg, &vg_count, sizeof(vg_count)) != 0)
			return -EFAULT;

		return 0;


		/* get volume group count */
	case VG_STATUS_GET_NAMELIST:
		for (l = v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] != NULL) {
				if (copy_to_user(arg + l++ * NAME_LEN,
						 vg[v]->vg_name,
						 NAME_LEN) != 0)
					return -EFAULT;
			}
		}
		return 0;


		/* create, remove, extend or reduce a logical volume */
	case LV_CREATE:
	case LV_REMOVE:
	case LV_EXTEND:
	case LV_REDUCE:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&lv_req, arg, sizeof(lv_req)) != 0)
			return -EFAULT;

		if (command != LV_REMOVE) {
			if (copy_from_user(&lv, lv_req.lv, sizeof(lv_t)) != 0)
				return -EFAULT;
		}
		switch (command) {
		case LV_CREATE:
			return do_lv_create(minor, lv_req.lv_name, &lv);

		case LV_REMOVE:
			return do_lv_remove(minor, lv_req.lv_name, -1);

		case LV_EXTEND:
		case LV_REDUCE:
			return do_lv_extend_reduce(minor, lv_req.lv_name, &lv);
		}


		/* get status of a logical volume by name */
	case LV_STATUS_BYNAME:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&lv_status_byname_req, arg,
				   sizeof(lv_status_byname_req_t)) != 0)
			return -EFAULT;

		if (lv_status_byname_req.lv == NULL)
			return -EINVAL;
		if (copy_from_user(&lv, lv_status_byname_req.lv,
				   sizeof(lv_t)) != 0)
			return -EFAULT;

		for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
			if (vg[VG_CHR(minor)]->lv[l] != NULL &&
			    lvm_strcmp(vg[VG_CHR(minor)]->lv[l]->lv_name,
				    lv_status_byname_req.lv_name) == 0) {
				if (copy_to_user(lv_status_byname_req.lv,
						 vg[VG_CHR(minor)]->lv[l],
						 sizeof(lv_t)) != 0)
					return -EFAULT;

				if (lv.lv_current_pe != NULL) {
					size = vg[VG_CHR(minor)]->lv[l]->lv_allocated_le *
					    sizeof(pe_t);
					if (copy_to_user(lv.lv_current_pe,
							 vg[VG_CHR(minor)]->lv[l]->lv_current_pe,
							 size) != 0)
						return -EFAULT;
				}
				if (lv.lv_exception != NULL &&
				    vg[VG_CHR(minor)]->lv[l]->lv_exception != NULL) {
					if (copy_to_user(lv.lv_exception,
							 vg[VG_CHR(minor)]->lv[l]->lv_exception,
					    sizeof(lv_exception_t)) != 0)
						return -EFAULT;
				}
				return 0;
			}
		}
		return -ENXIO;


		/* get status of a logical volume by index */
	case LV_STATUS_BYINDEX:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&lv_status_byindex_req, arg,
				   sizeof(lv_status_byindex_req)) != 0)
			return -EFAULT;

		if ((lvp = lv_status_byindex_req.lv) == NULL)
			return -EINVAL;
		l = lv_status_byindex_req.lv_index;
		if (vg[VG_CHR(minor)]->lv[l] == NULL)
			return -ENXIO;

		if (copy_from_user(&lv, lvp, sizeof(lv_t)) != 0)
			return -EFAULT;

		if (copy_to_user(lvp, vg[VG_CHR(minor)]->lv[l],
				 sizeof(lv_t)) != 0)
			return -EFAULT;

		if (lv.lv_current_pe != NULL) {
			size = vg[VG_CHR(minor)]->lv[l]->lv_allocated_le * sizeof(pe_t);
			if (copy_to_user(lv.lv_current_pe,
				 vg[VG_CHR(minor)]->lv[l]->lv_current_pe,
					 size) != 0)
				return -EFAULT;
		}
		if (lv.lv_exception != NULL &&
		    vg[VG_CHR(minor)]->lv[l]->lv_exception != NULL) {
			if (copy_to_user(lv.lv_exception,
				  vg[VG_CHR(minor)]->lv[l]->lv_exception,
					 sizeof(lv_exception_t)) != 0)
				return -EFAULT;
		}
		return 0;


		/* change a physical volume */
	case PV_CHANGE:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&pv_change_req, arg,
				   sizeof(pv_change_req)) != 0)
			return -EFAULT;

		for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
			if (vg[VG_CHR(minor)]->pv[p] != NULL &&
			    lvm_strcmp(vg[VG_CHR(minor)]->pv[p]->pv_name,
				       pv_change_req.pv_name) == 0) {
#ifdef LVM_GET_INODE
				inode_sav = vg[VG_CHR(minor)]->pv[p]->inode;
#endif
				if (copy_from_user(vg[VG_CHR(minor)]->pv[p],
						   pv_change_req.pv,
						   sizeof(pv_t)) != 0)
					return -EFAULT;

				/* We don't need the PE list
				   in kernel space as with LVs pe_t list */
				vg[VG_CHR(minor)]->pv[p]->pe = NULL;
#ifdef LVM_GET_INODE
				vg[VG_CHR(minor)]->pv[p]->inode = inode_sav;
#endif
				return 0;
			}
		}
		return -ENXIO;


		/* get physical volume data (pv_t structure only) */
	case PV_STATUS:
		if (vg[VG_CHR(minor)] == NULL)
			return -ENXIO;
		if (copy_from_user(&pv_status_req, arg,
				   sizeof(pv_status_req)) != 0)
			return -EFAULT;

		for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
			if (vg[VG_CHR(minor)]->pv[p] != NULL) {
				if (lvm_strcmp(vg[VG_CHR(minor)]->pv[p]->pv_name,
					   pv_status_req.pv_name) == 0) {
					if (copy_to_user(pv_status_req.pv,
						vg[VG_CHR(minor)]->pv[p],
						      sizeof(pv_t)) != 0)
						return -EFAULT;
					return 0;
				}
			}
		}
		return -ENXIO;


		/* physical volume buffer flush/invalidate */
	case PV_FLUSH:
		if (copy_from_user(&pv_flush_req, arg, sizeof(pv_flush_req)) != 0)
			return -EFAULT;

		for (v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] == NULL)
				continue;
			for (p = 0; p < vg[v]->pv_max; p++) {
				if (vg[v]->pv[p] != NULL &&
				    lvm_strcmp(vg[v]->pv[p]->pv_name,
					    pv_flush_req.pv_name) == 0) {
					fsync_dev(vg[v]->pv[p]->pv_dev);
					invalidate_buffers(vg[v]->pv[p]->pv_dev);
					return 0;
				}
			}
		}
		return 0;


	default:
		printk(KERN_WARNING
		       "%s -- lvm_chr_ioctl: unknown command %d\n",
		       lvm_name, command);
		return -EINVAL;
	}

	return 0;
}				/* lvm_chr_ioctl */


/*
 * character device close routine
 */
static int lvm_chr_close(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	int minor = MINOR(inode->i_rdev);
	printk(KERN_DEBUG
	     "%s -- lvm_chr_close   VG#: %d\n", lvm_name, VG_CHR(minor));
#endif

#ifdef MODULE
	if (GET_USE_COUNT(&__this_module) > 0)
		MOD_DEC_USE_COUNT;
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0) {
		lvm_reset_spindown = 0;
		lvm_chr_open_count = 1;
	}
#endif

	if (lvm_chr_open_count > 0)
		lvm_chr_open_count--;
	if (lock == current->pid) {
		lock = 0;	/* release lock */
		wake_up_interruptible(&lvm_wait);
	}
	return 0;
}				/* lvm_chr_close () */



/********************************************************************
 *
 * Block device functions
 *
 ********************************************************************/

/*
 * block device open routine
 */
static int lvm_blk_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);

#ifdef DEBUG_LVM_BLK_OPEN
	printk(KERN_DEBUG
	  "%s -- lvm_blk_open MINOR: %d  VG#: %d  LV#: %d  mode: 0x%X\n",
	    lvm_name, minor, VG_BLK(minor), LV_BLK(minor), file->f_mode);
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0)
		return -EPERM;
#endif

	if (vg[VG_BLK(minor)] != NULL &&
	    (vg[VG_BLK(minor)]->vg_status & VG_ACTIVE) &&
	    vg[VG_BLK(minor)]->lv[LV_BLK(minor)] != NULL &&
	    LV_BLK(minor) >= 0 &&
	    LV_BLK(minor) < vg[VG_BLK(minor)]->lv_max) {

		/* Check parallel LV spindown (LV remove) */
		if (vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_status & LV_SPINDOWN)
			return -EPERM;

		/* Check inactive LV and open for read/write */
		if (file->f_mode & O_RDWR) {
			if (!(vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_status & LV_ACTIVE))
				return -EPERM;
			if (!(vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_access & LV_WRITE))
				return -EACCES;
		}
		if (vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_open == 0)
			vg[VG_BLK(minor)]->lv_open++;
		vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_open++;

#ifdef MODULE
		MOD_INC_USE_COUNT;
#endif

#ifdef DEBUG_LVM_BLK_OPEN
		printk(KERN_DEBUG
		       "%s -- lvm_blk_open MINOR: %d  VG#: %d  LV#: %d  size: %d\n",
		       lvm_name, minor, VG_BLK(minor), LV_BLK(minor),
		       vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_size);
#endif

		return 0;
	}
	return -ENXIO;
}				/* lvm_blk_open () */


/*
 * block device read
 */
static ssize_t lvm_blk_read(struct file *file, char *buffer,
			    size_t size, loff_t * offset)
{
	int major = MAJOR(file->f_dentry->d_inode->i_rdev);
	int minor = MINOR(file->f_dentry->d_inode->i_rdev);

	read_ahead[major] = vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_read_ahead;
	return block_read(file, buffer, size, offset);
}


/*
 * block device write
 */
static ssize_t lvm_blk_write(struct file *file, const char *buffer,
			     size_t size, loff_t * offset)
{
	int major = MAJOR(file->f_dentry->d_inode->i_rdev);
	int minor = MINOR(file->f_dentry->d_inode->i_rdev);

	read_ahead[major] = vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_read_ahead;
	return block_write(file, buffer, size, offset);
}


/*
 * block device i/o-control routine
 */
static int lvm_blk_ioctl(struct inode *inode, struct file *file,
			 uint command, ulong a)
{
	int minor = MINOR(inode->i_rdev);
	void *arg = (void *) a;
	struct hd_geometry *hd = (struct hd_geometry *) a;

#ifdef DEBUG_IOCTL
	printk(KERN_DEBUG
	       "%s -- lvm_blk_ioctl MINOR: %d  command: 0x%X  arg: %X  "
	       "VG#: %dl  LV#: %d\n",
	       lvm_name, minor, command, (ulong) arg,
	       VG_BLK(minor), LV_BLK(minor));
#endif

	switch (command) {
		/* return device size */
	case BLKGETSIZE:
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKGETSIZE: %u\n",
		lvm_name, vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_size);
#endif
		copy_to_user((long *) arg, &vg[VG_BLK(minor)]-> \
			     lv[LV_BLK(minor)]->lv_size,
			     sizeof(vg[VG_BLK(minor)]-> \
				    lv[LV_BLK(minor)]->lv_size));
		break;


		/* flush buffer cache */
	case BLKFLSBUF:
		/* super user validation */
		if (!suser())
			return -EACCES;

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKFLSBUF\n", lvm_name);
#endif
		fsync_dev(inode->i_rdev);
		break;


		/* set read ahead for block device */
	case BLKRASET:
		/* super user validation */
		if (!suser())
			return -EACCES;

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKRASET: %d sectors for %02X:%02X\n",
		       lvm_name, (long) arg, MAJOR(inode->i_rdev), minor);
#endif
		if ((long) arg < LVM_MIN_READ_AHEAD ||
		    (long) arg > LVM_MAX_READ_AHEAD)
			return -EINVAL;
		vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_read_ahead = (long) arg;
		break;


		/* get current read ahead setting */
	case BLKRAGET:
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKRAGET\n", lvm_name);
#endif
		copy_to_user((long *) arg,
		    &vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_read_ahead,
			  sizeof(vg[VG_BLK(minor)]->lv[LV_BLK(minor)]-> \
				 lv_read_ahead));
		break;


		/* get disk geometry */
	case HDIO_GETGEO:
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- HDIO_GETGEO\n", lvm_name);
#endif
		if (hd == NULL)
			return -EINVAL;
		{
			unsigned char heads = 64;
			unsigned char sectors = 32;
			long start = 0;
			short cylinders = vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_size /
			heads / sectors;

			if (copy_to_user((char *) &hd->heads, &heads,
					 sizeof(heads)) != 0 ||
			    copy_to_user((char *) &hd->sectors, &sectors,
					 sizeof(sectors)) != 0 ||
			    copy_to_user((short *) &hd->cylinders,
				   &cylinders, sizeof(cylinders)) != 0 ||
			    copy_to_user((long *) &hd->start, &start,
					 sizeof(start)) != 0)
				return -EFAULT;
		}

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- cylinders: %d\n",
		       lvm_name, vg[VG_BLK(minor)]->lv[LV_BLK(minor)]-> \
		       lv_size / heads / sectors);
#endif
		break;


		/* set access flags of a logical volume */
	case LV_SET_ACCESS:
		/* super user validation */
		if (!suser())
			return -EACCES;
		vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_access = (ulong) arg;
		break;


		/* set status flags of a logical volume */
	case LV_SET_STATUS:
		/* super user validation */
		if (!suser())
			return -EACCES;
		if (!((ulong) arg & LV_ACTIVE) &&
		    vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_open > 1)
			return -EPERM;
		vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_status = (ulong) arg;
		break;


		/* set allocation flags of a logical volume */
	case LV_SET_ALLOCATION:
		/* super user validation */
		if (!suser())
			return -EACCES;
		vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_allocation = (ulong) arg;
		break;


	default:
		printk(KERN_WARNING
		       "%s -- lvm_blk_ioctl: unknown command %d\n",
		       lvm_name, command);
		return -EINVAL;
	}

	return 0;
}				/* lvm_blk_ioctl () */


/*
 * block device close routine
 */
static int lvm_blk_close(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);

#ifdef DEBUG
	printk(KERN_DEBUG
	       "%s -- lvm_blk_close MINOR: %d  VG#: %d  LV#: %d\n",
	       lvm_name, minor, VG_BLK(minor), LV_BLK(minor));
#endif

	sync_dev(inode->i_rdev);
	if (vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_open == 1)
		vg[VG_BLK(minor)]->lv_open--;
	vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_open--;

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif

	return 0;
}				/* lvm_blk_close () */


#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
/*
 * Support function /proc-Filesystem
 */
#define  LVM_PROC_BUF   ( i == 0 ? dummy_buf : &buf[sz])

static int lvm_proc_get_info(char *page, char **start, off_t pos,
			     int count, int whence)
{
	int c, i, l, p, v, vg_counter, pv_counter, lv_counter, lv_open_counter,
	 lv_open_total, pe_t_bytes, seconds;
	static off_t sz;
	off_t sz_last;
	char allocation_flag, inactive_flag, rw_flag, stripes_flag;
	char *lv_name = NULL;
	static char *buf = NULL;
	static char dummy_buf[160];	/* sized for 2 lines */

#ifdef DEBUG_LVM_PROC_GET_INFO
	printk(KERN_DEBUG
	       "%s - lvm_proc_get_info CALLED  pos: %lu  count: %d  whence: %d\n",
	       lvm_name, pos, count, whence);
#endif

	if (pos == 0 || buf == NULL) {
		sz_last = vg_counter = pv_counter = lv_counter = \
		    lv_open_counter = lv_open_total = pe_t_bytes = 0;

		/* search for activity */
		for (v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] != NULL) {
				vg_counter++;
				pv_counter += vg[v]->pv_cur;
				lv_counter += vg[v]->lv_cur;
				if (vg[v]->lv_cur > 0) {
					for (l = 0; l < vg[v]->lv_max; l++) {
						if (vg[v]->lv[l] != NULL) {
							pe_t_bytes += vg[v]->lv[l]->lv_allocated_le;
							if (vg[v]->lv[l]->lv_open > 0) {
								lv_open_counter++;
								lv_open_total += vg[v]->lv[l]->lv_open;
							}
						}
					}
				}
			}
		}
		pe_t_bytes *= sizeof(pe_t);

		if (buf != NULL) {
#ifdef DEBUG_VFREE
			printk(KERN_DEBUG
			       "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
			vfree(buf);
			buf = NULL;
		}
		/* 2 times: first to get size to allocate buffer,
		   2nd to fill the vmalloced buffer */
		for (i = 0; i < 2; i++) {
			sz = 0;
			sz += sprintf(LVM_PROC_BUF,
				      "LVM "
#ifdef MODULE
				      "module"
#else
				      "driver"
#endif
				      " %s\n\n"
				    "Total:  %d VG%s  %d PV%s  %d LV%s ",
				      lvm_short_version,
				  vg_counter, vg_counter == 1 ? "" : "s",
				  pv_counter, pv_counter == 1 ? "" : "s",
				 lv_counter, lv_counter == 1 ? "" : "s");
			sz += sprintf(LVM_PROC_BUF,
				      "(%d LV%s open",
				      lv_open_counter,
				      lv_open_counter == 1 ? "" : "s");
			if (lv_open_total > 0)
				sz += sprintf(LVM_PROC_BUF,
					      " %d times)\n",
					      lv_open_total);
			else
				sz += sprintf(LVM_PROC_BUF, ")");
			sz += sprintf(LVM_PROC_BUF,
				      "\nGlobal: %lu bytes vmalloced   IOP version: %d   ",
				      vg_counter * sizeof(vg_t) +
				      pv_counter * sizeof(pv_t) +
				      lv_counter * sizeof(lv_t) +
				      pe_t_bytes + sz_last,
				      lvm_iop_version);

			seconds = CURRENT_TIME - loadtime;
			if (seconds < 0)
				loadtime = CURRENT_TIME + seconds;
			if (seconds / 86400 > 0) {
				sz += sprintf(LVM_PROC_BUF, "%d day%s ",
					      seconds / 86400,
					      seconds / 86400 == 0 ||
					 seconds / 86400 > 1 ? "s" : "");
			}
			sz += sprintf(LVM_PROC_BUF, "%d:%02d:%02d active\n",
				      (seconds % 86400) / 3600,
				      (seconds % 3600) / 60,
				      seconds % 60);

			if (vg_counter > 0) {
				for (v = 0; v < ABS_MAX_VG; v++) {
					/* volume group */
					if (vg[v] != NULL) {
						inactive_flag = ' ';
						if (!(vg[v]->vg_status & VG_ACTIVE))
							inactive_flag = 'I';
						sz += sprintf(LVM_PROC_BUF,
							      "\nVG: %c%s  [%d PV, %d LV/%d open] "
						      " PE Size: %d KB\n"
							      "  Usage [KB/PE]: %d /%d total  "
							      "%d /%d used  %d /%d free",
							   inactive_flag,
							  vg[v]->vg_name,
							   vg[v]->pv_cur,
							   vg[v]->lv_cur,
							  vg[v]->lv_open,
						     vg[v]->pe_size >> 1,
							      vg[v]->pe_size * vg[v]->pe_total >> 1,
							 vg[v]->pe_total,
							      vg[v]->pe_allocated * vg[v]->pe_size >> 1,
						     vg[v]->pe_allocated,
							      (vg[v]->pe_total - vg[v]->pe_allocated) *
						     vg[v]->pe_size >> 1,
							      vg[v]->pe_total - vg[v]->pe_allocated);

						/* physical volumes */
						sz += sprintf(LVM_PROC_BUF,
							      "\n  PV%s ",
							      vg[v]->pv_cur == 1 ? ": " : "s:");
						c = 0;
						for (p = 0; p < vg[v]->pv_max; p++) {
							if (vg[v]->pv[p] != NULL) {
								inactive_flag = 'A';
								if (!(vg[v]->pv[p]->pv_status & PV_ACTIVE))
									inactive_flag = 'I';
								allocation_flag = 'A';
								if (!(vg[v]->pv[p]->pv_allocatable & PV_ALLOCATABLE))
									allocation_flag = 'N';
								sz += sprintf(LVM_PROC_BUF,
									      "[%c%c] %-21s %8d /%-6d  "
									      "%8d /%-6d  %8d /%-6d",
									      inactive_flag,
									      allocation_flag,
									      vg[v]->pv[p]->pv_name,
									      vg[v]->pv[p]->pe_total *
									      vg[v]->pv[p]->pe_size >> 1,
									      vg[v]->pv[p]->pe_total,
									      vg[v]->pv[p]->pe_allocated *
									      vg[v]->pv[p]->pe_size >> 1,
									      vg[v]->pv[p]->pe_allocated,
									      (vg[v]->pv[p]->pe_total -
									       vg[v]->pv[p]->pe_allocated) *
									      vg[v]->pv[p]->pe_size >> 1,
									      vg[v]->pv[p]->pe_total -
									      vg[v]->pv[p]->pe_allocated);
								c++;
								if (c < vg[v]->pv_cur)
									sz += sprintf(LVM_PROC_BUF,
										      "\n       ");
							}
						}

						/* logical volumes */
						sz += sprintf(LVM_PROC_BUF,
							   "\n    LV%s ",
							      vg[v]->lv_cur == 1 ? ": " : "s:");
						c = 0;
						for (l = 0; l < vg[v]->lv_max; l++) {
							if (vg[v]->lv[l] != NULL) {
								inactive_flag = 'A';
								if (!(vg[v]->lv[l]->lv_status & LV_ACTIVE))
									inactive_flag = 'I';
								rw_flag = 'R';
								if (vg[v]->lv[l]->lv_access & LV_WRITE)
									rw_flag = 'W';
								allocation_flag = 'D';
								if (vg[v]->lv[l]->lv_allocation & LV_CONTIGUOUS)
									allocation_flag = 'C';
								stripes_flag = 'L';
								if (vg[v]->lv[l]->lv_stripes > 1)
									stripes_flag = 'S';
								sz += sprintf(LVM_PROC_BUF,
									      "[%c%c%c%c",
									      inactive_flag,
								 rw_flag,
									      allocation_flag,
									      stripes_flag);
								if (vg[v]->lv[l]->lv_stripes > 1)
									sz += sprintf(LVM_PROC_BUF, "%-2d",
										      vg[v]->lv[l]->lv_stripes);
								else
									sz += sprintf(LVM_PROC_BUF, "  ");
								lv_name = lvm_strrchr(vg[v]->lv[l]->lv_name, '/');
								if (lv_name != NULL)
									lv_name++;
								else
									lv_name = vg[v]->lv[l]->lv_name;
								sz += sprintf(LVM_PROC_BUF, "] %-25s", lv_name);
								if (lvm_strlen(lv_name) > 25)
									sz += sprintf(LVM_PROC_BUF,
										      "\n                              ");
								sz += sprintf(LVM_PROC_BUF, "%9d /%-6d   ",
									      vg[v]->lv[l]->lv_size >> 1,
									      vg[v]->lv[l]->lv_size / vg[v]->pe_size);

								if (vg[v]->lv[l]->lv_open == 0)
									sz += sprintf(LVM_PROC_BUF, "close");
								else
									sz += sprintf(LVM_PROC_BUF, "%dx open",
										      vg[v]->lv[l]->lv_open);
								c++;
								if (c < vg[v]->lv_cur)
									sz += sprintf(LVM_PROC_BUF,
										      "\n         ");
							}
						}
						if (vg[v]->lv_cur == 0)
							sz += sprintf(LVM_PROC_BUF, "none");
						sz += sprintf(LVM_PROC_BUF, "\n");
					}
				}
			}
			if (buf == NULL) {
				if ((buf = vmalloc(sz)) == NULL) {
					sz = 0;
					return sprintf(page, "%s - vmalloc error at line %d\n",
						     lvm_name, __LINE__);
				}
			}
			sz_last = sz;
		}
	}
	if (pos > sz - 1) {
		vfree(buf);
		buf = NULL;
		return 0;
	}
	*start = &buf[pos];
	if (sz - pos < count)
		return sz - pos;
	else
		return count;
}				/* lvm_proc_get_info () */
#endif				/* #if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS */


/*
 * block device support function for /usr/src/linux/drivers/block/ll_rw_blk.c
 * (see init_module/lvm_init)
 */
static int lvm_map(int minor, kdev_t * rdev,
		   ulong * rsector, ulong size, int rw)
{
	ulong index;
	ulong rsector_sav;
	kdev_t rdev_sav;
	lv_t *lv = vg[VG_BLK(minor)]->lv[LV_BLK(minor)];

	if (!(lv->lv_status & LV_ACTIVE)) {
		printk(KERN_ALERT
		       "%s - lvm_map: ll_rw_blk for inactive LV %s\n",
		       lvm_name, lv->lv_name);
		return -1;
	}
#ifdef WRITEA
	if ((rw == WRITE || rw == WRITEA) &&
#else
	if (rw == WRITE &&
#endif
	    !(lv->lv_access & LV_WRITE)) {
		printk(KERN_CRIT
		    "%s - lvm_map: ll_rw_blk write for readonly LV %s\n",
		       lvm_name, lv->lv_name);
		return -1;
	}
#ifdef DEBUG_MAP_SIZE
	if (size != 2)
		printk(KERN_DEBUG
		"%s - lvm_map minor:%d size:%lu ", lvm_name, minor, size);
#endif

#ifdef DEBUG_MAP
	printk(KERN_DEBUG
	       "%s - lvm_map minor:%d  *rdev: %02d:%02d  *rsector: %lu  "
	       "size:%lu\n",
	       lvm_name, minor,
	       MAJOR(*rdev),
	       MINOR(*rdev),
	       *rsector, size);
#endif

	if (*rsector + size > lv->lv_size) {
		printk(KERN_ALERT
		   "%s - lvm_map *rsector: %lu + size: %lu too large for"
		       " minor: %2d\n", lvm_name, *rsector, size, minor);
		return -1;
	}
	rsector_sav = *rsector;
	rdev_sav = *rdev;

      lvm_second_remap:

	/* linear mapping */
	if (lv->lv_stripes < 2) {
		index = *rsector / vg[VG_BLK(minor)]->pe_size;	/* get the index */
		*rsector = lv->lv_current_pe[index].pe +
		    (*rsector % vg[VG_BLK(minor)]->pe_size);
		*rdev = lv->lv_current_pe[index].dev;

#ifdef DEBUG_MAP
		printk(KERN_DEBUG
		       "lv_current_pe[%ld].pe: %ld  rdev: %02d:%02d  rsector:%ld\n",
		       index,
		       lv->lv_current_pe[index].pe,
		       MAJOR(*rdev),
		       MINOR(*rdev),
		       *rsector);
#endif

		/* striped mapping */
	} else {
		ulong stripe_index;
		ulong stripe_length;

		stripe_length = vg[VG_BLK(minor)]->pe_size * lv->lv_stripes;
		stripe_index = (*rsector % stripe_length) / lv->lv_stripesize;
		index = *rsector / stripe_length +
		    (stripe_index % lv->lv_stripes) *
		    (lv->lv_allocated_le / lv->lv_stripes);
		*rsector = lv->lv_current_pe[index].pe +
		    (*rsector % stripe_length) -
		    (stripe_index % lv->lv_stripes) * lv->lv_stripesize -
		    stripe_index / lv->lv_stripes *
		    (lv->lv_stripes - 1) * lv->lv_stripesize;
		*rdev = lv->lv_current_pe[index].dev;
	}

#ifdef DEBUG_MAP
	printk(KERN_DEBUG
	     "lv_current_pe[%ld].pe: %ld  rdev: %02d:%02d  rsector:%ld\n"
	       "stripe_length: %ld  stripe_index: %ld\n",
	       index,
	       lv->lv_current_pe[index].pe,
	       MAJOR(*rdev),
	       MINOR(*rdev),
	       *rsector,
	       stripe_length,
	       stripe_index);
#endif

	/* handle physical extents on the move */
	if (pe_lock_req.lock == LOCK_PE) {
		if (*rdev == pe_lock_req.data.pv_dev &&
		    *rsector >= pe_lock_req.data.pv_offset &&
		    *rsector < (pe_lock_req.data.pv_offset +
				vg[VG_BLK(minor)]->pe_size)) {
			sleep_on(&lvm_map_wait);
			*rsector = rsector_sav;
			*rdev = rsector_sav;
			goto lvm_second_remap;
		}
	}
	/* statistic */
#ifdef WRITEA
	if (rw == WRITE || rw == WRITEA)
#else
	if (rw == WRITE)
#endif
		lv->lv_current_pe[index].writes++;
	else
		lv->lv_current_pe[index].reads++;

	/* snapshot volume exception handling on physical address base */
	if (lv->lv_exception != NULL) {
		spin_lock(&lv->lv_exception->lv_snapshot_lock);
		if (lv->lv_exception->lv_remap_ptr <= lv->lv_exception->lv_remap_end) {
			if (lv->lv_access & LV_SNAPSHOT_ORG) {
				/* for write, check if it is neccessary to
				   create a new remapped block */
#ifdef WRITEA
				if (rw == WRITE || rw == WRITEA)
#else
				if (rw == WRITE)
#endif
				{
					rdev_sav = *rdev;
					rsector_sav = *rsector;
					if (lvm_snapshot_remap_block(rsector, rdev, minor) == FALSE) {
						/* create a new mapping */
						lvm_snapshot_remap_new_block(rsector, rdev, minor);
					}
					*rdev = rdev_sav;
					*rsector = rsector_sav;
				}
			} else
				lvm_snapshot_remap_block(rsector, rdev, minor);
		}
		spin_unlock(&lv->lv_exception->lv_snapshot_lock);
	}
	return 0;
}				/* lvm_map () */


/*
 * lvm_map snapshot logical volume support functions
 */


/*
 * figure out physical sector/rdev pair remapping
 */
static int lvm_snapshot_remap_block(ulong * rsector, kdev_t * rdev, int minor)
{
	lv_t *lv = vg[VG_BLK(minor)]->lv[LV_BLK(minor)];
	lv_t *lv_org, *lv_snapshot;
	lv_block_exception_t *lv_block_exception =
	lv->lv_exception->lv_block_exception;
	int i;
	int lv_remap_ptr = lv->lv_exception->lv_remap_ptr;
	int lv_remap_end = lv->lv_exception->lv_remap_end;
	kdev_t last_dev;


	if (lv_remap_ptr < lv_remap_end) {
		for (i = 0; i < lv_remap_ptr; i++) {
			if (*rsector >= lv_block_exception[i].rsector_org &&
			    *rsector < lv_block_exception[i].rsector_org +
			    lv->lv_exception->lv_chunk_size &&
			    *rdev == lv_block_exception[i].rdev_org) {
				*rdev = lv_block_exception[i].rdev_new;
				*rsector = lv_block_exception[i].rsector_new +
				    (*rsector % lv->lv_exception->lv_chunk_size);
				return TRUE;
			}
		}
		return FALSE;
	}
	/* give up, if we don't have capacity left */
	lv->lv_exception->lv_remap_ptr = lv->lv_exception->lv_remap_end + 1;
	spin_unlock(&lv->lv_exception->lv_snapshot_lock);

	if (lv->lv_access & LV_SNAPSHOT_ORG) {
		lv_org = lv;
		lv_snapshot = vg[VG_BLK(minor)]->lv[LV_BLK(lv->lv_snapshot_minor)];
	} else {
		lv_org = vg[VG_BLK(minor)]->lv[LV_BLK(lv->lv_snapshot_minor)];
		lv_snapshot = lv;
	}

	fsync_dev(lv_org->lv_dev);
	invalidate_buffers(lv_snapshot->lv_dev);

	last_dev = 0;
	for (i = 0; i < lv_remap_end; i++) {
		if (lv_block_exception[i].rdev_new != last_dev) {
			fsync_dev(lv_block_exception[i].rdev_new);
			invalidate_buffers(lv_block_exception[i].rdev_new);
			last_dev = lv_block_exception[i].rdev_new;
		}
	}

	printk(KERN_INFO
	       "%s -- giving up to snapshot %s on %s\n",
	       lvm_name, lv_org->lv_name, lv_snapshot->lv_name);

	return TRUE;
}



/*
 * copy on write handler for snapshot logical volumes
 *
 * get new block(s), read the original one(s), store it on the new one(s)
 *
 */
static int lvm_snapshot_remap_new_block(ulong * rsector,
					kdev_t * rdev,
					int minor)
{
	lv_t *lv = vg[VG_BLK(minor)]->lv[LV_BLK(minor)];
	lv_block_exception_t *lv_block_exception =
	lv->lv_exception->lv_block_exception;
	int end, error, i;
	int lv_remap_ptr = lv->lv_exception->lv_remap_ptr;
	ulong rblock_start;
	static struct buffer_head *bh_list[LVM_SNAPSHOT_MAX_CHUNK],
	*bh_new[LVM_SNAPSHOT_MAX_CHUNK];


	if (lv_remap_ptr < lv->lv_exception->lv_remap_end) {
		lv->lv_exception->lv_remap_ptr++;

		rblock_start = *rsector >> 1;
		rblock_start -= rblock_start % (lv->lv_exception->lv_chunk_size >> 1);
		end = lv->lv_exception->lv_chunk_size / (lvm_blocksizes[minor] >> 9);
		lv_block_exception[lv_remap_ptr].rdev_org = *rdev;
		lv_block_exception[lv_remap_ptr].rsector_org = rblock_start << 1;

		for (i = 0; i < end; i++) {
			if ((bh_list[i] = getblk(*rdev,
						 rblock_start +
				       i * (lvm_blocksizes[minor] >> 10),
				       lvm_blocksizes[minor])) == NULL) {
				printk(KERN_CRIT
				       "%s -- oops, unable to getblk %d from block "
				       "%lu for %02d:%02d\n",
				       lvm_name, lvm_blocksizes[minor],
				       rblock_start + i * (lvm_blocksizes[minor] >> 10),
				       MAJOR(*rdev), MINOR(*rdev));
				for (i--; i >= 0; i--)
					brelse(bh_list[i]);
				goto lvm_snapshot_remap_new_block_error;
			}
		}

		ll_rw_block(READ, end, bh_list);

		for (error = i = 0; i < end; i++) {
			if (!buffer_uptodate)
				wait_on_buffer(bh_list[i]);
			if (!buffer_uptodate(bh_list[i])) {
				printk(KERN_CRIT
				       "%s -- oops, buffer %d (b_rdev: %02d:%02d  "
				       "b_rsector: %ld) not uptodate at line %d\n",
				       lvm_name, i,
				       MAJOR(bh_list[i]->b_rdev),
				       MINOR(bh_list[i]->b_rdev),
				       bh_list[i]->b_rsector,
				       __LINE__);
				error++;
			}
			if (error > 0) {
				for (i = 0; i < end; i++) {
					printk(KERN_CRIT
					       "%s -- releasing buffer %d (b_rdev: %02d:%02d  "
					       "b_rsector: %ld) with b_count: %d for lv_remap_ptr: %d\n",
					       lvm_name, i,
					       MAJOR(bh_list[i]->b_rdev), MINOR(bh_list[i]->b_rdev),
					       bh_list[i]->b_rsector, bh_list[i]->b_count,
					       lv_remap_ptr);

					brelse(bh_list[i]);
				}
				goto lvm_snapshot_remap_new_block_error;
			}
		}


		/* remap exception block */
		*rdev = lv_block_exception[lv_remap_ptr].rdev_new;
		*rsector = lv_block_exception[lv_remap_ptr].rsector_new +
		    (*rsector % lv->lv_exception->lv_chunk_size);

		rblock_start = lv_block_exception[lv_remap_ptr].rsector_new >> 1;

		for (i = 0; i < end; i++) {
			if ((bh_new[i] = getblk(*rdev, rblock_start +
				       i * (lvm_blocksizes[minor] >> 10),
				       lvm_blocksizes[minor])) == NULL) {
				printk(KERN_CRIT
				       "%s -- oops, unable to getblk %d for block "
				       "%lu from %02d:%02d\n",
				       lvm_name, lvm_blocksizes[minor],
				       rblock_start + i * (lvm_blocksizes[minor] >> 10),
				       MAJOR(*rdev), MINOR(*rdev));
				for (--i; i >= 0; i--)
					brelse(bh_new[i]);
				for (i = 0; i < end; i++)
					brelse(bh_list[i]);
				goto lvm_snapshot_remap_new_block_error;
			}
		}

		/* Copy data */
		for (i = 0; i < end; i++) {
			lvm_memcpy(bh_new[i]->b_data, bh_list[i]->b_data, bh_new[i]->b_size);
			mark_buffer_dirty(bh_new[i], 0);
			if (bh_new[i]->b_count > 0)
				brelse(bh_new[i]);
			if (bh_list[i]->b_count > 0)
				brelse(bh_list[i]);
		}

		return TRUE;
	}
	return FALSE;

      lvm_snapshot_remap_new_block_error:
	/* end criteria for lvm_map() */
	lv->lv_exception->lv_remap_ptr = lv->lv_exception->lv_remap_end + 1;
	printk(KERN_CRIT
	       "%s -- I/O ERROR during snapshot\n", lvm_name);
	lvm_snapshot_remap_block(rsector, rdev, minor);
	return FALSE;
}
/*
 * end lvm_map snapshot logical volume support functions
 */


/*
 * internal support functions
 */

#ifdef LVM_HD_NAME
/*
 * generate "hard disk" name
 */
void lvm_hd_name(char *buf, int minor)
{
	int len = 0;

	if (vg[VG_BLK(minor)] == NULL ||
	    vg[VG_BLK(minor)]->lv[LV_BLK(minor)] == NULL)
		return;
	len = lvm_strlen(vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_name) - 5;
	lvm_memcpy(buf, &vg[VG_BLK(minor)]->lv[LV_BLK(minor)]->lv_name[5], len);
	buf[len] = 0;
	return;
}
#endif


/*
 * this one never should be called...
 */
static void lvm_dummy_device_request(void)
{
	printk(KERN_EMERG
	       "%s -- oops, got lvm request?\n", lvm_name);
	return;
}


/*
 * character device support function VGDA create
 */
int do_vg_create(int minor, void *arg)
{
	int snaporg_minor;
	ulong l, p;
	lv_t lv;

	if (vg[VG_CHR(minor)] != NULL)
		return -EPERM;

	if ((vg[VG_CHR(minor)] = vmalloc(sizeof(vg_t))) == NULL) {
		printk(KERN_CRIT
		       "%s -- VG_CREATE: vmalloc error VG\n", lvm_name);
		return -ENOMEM;
	}
	/* get the volume group structure */
	if (copy_from_user(vg[VG_CHR(minor)], arg, sizeof(vg_t)) != 0) {
		vfree(vg[VG_CHR(minor)]);
		vg[VG_CHR(minor)] = NULL;
		return -EFAULT;
	}
	/* we are not that active so far... */
	vg[VG_CHR(minor)]->vg_status &= ~VG_ACTIVE;

	vg[VG_CHR(minor)]->pe_allocated = 0;
	if (vg[VG_CHR(minor)]->pv_max > ABS_MAX_PV) {
		printk(KERN_WARNING
		       "%s -- Can't activate VG: ABS_MAX_PV too small\n",
		       lvm_name);
		vfree(vg[VG_CHR(minor)]);
		vg[VG_CHR(minor)] = NULL;
		return -EPERM;
	}
	if (vg[VG_CHR(minor)]->lv_max > ABS_MAX_LV) {
		printk(KERN_WARNING
		"%s -- Can't activate VG: ABS_MAX_LV too small for %u\n",
		       lvm_name, vg[VG_CHR(minor)]->lv_max);
		vfree(vg[VG_CHR(minor)]);
		vg[VG_CHR(minor)] = NULL;
		return -EPERM;
	}
	/* get the physical volume structures */
	vg[VG_CHR(minor)]->pv_act = vg[VG_CHR(minor)]->pv_cur = 0;
	for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
		/* user space address */
		if ((pvp = vg[VG_CHR(minor)]->pv[p]) != NULL) {
			vg[VG_CHR(minor)]->pv[p] = vmalloc(sizeof(pv_t));
			if (vg[VG_CHR(minor)]->pv[p] == NULL) {
				printk(KERN_CRIT
				       "%s -- VG_CREATE: vmalloc error PV\n", lvm_name);
				do_vg_remove(minor);
				return -ENOMEM;
			}
			if (copy_from_user(vg[VG_CHR(minor)]->pv[p], pvp,
					   sizeof(pv_t)) != 0) {
				do_vg_remove(minor);
				return -EFAULT;
			}
			/* We don't need the PE list
			   in kernel space as with LVs pe_t list (see below) */
			vg[VG_CHR(minor)]->pv[p]->pe = NULL;
			vg[VG_CHR(minor)]->pv[p]->pe_allocated = 0;
			vg[VG_CHR(minor)]->pv[p]->pv_status = PV_ACTIVE;
			vg[VG_CHR(minor)]->pv_act++;
			vg[VG_CHR(minor)]->pv_cur++;

#ifdef LVM_GET_INODE
			/* insert a dummy inode for fs_may_mount */
			vg[VG_CHR(minor)]->pv[p]->inode =
			    lvm_get_inode(vg[VG_CHR(minor)]->pv[p]->pv_dev);
#endif
		}
	}

	/* get the logical volume structures */
	vg[VG_CHR(minor)]->lv_cur = 0;
	for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
		/* user space address */
		if ((lvp = vg[VG_CHR(minor)]->lv[l]) != NULL) {
			if (copy_from_user(&lv, lvp, sizeof(lv_t)) != 0) {
				do_vg_remove(minor);
				return -EFAULT;
			}
			vg[VG_CHR(minor)]->lv[l] = NULL;
			if (do_lv_create(minor, lv.lv_name, &lv) != 0) {
				do_vg_remove(minor);
				return -EFAULT;
			}
		}
	}

	/* Second path to correct snapshot logical volumes which are not
	   in place during first path above */
	for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
		if (vg[VG_CHR(minor)]->lv[l] != NULL &&
		    vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT) {
			snaporg_minor = vg[VG_CHR(minor)]->lv[l]->lv_snapshot_minor;
			if (vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)] != NULL) {
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_access |=
				    LV_SNAPSHOT_ORG;
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_exception =
				    vg[VG_CHR(minor)]->lv[l]->lv_exception;
				vg[VG_CHR(minor)]->lv[l]->lv_current_pe =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_current_pe;
				vg[VG_CHR(minor)]->lv[l]->lv_allocated_le =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_allocated_le;
				vg[VG_CHR(minor)]->lv[l]->lv_current_le =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_current_le;
			}
		}
	}

	vg_count++;

	/* let's go active */
	vg[VG_CHR(minor)]->vg_status |= VG_ACTIVE;

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	return 0;
}				/* do_vg_create () */


/*
 * character device support function VGDA remove
 */
static int do_vg_remove(int minor)
{
	int l, p;

	if (vg[VG_CHR(minor)] == NULL)
		return -ENXIO;

#ifdef LVM_TOTAL_RESET
	if (vg[VG_CHR(minor)]->lv_open > 0 && lvm_reset_spindown == 0)
#else
	if (vg[VG_CHR(minor)]->lv_open > 0)
#endif
		return -EPERM;

	/* let's go inactive */
	vg[VG_CHR(minor)]->vg_status &= ~VG_ACTIVE;

	/* free LVs */
	/* first free snapshot logical volumes */
	for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
		if (vg[VG_CHR(minor)]->lv[l] != NULL &&
		    vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT) {
			do_lv_remove(minor, NULL, l);
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}
	/* then free the rest */
	for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
		if (vg[VG_CHR(minor)]->lv[l] != NULL) {
			do_lv_remove(minor, NULL, l);
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	/* free PVs */
	for (p = 0; p < vg[VG_CHR(minor)]->pv_max; p++) {
		if (vg[VG_CHR(minor)]->pv[p] != NULL) {
#ifdef DEBUG_VFREE
			printk(KERN_DEBUG
			       "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
#ifdef LVM_GET_INODE
			clear_inode(vg[VG_CHR(minor)]->pv[p]->inode);
#endif
			vfree(vg[VG_CHR(minor)]->pv[p]);
			vg[VG_CHR(minor)]->pv[p] = NULL;
		}
	}

#ifdef DEBUG_VFREE
	printk(KERN_DEBUG "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
	vfree(vg[VG_CHR(minor)]);
	vg[VG_CHR(minor)] = NULL;

	vg_count--;

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}				/* do_vg_remove () */


/*
 * character device support function logical volume create
 */
static int do_lv_create(int minor, char *lv_name, lv_t * lv)
{
	int l, le, l_new, p, size, snaporg_minor;
	ulong lv_status_save;
	lv_exception_t *lve = lv->lv_exception;
	lv_block_exception_t *lvbe = NULL;

	/* use precopied logical volume */
	if ((pep = lv->lv_current_pe) == NULL)
		return -EINVAL;

	/* in case of lv_remove(), lv_create() pair; for eg. lvrename does this */
	l_new = -1;
	if (vg[VG_CHR(minor)]->lv[lv->lv_number] == NULL)
		l_new = lv->lv_number;
	else {
		for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
			if (lvm_strcmp(vg[VG_CHR(minor)]->lv[l]->lv_name, lv_name) == 0)
				return -EEXIST;
			if (vg[VG_CHR(minor)]->lv[l] == NULL)
				if (l_new == -1)
					l_new = l;
		}
	}
	if (l_new == -1)
		return -EPERM;
	l = l_new;

	if ((vg[VG_CHR(minor)]->lv[l] = vmalloc(sizeof(lv_t))) == NULL) {;
		printk(KERN_CRIT "%s -- LV_CREATE: vmalloc error LV\n", lvm_name);
		return -ENOMEM;
	}
	/* copy preloaded LV */
	lvm_memcpy((char *) vg[VG_CHR(minor)]->lv[l],
		   (char *) lv, sizeof(lv_t));
	lv_status_save = vg[VG_CHR(minor)]->lv[l]->lv_status;
	vg[VG_CHR(minor)]->lv[l]->lv_status &= ~LV_ACTIVE;

	/* get the PE structures from user space if this
	   is no snapshot logical volume */
	if (!(vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT)) {
		size = vg[VG_CHR(minor)]->lv[l]->lv_allocated_le * sizeof(pe_t);
		if ((vg[VG_CHR(minor)]->lv[l]->lv_current_pe =
		     vmalloc(size)) == NULL) {
			printk(KERN_CRIT
			       "%s -- LV_CREATE: vmalloc error LV_CURRENT_PE of %d Byte\n",
			       lvm_name, size);
#ifdef DEBUG_VFREE
			printk(KERN_DEBUG "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
			vfree(vg[VG_CHR(minor)]->lv[l]);
			vg[VG_CHR(minor)]->lv[l] = NULL;
			return -ENOMEM;
		}
		if (copy_from_user(vg[VG_CHR(minor)]->lv[l]->lv_current_pe,
				   pep,
				   size)) {
			vfree(vg[VG_CHR(minor)]->lv[l]->lv_current_pe);
			vfree(vg[VG_CHR(minor)]->lv[l]);
			vg[VG_CHR(minor)]->lv[l] = NULL;
			return -EFAULT;
		}
	} else {
		/* Get snapshot exception data and block list */
		if (lve != NULL) {
			if ((vg[VG_CHR(minor)]->lv[l]->lv_exception =
			     vmalloc(sizeof(lv_exception_t))) == NULL) {
				printk(KERN_CRIT
				       "%s -- LV_CREATE: vmalloc error LV_EXCEPTION at line %d "
				       "of %d Byte\n",
				       lvm_name, __LINE__, sizeof(lv_exception_t));
				vfree(vg[VG_CHR(minor)]->lv[l]);
				vg[VG_CHR(minor)]->lv[l] = NULL;
				return -ENOMEM;
			}
			if (copy_from_user(vg[VG_CHR(minor)]->lv[l]->lv_exception,
					   lve,
					   sizeof(lv_exception_t))) {
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]);
				vg[VG_CHR(minor)]->lv[l] = NULL;
				return -EFAULT;
			}
			lvbe = vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception;
			size = vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_remap_end *
			    sizeof(lv_block_exception_t);
			if ((vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception =
			     vmalloc(size)) == NULL) {
				printk(KERN_CRIT
				       "%s -- LV_CREATE: vmalloc error LV_BLOCK_EXCEPTION "
				       "at line %d of %d Byte\n",
				       lvm_name, __LINE__, size);
#ifdef DEBUG_VFREE
				printk(KERN_DEBUG "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]);
				vg[VG_CHR(minor)]->lv[l] = NULL;
				return -ENOMEM;
			}
			if (copy_from_user(
						  vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception,
						  lvbe, size)) {
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]);
				vg[VG_CHR(minor)]->lv[l] = NULL;
				return -EFAULT;
			}
			/* reset lock to UNLOCKED */
			vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_snapshot_lock = __lvm_dummy_lock;


			snaporg_minor = vg[VG_CHR(minor)]->lv[l]->lv_snapshot_minor;

			if (vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)] != NULL) {
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_access |=
				    LV_SNAPSHOT_ORG;
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_snapshot_minor =
				    MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev);
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_exception =
				    vg[VG_CHR(minor)]->lv[l]->lv_exception;
				vg[VG_CHR(minor)]->lv[l]->lv_current_pe =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_current_pe;
				vg[VG_CHR(minor)]->lv[l]->lv_allocated_le =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_allocated_le;
				vg[VG_CHR(minor)]->lv[l]->lv_current_le =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_current_le;
				vg[VG_CHR(minor)]->lv[l]->lv_size =
				    vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_size;
				/* sync the original logical volume */
				fsync_dev(vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_dev);
			} else {
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]);
				vg[VG_CHR(minor)]->lv[l] = NULL;
				return -EFAULT;
			}
		} else {
			vfree(vg[VG_CHR(minor)]->lv[l]);
			vg[VG_CHR(minor)]->lv[l] = NULL;
			return -EINVAL;
		}
	}			/* if ( vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT) */

	/* correct the PE count in PVs if this is no snapshot logical volume */
	if (!(vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT)) {
		for (le = 0; le < vg[VG_CHR(minor)]->lv[l]->lv_allocated_le; le++) {
			vg[VG_CHR(minor)]->pe_allocated++;
			for (p = 0; p < vg[VG_CHR(minor)]->pv_cur; p++) {
				if (vg[VG_CHR(minor)]->pv[p]->pv_dev ==
				    vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev)
					vg[VG_CHR(minor)]->pv[p]->pe_allocated++;
			}
		}
	}
	lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].start_sect = 0;
	lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].nr_sects =
	    vg[VG_CHR(minor)]->lv[l]->lv_size;
	lvm_size[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)] =
	    vg[VG_CHR(minor)]->lv[l]->lv_size >> 1;
	vg_lv_map[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].vg_number =
	    vg[VG_CHR(minor)]->vg_number;
	vg_lv_map[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].lv_number =
	    vg[VG_CHR(minor)]->lv[l]->lv_number;

	LVM_CORRECT_READ_AHEAD(vg[VG_CHR(minor)]->lv[l]->lv_read_ahead);
	vg[VG_CHR(minor)]->lv_cur++;

	vg[VG_CHR(minor)]->lv[l]->lv_status = lv_status_save;

	return 0;
}				/* do_lv_create () */


/*
 * character device support function logical volume remove
 */
static int do_lv_remove(int minor, char *lv_name, int l)
{
	uint le, p;
	int snaporg_minor;


	if (l == -1) {
		for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
			if (vg[VG_CHR(minor)]->lv[l] != NULL &&
			    lvm_strcmp(vg[VG_CHR(minor)]->lv[l]->lv_name, lv_name) == 0) {
				break;
			}
		}
	}
	if (l < vg[VG_CHR(minor)]->lv_max) {
#ifdef LVM_TOTAL_RESET
		if (vg[VG_CHR(minor)]->lv[l]->lv_open > 0 && lvm_reset_spindown == 0)
#else
		if (vg[VG_CHR(minor)]->lv[l]->lv_open > 0)
#endif
			return -EBUSY;

#ifdef DEBUG
		printk(KERN_DEBUG
		       "%s -- fsync_dev and "
		       "invalidate_buffers for %s [%s] in %s\n",
		       lvm_name, vg[VG_CHR(minor)]->lv[l]->lv_name,
		       kdevname(vg[VG_CHR(minor)]->lv[l]->lv_dev),
		       vg[VG_CHR(minor)]->vg_name);
#endif

		/* check for deletion of snapshot source while
		   snapshot volume still exists */
		if ((vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT_ORG) &&
		    vg[VG_CHR(minor)]->lv[l]->lv_snapshot_minor != 0)
			return -EPERM;

		vg[VG_CHR(minor)]->lv[l]->lv_status |= LV_SPINDOWN;

		/* sync the buffers if this is no snapshot logical volume */
		if (!(vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT))
			fsync_dev(vg[VG_CHR(minor)]->lv[l]->lv_dev);

		vg[VG_CHR(minor)]->lv[l]->lv_status &= ~LV_ACTIVE;

		/* invalidate the buffers */
		invalidate_buffers(vg[VG_CHR(minor)]->lv[l]->lv_dev);

		/* reset generic hd */
		lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].start_sect = -1;
		lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].nr_sects = 0;
		lvm_size[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)] = 0;

		/* reset VG/LV mapping */
		vg_lv_map[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].vg_number = ABS_MAX_VG;
		vg_lv_map[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].lv_number = -1;

		/* correct the PE count in PVs if this is no snapshot logical volume */
		if (!(vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT)) {
			for (le = 0; le < vg[VG_CHR(minor)]->lv[l]->lv_allocated_le; le++) {
				vg[VG_CHR(minor)]->pe_allocated--;
				for (p = 0; p < vg[VG_CHR(minor)]->pv_cur; p++) {
					if (vg[VG_CHR(minor)]->pv[p]->pv_dev ==
					    vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev)
						vg[VG_CHR(minor)]->pv[p]->pe_allocated--;
				}
			}
			/* only if this is no snapshot logical volume because we share
			   the lv_current_pe[] structs with the original logical volume */
			vfree(vg[VG_CHR(minor)]->lv[l]->lv_current_pe);
		} else {
			snaporg_minor = vg[VG_CHR(minor)]->lv[l]->lv_snapshot_minor;

			/* if we deleted snapshot original logical volume before
			   the snapshot volume (in case og VG_REMOVE for eg.) */
			if (vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)] != NULL) {
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_access &=
				    ~LV_SNAPSHOT_ORG;
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_exception = NULL;
				vg[VG_CHR(minor)]->lv[LV_BLK(snaporg_minor)]->lv_snapshot_minor = 0;
			}
			if (vg[VG_CHR(minor)]->lv[l]->lv_exception != NULL) {
				if (vg[VG_CHR(minor)]->lv[l]->lv_exception->lv_block_exception
				    != NULL)
					vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception \
					      ->lv_block_exception);
				vfree(vg[VG_CHR(minor)]->lv[l]->lv_exception);
			}
		}

#ifdef DEBUG_VFREE
		printk(KERN_DEBUG "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
		vfree(vg[VG_CHR(minor)]->lv[l]);
		vg[VG_CHR(minor)]->lv[l] = NULL;
		vg[VG_CHR(minor)]->lv_cur--;
		return 0;
	}
	return -ENXIO;
}				/* do_lv_remove () */


/*
 * character device support function logical volume extend / reduce
 */
static int do_lv_extend_reduce(int minor, char *lv_name, lv_t * lv)
{
	int l, le, p, size, old_allocated_le;
	uint32_t end, lv_status_save;
	pe_t *pe;

	if ((pep = lv->lv_current_pe) == NULL)
		return -EINVAL;

	for (l = 0; l < vg[VG_CHR(minor)]->lv_max; l++) {
		if (vg[VG_CHR(minor)]->lv[l] != NULL &&
		    lvm_strcmp(vg[VG_CHR(minor)]->lv[l]->lv_name, lv_name) == 0)
			break;
	}
	if (l == vg[VG_CHR(minor)]->lv_max)
		return -ENXIO;

	/* check for active snapshot */
	if (lv->lv_exception != NULL &&
	 lv->lv_exception->lv_remap_ptr < lv->lv_exception->lv_remap_end)
		return -EPERM;

	if ((pe = vmalloc(size = lv->lv_current_le * sizeof(pe_t))) == NULL) {
		printk(KERN_CRIT
		"%s -- do_lv_extend_reduce: vmalloc error LV_CURRENT_PE "
		       "of %d Byte\n", lvm_name, size);
		return -ENOMEM;
	}
	/* get the PE structures from user space */
	if (copy_from_user(pe, pep, size)) {
		vfree(pe);
		return -EFAULT;
	}
	if ((pe = vmalloc(size = lv->lv_current_le * sizeof(pe_t))) == NULL) {
		printk(KERN_CRIT
		"%s -- do_lv_extend_reduce: vmalloc error LV_CURRENT_PE "
		       "of %d Byte\n", lvm_name, size);
		return -ENOMEM;
	}
	/* get the PE structures from user space */
	if (copy_from_user(pe, pep, size)) {
		vfree(pe);
		return -EFAULT;
	}
#ifdef DEBUG
	printk(KERN_DEBUG
	       "%s -- fsync_dev and "
	       "invalidate_buffers for %s [%s] in %s\n",
	       lvm_name, vg[VG_CHR(minor)]->lv[l]->lv_name,
	       kdevname(vg[VG_CHR(minor)]->lv[l]->lv_dev),
	       vg[VG_CHR(minor)]->vg_name);
#endif

	vg[VG_CHR(minor)]->lv[l]->lv_status |= LV_SPINDOWN;
	fsync_dev(vg[VG_CHR(minor)]->lv[l]->lv_dev);
	vg[VG_CHR(minor)]->lv[l]->lv_status &= ~LV_ACTIVE;
	invalidate_buffers(vg[VG_CHR(minor)]->lv[l]->lv_dev);

	/* reduce allocation counters on PV(s) */
	for (le = 0; le < vg[VG_CHR(minor)]->lv[l]->lv_allocated_le; le++) {
		vg[VG_CHR(minor)]->pe_allocated--;
		for (p = 0; p < vg[VG_CHR(minor)]->pv_cur; p++) {
			if (vg[VG_CHR(minor)]->pv[p]->pv_dev ==
			vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev) {
				vg[VG_CHR(minor)]->pv[p]->pe_allocated--;
				break;
			}
		}
	}

#ifdef DEBUG_VFREE
	printk(KERN_DEBUG "%s -- vfree %d\n", lvm_name, __LINE__);
#endif

	/* save pointer to "old" lv/pe pointer array */
	pep1 = vg[VG_CHR(minor)]->lv[l]->lv_current_pe;
	end = vg[VG_CHR(minor)]->lv[l]->lv_current_le;

	/* save open counter */
	lv_open = vg[VG_CHR(minor)]->lv[l]->lv_open;

	/* save # of old allocated logical extents */
	old_allocated_le = vg[VG_CHR(minor)]->lv[l]->lv_allocated_le;

	/* copy preloaded LV */
	lvm_memcpy((char *) vg[VG_CHR(minor)]->lv[l], (char *) lv, sizeof(lv_t));
	lv_status_save = vg[VG_CHR(minor)]->lv[l]->lv_status;
	vg[VG_CHR(minor)]->lv[l]->lv_status |= LV_SPINDOWN;
	vg[VG_CHR(minor)]->lv[l]->lv_status &= ~LV_ACTIVE;
	vg[VG_CHR(minor)]->lv[l]->lv_current_pe = pe;
	vg[VG_CHR(minor)]->lv[l]->lv_open = lv_open;

	/* save availiable i/o statistic data */
	/* linear logical volume */
	if (vg[VG_CHR(minor)]->lv[l]->lv_stripes < 2) {
		/* Check what last LE shall be used */
		if (end > vg[VG_CHR(minor)]->lv[l]->lv_current_le)
			end = vg[VG_CHR(minor)]->lv[l]->lv_current_le;
		for (le = 0; le < end; le++) {
			vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].reads = pep1[le].reads;
			vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].writes = pep1[le].writes;
		}
		/* striped logical volume */
	} else {
		uint i, j, source, dest, end, old_stripe_size, new_stripe_size;

		old_stripe_size = old_allocated_le / vg[VG_CHR(minor)]->lv[l]->lv_stripes;
		new_stripe_size = vg[VG_CHR(minor)]->lv[l]->lv_allocated_le /
		    vg[VG_CHR(minor)]->lv[l]->lv_stripes;
		end = old_stripe_size;
		if (end > new_stripe_size)
			end = new_stripe_size;
		for (i = source = dest = 0;
		     i < vg[VG_CHR(minor)]->lv[l]->lv_stripes; i++) {
			for (j = 0; j < end; j++) {
				vg[VG_CHR(minor)]->lv[l]->lv_current_pe[dest + j].reads =
				    pep1[source + j].reads;
				vg[VG_CHR(minor)]->lv[l]->lv_current_pe[dest + j].writes =
				    pep1[source + j].writes;
			}
			source += old_stripe_size;
			dest += new_stripe_size;
		}
	}
	vfree(pep1);
	pep1 = NULL;


	/* extend the PE count in PVs */
	for (le = 0; le < vg[VG_CHR(minor)]->lv[l]->lv_allocated_le; le++) {
		vg[VG_CHR(minor)]->pe_allocated++;
		for (p = 0; p < vg[VG_CHR(minor)]->pv_cur; p++) {
			if (vg[VG_CHR(minor)]->pv[p]->pv_dev ==
			vg[VG_CHR(minor)]->lv[l]->lv_current_pe[le].dev) {
				vg[VG_CHR(minor)]->pv[p]->pe_allocated++;
				break;
			}
		}
	}

	lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].start_sect = 0;
	lvm_gendisk.part[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)].nr_sects =
	    vg[VG_CHR(minor)]->lv[l]->lv_size;
	lvm_size[MINOR(vg[VG_CHR(minor)]->lv[l]->lv_dev)] =
	    vg[VG_CHR(minor)]->lv[l]->lv_size >> 1;
	/* vg_lv_map array doesn't have to be changed here */

	vg[VG_CHR(minor)]->lv[l]->lv_status = lv_status_save;
	LVM_CORRECT_READ_AHEAD(vg[VG_CHR(minor)]->lv[l]->lv_read_ahead);

	return 0;
}				/* do_lv_extend_reduce () */


/*
 * support function initialize gendisk variables
 */
__initfunc(static void lvm_geninit(struct gendisk *lvm_gdisk))
{
	int i = 0;

#ifdef DEBUG_GENDISK
	printk(KERN_DEBUG "%s -- lvm_gendisk\n", lvm_name);
#endif

	for (i = 0; i < MAX_LV; i++) {
		lvm_gendisk.part[i].start_sect = -1;	/* avoid partition check */
		lvm_size[i] = lvm_gendisk.part[i].nr_sects = 0;
		lvm_blocksizes[i] = BLOCK_SIZE;
	}

	blksize_size[LVM_BLOCK_MAJOR] = lvm_blocksizes;
	blk_size[LVM_BLOCK_MAJOR] = lvm_size;

	return;
}				/* lvm_gen_init () */


#ifdef LVM_GET_INODE
/*
 * support function to get an empty inode
 *
 * Gets an empty inode to be inserted into the inode hash,
 * so that a physical volume can't be mounted.
 * This is analog to drivers/block/md.c
 *
 * Is this the real thing?
 *
 */
struct inode *lvm_get_inode(int dev)
{
	struct inode *inode_this = NULL;

	/* Lock the device by inserting a dummy inode. */
	inode_this = get_empty_inode();
	inode_this->i_dev = inode_this->i_rdev = dev;
	insert_inode_hash(inode_this);
	return inode_this;
}
#endif				/* #ifdef LVM_GET_INODE */


/* my strlen */
inline int lvm_strlen(char *s1)
{
	int len = 0;

	while (s1[len] != 0)
		len++;
	return len;
}


/* my strcmp */
inline int lvm_strcmp(char *s1, char *s2)
{
	while (*s1 != 0 && *s2 != 0) {
		if (*s1 != *s2)
			return -1;
		s1++;
		s2++;
	}
	if (*s1 == 0 && *s2 == 0)
		return 0;
	return -1;
}


/* my strrchr */
inline char *lvm_strrchr(char *s1, char c)
{
	char *s2 = NULL;

	while (*s1 != 0) {
		if (*s1 == c)
			s2 = s1;
		s1++;
	}
	return s2;
}


/* my memcpy */
inline void lvm_memcpy(char *dest, char *source, int size)
{
	for (; size > 0; size--)
		*dest++ = *source++;
}
