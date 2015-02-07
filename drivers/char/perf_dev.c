/*
 * perf_dev.c - 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <linux/autoconf.h>


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

#include <linux/sched.h>
#include <linux/wait.h>

#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* file op. */
#include <linux/proc_fs.h>	/* proc fs file op. */
#include <linux/poll.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/uaccess.h>	/* copy to/from user space */
#include <asm/page.h>		/* page size */
#include <asm/pgtable.h>	/* PAGE_READONLY */

#include <linux/perf_dev.h>
#include <linux/module.h>
#include <linux/major.h>

/*
 * device number
 */

#ifndef PERFDEV_MAJOR
#define PERFDEV_MAJOR	124
#endif
static int perf_dev_major=PERFDEV_MAJOR;	
EXPORT_SYMBOL(perf_dev_major);		/* export symbole */
MODULE_PARM(perf_dev_major,"i");	/* as parameter on loaing */

/*
 * export/import global vars
 */

	/* FOR CTRL OPERATION */
extern struct perf_ctrl_operations *perf_ctrl_ops;

	/* FOR COUNTING OPERATION */
extern struct perf_counter_operations *perf_counter_ops;

	/* FOR SAMPLING OPERATION */
extern struct wait_queue *perf_sample_queue;
extern struct perf_sample_operations *perf_sample_ops;


/*
 * File Operations table
 *	please refer <linux/fs.h> for other methods.
 */



static struct file_operations  perf_dev_fops; 	/* generic ops */ 
static struct file_operations  ctrl_fops;	/* for perf_ctrl */
static struct file_operations  counter_fops;	/* for perf_counter */
static struct file_operations  sample_fops;	/* for perf_sample */

static
struct file_operations  *(fops_table[]) = {
	&ctrl_fops,
	&counter_fops,
	&sample_fops,
	NULL
};


struct sample_context{
	struct perf_sample_entry (*buffer)[];
	int num;
	int pos;
	void *key;
};

/*
 * device private data and access methods
 */

struct  perf_dev_data {
	struct file_operations * fops; 
	struct fasync_struct *async_queue;
	union {
		struct sample_context sample_context;
	} aux;
};


inline static struct sample_context *
get_sample_context(struct file *p_file)
{
	return &((struct perf_dev_data *)p_file->private_data)
						->aux.sample_context;
}

inline
static struct file_operations * file_ops(struct file *p_file) 
{
	if (!(p_file->private_data)) {
		printk(KERN_ERR 
			"perf_dev:no private data\n");
		return NULL;
	}
	if (!(((struct perf_dev_data *)(p_file->private_data))->fops)) {
		printk(KERN_ERR 
			"perf_dev:no private fops\n");
		return NULL;
	}

	return (((struct perf_dev_data *)(p_file->private_data))->fops);
}

inline
static void store_fops(struct file *p_file,int index)
{
	(((struct perf_dev_data *)(p_file->private_data))->fops)
		= fops_table[index];
}



/*======================================================================*/

/*
 * PROC FS support 
 */

static int get_perf_sample_info
	(char *buf, char **start, off_t pos, int count, int wr);

static int get_perf_counter_info
	(char *buf, char **start, off_t pos, int count, int wr);

static
struct proc_dir_entry proc_sample = {
	low_ino:0,		/* inode # will be dynamic assgined */
	namelen:sizeof(PERF_DEV_SAMPLE_NAME)-1, 
	name:PERF_DEV_SAMPLE_NAME,
	mode:(S_IFREG | S_IRUGO),	/* in <linux/stat.h> */
	nlink:1,		/* 1 for FILE, 2 for DIR */
	uid:0, gid:0,		/* owner and group */
	size:0, 		/* inode size */
	ops:NULL,		/* use default procs for leaf */
	get_info:get_perf_sample_info,
};

static
struct proc_dir_entry proc_counter = {
	low_ino:0,		/* inode # will be dynamic assgined */
	namelen:sizeof(PERF_DEV_COUNTER_NAME)-1, 
	name:PERF_DEV_COUNTER_NAME,
	mode:(S_IFREG | S_IRUGO),	/* in <linux/stat.h> */
	nlink:1,		/* 1 for FILE, 2 for DIR */
	uid:0, gid:0,		/* owner and group */
	size:0, 		/* inode size */
	ops:NULL,		/* use default procs for leaf */
	get_info:get_perf_counter_info,
};


static 
int get_perf_info(
		void * (*get_info)(char *, int),
		char *buf, 	/*  allocated area for info */
		char **start, 	/*  return your own area if you allocate */
		off_t pos,	/*  pos arg of vfs read */
		int count,	/*  readable bytes */
		int wr)		/*  1, for O_RDWR  */
{

/* SPRINTF does not exist in the kernel */
#define MY_BUFSIZE (PAGE_SIZE-256)
	int len;

	char *mybuf;

	mybuf = (char *) get_free_page(GFP_ATOMIC);
	if (!mybuf)
		return -ENOMEM;

	(*get_info)(mybuf,  MY_BUFSIZE);

	len = strlen(mybuf) +1;

	if ( pos >= len ) {
		return 0;
	}

	if ( pos+count >= len ) {
		count = len-pos;
	}
	memcpy (buf, mybuf+pos, count);
	free_page((unsigned long) mybuf);

	return count;
}

/* NOTE:Caller of (*get_info)() is  proc_file_read() in fs/proc/generic.c */
static 
int get_perf_sample_info(char *buf, 	/*  allocated area for info */
	       char **start, 	/*  return your own area if you allocate */
	       off_t pos,	/*  pos arg of vfs read */
	       int count,	/*  readable bytes */
	       int wr)		/*  1, for O_RDWR  */
{

	return  get_perf_info(
			perf_sample_ops->get_info,
			buf,
			start,
			pos,
			count,
			wr);
}

static 
int get_perf_counter_info(char *buf, 	/*  allocated area for info */
	       char **start, 	/*  return your own area if you allocate */
	       off_t pos,	/*  pos arg of vfs read */
	       int count,	/*  readable bytes */
	       int wr)		/*  1, for O_RDWR  */
{

	return  get_perf_info(
			perf_counter_ops->get_info,
			buf,
			start,
			pos,
			count,
			wr);
}


/*======================================================================*/

/*
 * Constructor/Destoructor of the driver itself.
 */

#ifdef MODULE
int
init_module (void)
{

	int result;


	/* register chrdev */
	result = register_chrdev(perf_dev_major, 
			PERF_DEV_NAME , &perf_dev_fops);
	if (result < 0) {
		printk(KERN_WARNING 
		       PERF_DEV_SAMPLE_NAME 
			": can't get major %d\n",perf_dev_major);
		return result;
	}
	if (perf_dev_major == 0) perf_dev_major = result; /* dynamic */

	/*
	 * register /proc entry, if you want.
	 */
	result=proc_register(&proc_root, &proc_sample);
	if (result < 0)  {
		printk(KERN_WARNING 
		       PERF_DEV_NAME ": can't get proc entry\n");
		unregister_chrdev(perf_dev_major, PERF_DEV_NAME);
		return result;
	}

	result=proc_register(&proc_root, &proc_counter);
	if (result < 0)  {
		printk(KERN_WARNING 
		       PERF_DEV_NAME ": can't get proc entry\n");

		unregister_chrdev(perf_dev_major, PERF_DEV_NAME);

		(void) proc_unregister(&proc_root, proc_sample.low_ino);

		return result;
	}

	return 0;
}

void
cleanup_module (void)
{


	/* unregister /proc entry */
	(void) proc_unregister(&proc_root, proc_sample.low_ino);
	(void) proc_unregister(&proc_root, proc_counter.low_ino);

	/* unregister chrdev */
	unregister_chrdev(perf_dev_major, PERF_DEV_NAME);

}


#endif /* MODULE */

__initfunc(void perf_dev_init(void))
{
	int result;


	/* register chrdev */
	result = register_chrdev (perf_dev_major,
			PERF_DEV_NAME,&perf_dev_fops);
	if (result < 0) {
		printk(KERN_WARNING 
			PERF_DEV_NAME 
			": can't get major %d\n",perf_dev_major);
		return;
	}

	if (perf_dev_major == 0) perf_dev_major = result; /* dynamic */

	/* register /proc entry */
	result=proc_register(&proc_root, &proc_sample);
	if (result < 0)  {
		printk(KERN_WARNING 
		       PERF_DEV_NAME ": can't get proc entry\n");
		unregister_chrdev(perf_dev_major, PERF_DEV_NAME);

		return;
	}

	result=proc_register(&proc_root, &proc_counter);
	if (result < 0)  {
		printk(KERN_WARNING 
		       PERF_DEV_COUNTER_NAME ": can't get proc entry\n");
		unregister_chrdev(perf_dev_major, PERF_DEV_COUNTER_NAME);

		(void) proc_unregister(&proc_root, proc_sample.low_ino);
		return;
	}

}



/*======================================================================*/

/*
 * 	Device File Operations
 */

/*
 *  Bit layout of dev_t
 *
 *		|76|543210|
 *		|--|------|	XX:	type 
 *		|XX|YYYYYY|	YYYY:	cpu id
 *	type:	0 ... control
 *		1 ... counter
 *		2 ... sample
 *
 */
#define DEV_CPUID_MASK	0xc0
#define DEV_TYPE(rdev)	(((MINOR(rdev)) & DEV_CPUID_MASK)>>6)
#define DEV_CPUID(rdev)	((MINOR(rdev)) & ~DEV_CPUID_MASK)


/*======================================================================*/
/*	Ctrl ops							*/
/*======================================================================*/

/*
 * Open and Close
 */

static int open_ctrl (struct inode *p_inode, struct file *p_file)
{
	
        if ( p_file->f_mode & FMODE_WRITE ) {
                return -EPERM;
        }
        return 0;          /* success */
}

static int release_ctrl (struct inode *p_inode, struct file *p_file)
{
	return 0;
}

/*
 * Ioctl
 */

static int ioctl_ctrl (struct inode *inode, 
		struct file *file, unsigned int cmd, unsigned long arg)
{
	int cpuid=DEV_CPUID(file->f_dentry->d_inode->i_rdev);
        return perf_ctrl_ops->control(cpuid, cmd, (void *)arg);
}

/*======================================================================*/
/*	Counter ops							*/
/*======================================================================*/

/*
 * Open and Close
 */

static int open_counter (struct inode *p_inode, struct file *p_file)
{
	
	struct perf_dev_internal_cmd cmd_pkt;
	int ret;
	int cpuid=DEV_CPUID(p_inode->i_rdev);
        u8 wr = ( p_file->f_mode & FMODE_WRITE ) != 0 ;
	
	cmd_pkt.cmd = wr ? PERF_ACQUIRE_WR : PERF_ACQUIRE;
        ret=perf_counter_ops->control(cpuid, PERF_IOCINTERNAL, &cmd_pkt);
	return ret;
}

static int release_counter (struct inode *p_inode, struct file *p_file)
{
	struct perf_dev_internal_cmd cmd_pkt;
	int cpuid=DEV_CPUID(p_inode->i_rdev);
        u8 wr = ( p_file->f_mode & FMODE_WRITE ) != 0 ;

	cmd_pkt.cmd = wr ? PERF_RELEASE_WR : PERF_RELEASE;
        (void)perf_counter_ops->control(cpuid, PERF_IOCINTERNAL, &cmd_pkt);
	return 0;
}

/*
 * Ioctl
 */

static int ioctl_counter (struct inode *inode, 
		struct file *file, unsigned int cmd, unsigned long arg)
{
	int cpuid=DEV_CPUID(file->f_dentry->d_inode->i_rdev);
        return perf_counter_ops->control(cpuid, cmd, (void *)arg);
}


/*======================================================================*/
/*	Sampling ops							*/
/*======================================================================*/

/*
 * Open and Close
 */

static int open_sample (struct inode *p_inode, struct file *p_file)
{
	
	int ret;
	struct perf_dev_internal_cmd cmd_pkt;
	int cpuid=DEV_CPUID(p_inode->i_rdev);
#ifdef DEBUG
#else /* DEBUG */
        if ( p_file->f_mode & FMODE_WRITE ) {
                return -EPERM;
        }
#endif /* DEBUG */
	
	cmd_pkt.cmd = PERF_ACQUIRE;
        ret = perf_sample_ops->control(cpuid, PERF_IOCINTERNAL, &cmd_pkt);

	if (ret == 0 ) {
		struct sample_context * p;
		p = get_sample_context(p_file);
		p->num = 0;
		p->pos = 0;
		p->buffer= 0;
	}

        return ret;
}

static int release_sample (struct inode *p_inode, struct file *p_file)
{
	struct perf_dev_internal_cmd cmd_pkt;
	int cpuid=DEV_CPUID(p_inode->i_rdev);

	cmd_pkt.cmd = PERF_RELEASE;
        (void)perf_sample_ops->control(cpuid, PERF_IOCINTERNAL, &cmd_pkt);
	return 0;
}

#ifdef DEBUG

/*
 * Write
 */

static ssize_t write_sample(struct file *p_file, 
		const char * p_buffer, size_t count, 
		loff_t * p_pos)
{
	
	int cpuid=DEV_CPUID(p_file->f_dentry->d_inode->i_rdev);
	struct inode * p_inode;
	struct perf_sample_entry entry;
	size_t remains;

	p_inode = p_file->f_dentry->d_inode;

	if ( !p_buffer || !count )
		return -EINVAL;

	if (!access_ok (VERIFY_READ,p_buffer,count))
		return -EINVAL; /* not writable */

	
	remains = count;
	if (remains >= sizeof (entry) ) {
		while (remains >= sizeof (entry) ) {
			__copy_from_user(&entry, p_buffer, sizeof(entry));
			perf_sample_ops->record(cpuid,&entry);
			remains-= sizeof(entry);
		}
		wake_up_interruptible(&perf_sample_queue);
	}
	return 	count - remains;
}
#endif /* DEBUG */

/*
 * Read
 */


static ssize_t read_sample(struct file *p_file, 
		char * p_buffer, size_t count, 
		loff_t * p_pos)
{
	
	int cpuid=DEV_CPUID(p_file->f_dentry->d_inode->i_rdev);
	struct wait_queue wait = { current, NULL };
	struct inode * p_inode;
	int	transfer;
	size_t	copy_size;
	int i;
	void *head;
	const size_t entry_size=sizeof(struct perf_sample_entry);
#define ARRAY_SIZE(x) (x/entry_size)


	struct sample_context * sample_context;
	sample_context = get_sample_context(p_file);

	p_inode = p_file->f_dentry->d_inode;

	if ( !p_buffer || !count )
		return -EINVAL;

	if ( (p_file->f_flags & O_NONBLOCK) && 
		(perf_sample_ops->is_empty (cpuid)) ) {
			return -EAGAIN;
	}

	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&perf_sample_queue, &wait);
	while ( (!sample_context->num) && perf_sample_ops->is_empty(cpuid) ){
		if (signal_pending(current)) {
			remove_wait_queue(&perf_sample_queue, &wait);
			current->state = TASK_RUNNING;
			return  -ERESTARTSYS;
		}
		schedule();
		current->state = TASK_INTERRUPTIBLE;
	}
	remove_wait_queue (&perf_sample_queue, &wait);
	current->state = TASK_RUNNING;


	if (!sample_context->num) {
		sample_context->num = perf_sample_ops->retrieve_buffer 
			(cpuid, &sample_context->buffer, &sample_context->key);
		sample_context->pos = 0;
	}

	transfer = ARRAY_SIZE(count);
	transfer = transfer > sample_context->num ? sample_context->num: transfer;
	copy_size = entry_size*transfer;

	if (!sample_context->buffer || !copy_size)
		return 0;

	head = &((*sample_context->buffer)[sample_context->pos]);
	i = copy_to_user( p_buffer, head, copy_size);
	if ( i < 0 ) 
		return i;
	sample_context->num -= transfer;
	sample_context->pos += transfer;
	*p_pos += copy_size;
	if (!sample_context->num) {
		perf_sample_ops->release_buffer (cpuid, sample_context->key);
	}
	return copy_size;
}

/*
 * Poll
 */

static unsigned int poll_sample (struct file *file, 
				poll_table * wait)
{
	int cpuid=DEV_CPUID(file->f_dentry->d_inode->i_rdev);
	poll_wait(file, &perf_sample_queue, wait);
	if ( !perf_sample_ops->is_empty (cpuid)) {
		return POLLIN | POLLRDNORM;
	}
	return 0;
}



/*
 * Ioctl
 */

static int ioctl_sample (struct inode *inode, 
		struct file *file, unsigned int cmd, unsigned long arg)
{
	int cpuid=DEV_CPUID(file->f_dentry->d_inode->i_rdev);
        return perf_sample_ops->control(cpuid, cmd, (void *)arg);
}

/*
 * Faync
 */

static int fasync_sample (int fd, struct file *p_file, int on)
{
	int cpuid=DEV_CPUID(p_file->f_dentry->d_inode->i_rdev);
	int result;
	struct perf_dev_data * p_dev_data;
	struct perf_dev_internal_cmd cmd_pkt;

	p_dev_data = (struct perf_dev_data *) p_file->private_data;
	
	result = fasync_helper (fd, p_file, on,
					&p_dev_data->async_queue);
	if (result<0) {
		return result;
	}
	cmd_pkt.cmd = PERF_SET_ASYNC;
	cmd_pkt.arg0 = on ? &p_dev_data->async_queue : NULL;
        perf_sample_ops->control(cpuid, PERF_IOCINTERNAL, &cmd_pkt);

	return 0;

}

/*======================================================================*/
/*	Generic ops							*/
/*======================================================================*/


static
struct file_operations  ctrl_fops = {
	open:open_ctrl,
	release:release_ctrl, 
	ioctl:ioctl_ctrl,
};

static
struct file_operations  counter_fops = {
	open:open_counter,
	release:release_counter, 
	ioctl:ioctl_counter,
};

static
struct file_operations  sample_fops = {
	read:read_sample,
	open:open_sample,
	release:release_sample, 
	poll:poll_sample,
	ioctl:ioctl_sample,
	fasync:fasync_sample,
#ifdef DEBUG
	write:write_sample,
#endif /*DEBUG*/
};

/*
 * Open and Close
 */

static int perf_dev_open (struct inode *p_inode, struct file *p_file)
{
	
	struct file_operations * fops;
	int result=0;
	int cpuid = DEV_CPUID(p_inode->i_rdev);
	int dev_type = DEV_TYPE(p_inode->i_rdev);

	/* check minor */
	if (dev_type >= sizeof(fops_table)/sizeof(fops_table[0])){
		printk(KERN_DEBUG 
			"perf_dev:dev_type is too large:%d\n", dev_type);
		return -EINVAL;
	}
	if (cpuid>=smp_num_cpus) {
		printk(KERN_DEBUG 
			"perf_dev:cpuid is too large:%d\n", cpuid);
		return -EINVAL;
	}


	/* save the fops table to p_file->private_data. */
	if (!p_file->private_data) {
		p_file->private_data 
			= kmalloc(sizeof(struct perf_dev_data), GFP_ATOMIC);
		if (!p_file->private_data) 
			return -ENOMEM;
		memset(p_file->private_data,0,sizeof(struct perf_dev_data));
		store_fops(p_file, dev_type);
	}
	fops = file_ops(p_file);

	/* sanity check */
	if (!fops) {
		printk(KERN_DEBUG "perf_dev:invaild fops table\n");
		kfree(p_file->private_data);
		p_file->private_data=NULL;
		return -EINVAL;
	}

	if (fops->open) {
		/* do open */
		result = fops->open (p_inode, p_file);
		if (result <0) {
			kfree(p_file->private_data);
			p_file->private_data=NULL;
			return result;
		}
	}
        MOD_INC_USE_COUNT;
        return result;
}

static int perf_dev_release (struct inode *p_inode, struct file *p_file)
{
	int result = 0;
	struct file_operations *fops;

	fops=file_ops(p_file);
	if (fops && fops->release)
		result = fops->release (p_inode, p_file);

	if (p_file->private_data) {
		kfree(p_file->private_data);
		p_file->private_data=NULL;
	}

        MOD_DEC_USE_COUNT;
        return result;
}

#ifdef DEBUG
/*
 * Write
 */

static ssize_t perf_dev_write(struct file *p_file, 
		const char * p_buffer, size_t count, 
		loff_t * p_pos)
{
	
	int result;

	struct file_operations *fops;
	fops=file_ops(p_file);
	if (!fops || !fops->write)
		return -EIO;

	result = fops->write (p_file, p_buffer, count, p_pos);

	return result;
}
#endif /* DEBUG */

/*
 * Read
 */

static ssize_t perf_dev_read(struct file *p_file, 
		char * p_buffer, size_t count, 
		loff_t * p_pos)
{
	
	int result;

	struct file_operations *fops;
	fops=file_ops(p_file);
	if (!fops || !fops->read)
		return -EIO;

	result = fops->read (p_file, p_buffer, count, p_pos);

	return result;
}

/*
 * Poll
 */

static unsigned int perf_dev_poll (struct file *p_file, 
				poll_table * p_wait)
{
	int result;

	struct file_operations *fops;
	fops=file_ops(p_file);
	if (!fops || !fops->poll)
		return -EIO;

	result = fops->poll (p_file, p_wait);

	return result;
}

/*
 * Ioctl
 */

static int perf_dev_ioctl (struct inode *p_inode, 
		struct file *p_file, unsigned int cmd, unsigned long arg)
{
	int result;

	struct file_operations *fops;
	fops=file_ops(p_file);
	if (!fops || !fops->ioctl)
			return -EIO;
	if (cmd == PERF_IOCINTERNAL)
		return -EINVAL;
	result = fops->ioctl (p_inode, p_file, cmd, arg);

	return result;
}


/*
 * Faync
 */

static int perf_dev_fasync (int fd, struct file *p_file, int on)
{
	int result;

	struct file_operations *fops;
	fops=file_ops(p_file);
	if (!fops || !fops->fasync)
			return -EIO;
	result = fops->fasync (fd, p_file, on);

	return result;
}

/*
 *	generic ops.
 */
static
struct file_operations  perf_dev_fops = {
	read:perf_dev_read,
	open:perf_dev_open,
	release:perf_dev_release, 
	poll:perf_dev_poll,
	ioctl:perf_dev_ioctl,
	fasync:perf_dev_fasync,
#ifdef DEBUG
	write:perf_dev_write,
#endif /*DEBUG*/
};


