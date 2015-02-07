/*
 *  PlayStation 2 Game Controller driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: pad.c,v 1.10.6.1 2001/09/19 10:08:22 takemura Exp $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include "pad.h"
#include "padcall.h"

#define PS2PAD_NOPORTCLOSE

#define PORT(n)		(((n) & 0x10) >> 4)
#define SLOT(n)		(((n) & 0x0f) >> 0)
#define NPORTS		2
#define NSLOTS		1	/* currently, we doesn't support multitap */
#define MAXNPADS	8
#define DMABUFSIZE	(16 * 16)
#define INTERVAL_TIME  	HZ/10	/* 100ms */

struct ps2pad_dev {
	struct ps2pad_libctx *pad;
};

struct ps2pad_ctl_dev {
	int stat_is_valid;
	struct ps2pad_stat stat[MAXNPADS];
};

static void ps2pad_start_timer(void);
static inline void ps2pad_stop_timer(void);
static inline void ps2pad_update_status(void);
static int lock(void);
static void unlock(void);

static int ps2pad_read_proc(char *, char **, off_t, int, int);
static ssize_t ps2pad_read(struct file *, char *, size_t, loff_t *);
static unsigned int ps2pad_poll(struct file *file, poll_table * wait);
static int ps2pad_ioctl(struct inode *, struct file *, u_int, u_long);
static int ps2pad_open(struct inode *, struct file *);
static int ps2pad_release(struct inode *, struct file *);

static ssize_t ps2pad_ctl_read(struct file *, char *, size_t, loff_t *);
static unsigned int ps2pad_ctl_poll(struct file *file, poll_table * wait);
static int ps2pad_ctl_ioctl(struct inode *, struct file *, u_int, u_long);
static int ps2pad_ctl_release(struct inode *, struct file *);

static int ps2pad_major = PS2PAD_MAJOR;

#ifdef MODULE
EXPORT_NO_SYMBOLS;
MODULE_PARM(ps2pad_major, "0-255i");
#endif

#define PS2PAD_DEBUG
#ifdef PS2PAD_DEBUG
int ps2pad_debug = 0;
#define DPRINT(fmt, args...) \
	if (ps2pad_debug) printk(KERN_CRIT "ps2pad: " fmt, ## args)
#ifdef MODULE
MODULE_PARM(ps2pad_debug, "0-1i");
#endif
#else
#define DPRINT(fmt, args...)
#endif

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))

struct ps2pad_libctx ps2pad_pads[MAXNPADS];
int ps2pad_npads = 0;
static struct wait_queue *lockq = NULL;
static int locked = 0;
static struct wait_queue *watchq = NULL;
static struct timer_list ps2pad_timer;
static struct ps2pad_stat cur_stat[MAXNPADS];
static struct ps2pad_stat new_stat[MAXNPADS];
static int open_ctl_devices = 0;

static struct file_operations ps2pad_fops = {
	NULL,		/* lseek	*/
	ps2pad_read,
	NULL,		/* write	*/
	NULL,		/* readdir	*/
	ps2pad_poll,
	ps2pad_ioctl,
	NULL,		/* mmap		*/
	ps2pad_open,
	NULL,		/* flush	*/
	ps2pad_release,
};

static struct file_operations ps2pad_ctlops = {
	NULL,		/* lseek	*/
	ps2pad_ctl_read,
	NULL,		/* write	*/
	NULL,		/* readdir	*/
	ps2pad_ctl_poll,
	ps2pad_ctl_ioctl,
	NULL,		/* mmap		*/
	NULL,		/* open		*/
	NULL,		/* flush	*/
	ps2pad_ctl_release,
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry ps2pad_proc_de = {
	0, 6, "ps2pad",
	S_IFREG|S_IRUGO, 1, 0, 0,
	0, NULL,
	ps2pad_read_proc, NULL,
	NULL, NULL, NULL,
};
#endif

char *pad_type_names[16] = {
	"type 0",
	"type 1",
	"NEJICON",	/* PS2PAD_TYPE_NEJICON	*/
	"type 3",
	"DIGITAL",	/* PS2PAD_TYPE_DIGITAL	*/
	"ANALOG",	/* PS2PAD_TYPE_ANALOG	*/
	"type 6",
	"DUALSHOCK",	/* PS2PAD_TYPE_DUALSHOCK*/
	"type 8",
	"type 9",
	"type A",
	"type B",
	"type C",
	"type D",
	"type E",
	"type F",
};

static unsigned char stat_conv_table[] = {
	[PadStateDiscon]	= PS2PAD_STAT_NOTCON,
	[PadStateFindPad]	= PS2PAD_STAT_BUSY,
	[PadStateFindCTP1]	= PS2PAD_STAT_READY,
	[PadStateExecCmd]	= PS2PAD_STAT_BUSY,
	[PadStateStable]	= PS2PAD_STAT_READY,
	[PadStateError]		= PS2PAD_STAT_ERROR,
};

static unsigned char rstat_conv_table[] = {
	[PadReqStateComplete]		= PS2PAD_RSTAT_COMPLETE,
	[PadReqStateFailed]		= PS2PAD_RSTAT_FAILED,
	[PadReqStateBusy]		= PS2PAD_RSTAT_BUSY,
};

int
ps2pad_stat_conv(int stat)
{
	if (stat < 0 || ARRAYSIZEOF(stat_conv_table) <= stat) {
		return PS2PAD_STAT_ERROR;
	} else {
		return stat_conv_table[stat];
	}
}

static inline int
ps2pad_comp_stat(struct ps2pad_stat *a, struct ps2pad_stat *b)
{
	return memcmp(a, b, sizeof(struct ps2pad_stat) * ps2pad_npads);
}

static inline void
ps2pad_copy_stat(struct ps2pad_stat *a, struct ps2pad_stat *b)
{
	memcpy(a, b, sizeof(struct ps2pad_stat) * ps2pad_npads);
}

static void
ps2pad_read_stat(struct ps2pad_stat *stat)
{
	int i, res;
	u_char data[PS2PAD_DATASIZE];

	for (i = 0; i < ps2pad_npads; i++) {
		/* port and slot */
		stat[i].portslot = ((ps2pad_pads[i].port << 4) |
				    ps2pad_pads[i].slot);

		/* request status */
		res = ps2padlib_GetReqState(ps2pad_pads[i].port,
					ps2pad_pads[i].slot);
		if (res < 0 || ARRAYSIZEOF(rstat_conv_table) <= res) {
			stat[i].rstat = PS2PAD_RSTAT_FAILED;
		} else {
			stat[i].rstat = rstat_conv_table[res];
		}

		/* connection status */
		res = ps2padlib_GetState(ps2pad_pads[i].port, ps2pad_pads[i].slot);
		stat[i].type = 0;
		if (res < 0 || ARRAYSIZEOF(stat_conv_table) <= res) {
			stat[i].stat = PS2PAD_STAT_ERROR;
		} else {
			stat[i].stat = stat_conv_table[res];
			if (stat[i].stat == PS2PAD_STAT_READY) {
				res = ps2padlib_Read(ps2pad_pads[i].port,
						 ps2pad_pads[i].slot,
						 data);
				if (res != 0 && data[0] == 0) {
					/* pad data is valid */
					stat[i].type = data[1];
				} else {
					stat[i].stat = PS2PAD_STAT_ERROR;
				}
			}
		}
	}
}

static void
ps2pad_do_timer(unsigned long data)
{

	ps2pad_read_stat(new_stat);
	if (ps2pad_comp_stat(new_stat, cur_stat)) {
		ps2pad_copy_stat(cur_stat, new_stat);
#ifdef PS2PAD_DEBUG
			DPRINT("timer: new status: ");
			if (ps2pad_debug) {
				int i;
				u_char *p = (u_char*)new_stat;
				for (i = 0; i < sizeof(*new_stat) * 2; i++)
					printk("%02X", *p++);
				printk("\n");
			}
#endif
		wake_up_interruptible(&watchq);
	}

	ps2pad_timer.expires = jiffies + INTERVAL_TIME;
	add_timer(&ps2pad_timer);
}

static void
ps2pad_start_timer()
{
	DPRINT("start timer\n");
	ps2pad_read_stat(cur_stat);
	cli();
	ps2pad_do_timer(ps2pad_timer.data);
	sti();
}

static inline void
ps2pad_stop_timer()
{
	DPRINT("stop timer\n");
	del_timer(&ps2pad_timer);
}

static inline void
ps2pad_update_status()
{
	cli();
	del_timer(&ps2pad_timer);
	ps2pad_do_timer(ps2pad_timer.data);
	sti();
}

static int
lock()
{
	for ( ; ; ) {
		cli();
		if (!locked) {
			locked = 1;
			sti();
			return 0;
		}
		interruptible_sleep_on(&lockq);
		sti();
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
}

static void
unlock()
{
	cli();
	locked = 0;
	wake_up_interruptible(&lockq);
	sti();
}

static ssize_t
ps2pad_read(struct file *filp, char *buf, size_t size, loff_t *off)
{
	int res;
	struct ps2pad_dev *dev = filp->private_data;
	u_char data[PS2PAD_DATASIZE];

	/* ps2padlib_Read() does not involve any RPC to IOP.
	  if (lock() < 0) return -ERESTARTSYS;
	 */
	res = ps2padlib_Read(dev->pad->port, dev->pad->slot, data);
	/*
	  unlock();
	 */
	if (res == 0 || data[0] != 0) {
		/* pad data is invalid */
		return -EIO;	/* XXX */
	}

	/*
	 * XXX, ignore offset
	 */
	res = (data[1] & 0x0f) * 2 + 2;
	if (res < size) {
		size = res;
	}
	//memcpy_tofs(buf, data, size);
	copy_to_user(buf, data, size);
	return size;
}

static int
ps2pad_wait_req_stat(struct ps2pad_dev *dev)
{
	int res;

	for ( ; ; ) {
		cli();
		res = ps2padlib_GetReqState(dev->pad->port, dev->pad->slot);
		DPRINT("port%d slot%d: req stat %d\n",
		       dev->pad->port, dev->pad->slot, res);
		if (res != PadReqStateBusy) {
			sti();
			return res;
		}
		interruptible_sleep_on(&watchq);
		sti();
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
}

static int
ps2pad_check_req_stat(struct ps2pad_dev *dev)
{
	int res;

	if ((res = ps2pad_wait_req_stat(dev)) < 0)
		return res;
	return (res == PadReqStateComplete) ? 0 : -EIO;
}

static int
ps2pad_ioctl(struct inode *inode, struct file *filp, u_int cmd, u_long arg)
{
	int i, res;
	struct ps2pad_dev *dev = filp->private_data;
	int port = dev->pad->port;
	int slot = dev->pad->slot;

	switch (cmd) {
	case PS2PAD_IOCPRESSMODEINFO:
		if (lock() < 0) return -ERESTARTSYS;
		*(int*)(arg) = ps2padlib_InfoPressMode(port, slot);
		unlock();
		break;
	case PS2PAD_IOCENTERPRESSMODE:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_EnterPressMode(port, slot);
		unlock();
		ps2pad_update_status();
		if (res != 1)
			return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		break;
	case PS2PAD_IOCEXITPRESSMODE:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_ExitPressMode(port, slot);
		unlock();
		ps2pad_update_status();
		if (res != 1)
			return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		break;
	case PS2PAD_IOCGETREQSTAT:
		if (filp->f_flags & O_NONBLOCK) {
			res = ps2padlib_GetReqState(port, slot);
		} else {
			if ((res = ps2pad_wait_req_stat(dev)) < 0)
				return res;
		}
		if (res < 0 || ARRAYSIZEOF(rstat_conv_table) <= res) {
			return -EIO;
		} else {
			*(int*)(arg) = rstat_conv_table[res];
		}
		break;
	case PS2PAD_IOCGETSTAT:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_GetState(port, slot);
		unlock();
		if (res < 0 || ARRAYSIZEOF(stat_conv_table) <= res) {
			return -EIO;
		} else {
			*(int*)(arg) = stat_conv_table[res];
		}
		break;
	case PS2PAD_IOCACTINFO: {
		struct ps2pad_actinfo *info = (struct ps2pad_actinfo *)arg;
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoAct(port, slot, info->actno, info->term);
		unlock();
		if (res < 0) return -EIO;
		info->result = res;
		return 0;
		}
		break;
	case PS2PAD_IOCCOMBINFO: {
		struct ps2pad_combinfo *info = (struct ps2pad_combinfo *)arg;
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoComb(port, slot, info->listno, info->offs);
		unlock();
		if (res < 0) return -EIO;
		info->result = res;
		return 0;
		}
		break;
	case PS2PAD_IOCMODEINFO: {
		struct ps2pad_modeinfo *info = (struct ps2pad_modeinfo *)arg;
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoMode(port, slot, info->term, info->offs);
		unlock();
		if (res < 0) return -EIO;
		info->result = res;
		return 0;
		}
		break;
	case PS2PAD_IOCSETMODE: {
		struct ps2pad_mode *mode = (struct ps2pad_mode *)arg;
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetMainMode(port, slot, mode->offs, mode->lock);
		unlock();
		ps2pad_update_status();
		if (res != 1) {
			DPRINT("%d %d: ps2padlib_SetMainMode() failed\n",
			       dev->pad->port, dev->pad->slot);
			return -EIO;
		}
		if (filp->f_flags & O_NONBLOCK) {
			DPRINT("port%d slot%d: PS2PAD_IOCSETMODE: non-block\n",
			       dev->pad->port, dev->pad->slot);
			return 0;
		} else {
			return ps2pad_check_req_stat(dev);
		}
		}
		break;
	case PS2PAD_IOCSETACTALIGN: {
		struct ps2pad_act *act = (struct ps2pad_act *)arg;
		if (6 < act->len) {
			return EINVAL;
		}
		for (i = act->len; i < 6; i++) {
			act->data[i] = 0xff;
		}
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetActAlign(port, slot, act->data);
		unlock();
		ps2pad_update_status();
		if (res != 1) return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		}
		break;
	case PS2PAD_IOCSETACT: {
		struct ps2pad_act *act = (struct ps2pad_act *)arg;
		if (6 < act->len) {
			return EINVAL;
		}
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetActDirect(port, slot, act->data);
		unlock();
		if (res != 1) return -EIO;
		return 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
ps2pad_open(struct inode *inode, struct file *filp)
{
	kdev_t devno = inode->i_rdev;

	/* diagnosis */
	if (MAJOR(devno) != ps2pad_major) {
		printk(KERN_ERR "ps2pad: incorrect major no\n");
		return -ENODEV;
	}

	DPRINT("open, devno=%04x\n", devno);

	if (MINOR(devno)== 255) {
		/*
		 * control device
		 */
		struct ps2pad_ctl_dev *dev;

		dev = kmalloc(sizeof(struct ps2pad_ctl_dev), GFP_KERNEL);
		if (dev == NULL) {
			return -ENOMEM;
		}
		filp->private_data = dev;

		filp->f_op = &ps2pad_ctlops;

		dev->stat_is_valid = 0;

		if (open_ctl_devices++ == 0) {
			ps2pad_start_timer();
		}
	} else {
		/*
		 * control device
		 */
		struct ps2pad_dev *dev;
		int i;
		int port, slot;

		port = PORT(devno);
		slot = SLOT(devno);

		for (i = 0; i < ps2pad_npads; i++) {
			if (ps2pad_pads[i].port == port &&
			    ps2pad_pads[i].slot == slot) {
				break;
			}
		}

		if (ps2pad_npads <= i) {
			/* pad device not found */
			DPRINT("pad(%d,%d) not found\n", port, slot);
			return -ENODEV;
		}

		dev = kmalloc(sizeof(struct ps2pad_dev), GFP_KERNEL);
		if (dev == NULL) {
			return -ENOMEM;
		}
		filp->private_data = dev;

		dev->pad = &ps2pad_pads[i];
	}

	MOD_INC_USE_COUNT;

	return 0;
}

static ssize_t
ps2pad_ctl_read(struct file *filp, char *buf, size_t size, loff_t *off)
{
	struct ps2pad_ctl_dev *dev = filp->private_data;

	if (sizeof(struct ps2pad_stat) * ps2pad_npads < size)
		size = sizeof(struct ps2pad_stat) * ps2pad_npads;
	cli();
	for ( ; ; ) {
		if ((filp->f_flags & O_NONBLOCK) ||
		    !dev->stat_is_valid ||
		    ps2pad_comp_stat(dev->stat, cur_stat)) {
			ps2pad_copy_stat(dev->stat, cur_stat);
			dev->stat_is_valid = 1;
#ifdef PS2PAD_DEBUG
			DPRINT("new status: ");
			if (ps2pad_debug) {
				int i;
				u_char *p = (u_char*)dev->stat;
				for (i = 0; i < size; i++)
					printk("%02X", *p++);
				printk("\n");
			}
#endif
			copy_to_user(buf, dev->stat, size);
			sti();
			return size;
		}
		interruptible_sleep_on(&watchq);
		sti();
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
}

static int
ps2pad_ctl_ioctl(struct inode *inode, struct file *filp, u_int cmd, u_long arg)
{
	/*
	int res;
	struct ps2pad_ctl_dev *dev = filp->private_data;
	*/

	switch (cmd) {
	case PS2PAD_IOCGETNPADS:
		*(int*)(arg) = ps2pad_npads;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int
ps2pad_poll(struct file *file, poll_table * wait)
{
	return POLLIN | POLLRDNORM;
}

static unsigned int
ps2pad_ctl_poll(struct file *filp, poll_table * wait)
{
	unsigned int mask = 0;
	struct ps2pad_ctl_dev *dev = filp->private_data;

	poll_wait(filp, &watchq, wait);
	cli();
	if (!dev->stat_is_valid || ps2pad_comp_stat(dev->stat, cur_stat))
		mask |= POLLIN | POLLRDNORM;
	sti();

	return mask;
}

static int
ps2pad_release(struct inode *inode, struct file *filp)
{
	struct ps2pad_dev *dev = filp->private_data;

	DPRINT("close, dev=%lx\n", (unsigned long)dev);

       	kfree(dev);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int
ps2pad_ctl_release(struct inode *inode, struct file *filp)
{
	struct ps2pad_ctl_dev *dev = filp->private_data;

	DPRINT("ctl close, dev=%lx\n", (u_long)dev);

	kfree(dev);

	if (--open_ctl_devices == 0) {
		ps2pad_stop_timer();
	}

	MOD_DEC_USE_COUNT;

	return 0;
}

#ifdef CONFIG_PROC_FS
static char* PadStateStr[] =
{"DISCONNECT", "", "FINDCTP1", "", "", "EXECCMD", "STABLE", "ERROR" };

static int
ps2pad_read_proc(char *buf, char **start, off_t offset, int len, int unused)
{
	int res;
	int n, i, j;
	char *p = buf;
	u_char data[PS2PAD_DATASIZE];
	char *tmp;

	p += sprintf(p, "port slot status     type      button\n");

	cli();
	for (i = 0; i < ps2pad_npads; i++) {
		res = ps2padlib_GetState(ps2pad_pads[i].port, ps2pad_pads[i].slot);
		tmp = (res >= 0 && res <= 7) ? PadStateStr[res] : "";
		p += sprintf(p, "%4d %4d %-10s",
			     ps2pad_pads[i].port, ps2pad_pads[i].slot, tmp);

		res = ps2padlib_Read(ps2pad_pads[i].port, ps2pad_pads[i].slot,
				 data);
		if (res != 0 && data[0] == 0) {
			/* pad data is valid */
			p += sprintf(p, " %-9s",
				     pad_type_names[(data[1] & 0xf0) >> 4]);
			p += sprintf(p, " %02X%02X ", data[2], data[3]);
			n = (data[1] & 0x0f) * 2 + 2;
			for (j = 4; j < n; j++) {
				p += sprintf(p, "%02X", data[j]);
			}
		}
		p += sprintf(p, "\n");
	}
	sti();

	return p - buf;
}
#endif

int ps2pad_init(void)
{
	int res, i;
	int port, slot;
	unsigned char *dmabuf;

	DPRINT("PlayStation 2 game pad: initialize...\n");

	/*
	 * initialize library
	 */
	if (ps2padlib_Init(0) != 1) {
		printk(KERN_ERR "ps2pad: failed to initialize\n");
		return -EIO;
	}

	/*
	 * allocate memory space
	 */
	dmabuf = kmalloc(DMABUFSIZE * MAXNPADS, GFP_KERNEL);
	if (dmabuf == NULL) {
		printk(KERN_ERR "ps2pad: can't allocate memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < MAXNPADS; i++) {
		/* 
		 * We must access asynchronous DMA buffer via 
		 * non-cached segment(KSEG1).
		 */
		ps2pad_pads[i].dmabuf = (void *)KSEG1ADDR(&dmabuf[DMABUFSIZE * i]);
	}

	/*
	 * scan all pads and start DMA
	 */
	if (lock() < 0) return -ERESTARTSYS;
	for (port = 0; port < NPORTS; port++) {
	    for (slot = 0; slot < NSLOTS; slot++) {
		res = ps2padlib_PortOpen(port, slot,
				     (void *)ps2pad_pads[ps2pad_npads].dmabuf);
		if (res == 1) {
			DPRINT("port%d  slot%d\n", port, slot);
			ps2pad_pads[ps2pad_npads].port = port;
			ps2pad_pads[ps2pad_npads].slot = slot;
			ps2pad_npads++;
			if (MAXNPADS <= ps2pad_npads) {
				printk(KERN_WARNING "ps2pad: too many pads\n");
				break;
			}
		}
	    }
	}
	unlock();

	/*
	 * initialize timer
	 */
	init_timer(&ps2pad_timer);
	ps2pad_timer.function = ps2pad_do_timer;
	ps2pad_timer.data = 0;

	/*
	 * register device entry
	 */
	if ((res = register_chrdev(ps2pad_major, "ps2pad", &ps2pad_fops)) < 0) {
		printk(KERN_ERR "ps2pad: can't get major %d\n", ps2pad_major);
		return res;
	}
	if (ps2pad_major == 0)
		ps2pad_major = res;

#ifdef CONFIG_PROC_FS
	proc_register(&proc_root, &ps2pad_proc_de);
#endif

#if defined(CONFIG_JOYSTICK) || defined(CONFIG_JOYSTICK_MODULE)
	ps2pad_js_init();
#endif

	return( 0 );
}

#ifdef MODULE
int
init_module(void)
{
	DPRINT("load\n");
	return ps2pad_init();
}

void
cleanup_module(void)
{
#ifndef PS2PAD_NOPORTCLOSE
	int res, i;
#endif

	DPRINT("unload\n");

#ifndef PS2PAD_NOPORTCLOSE
	for (i = 0; i < ps2pad_npads; i++) {
		res = ps2padlib_PortClose(ps2pad_pads[i].port,
				      ps2pad_pads[i].slot);
		if (res != 1) {
			printk(KERN_WARNING "ps2pad: failed to close\n");
		}
	}
#endif

#if defined(CONFIG_JOYSTICK) || defined(CONFIG_JOYSTICK_MODULE)
	ps2pad_js_quit();
#endif

	if (unregister_chrdev(ps2pad_major, "ps2pad") < 0) {
		printk(KERN_WARNING "ps2pad: unregister_chrdev() error\n");
	}

#ifdef CONFIG_PROC_FS
	proc_unregister(&proc_root, ps2pad_proc_de.low_ino);
#endif

	if (ps2padlib_End() != 1) {
		printk(KERN_WARNING "ps2pad: failed to finalize\n");
	}
}
#endif /* MODULE */
