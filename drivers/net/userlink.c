/*
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 *
 * Portions:
 * Copyright (c) 1995, Kazuhiro Fukumura <kazu-f@po.iijnet.or.jp>
 *
 * This source may be freely distributed, however I would be interested
 * in any changes that are made.
 *
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have it's wicked way with. This driver has it's
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/select mode of
 * operation though.
 * 
 * NOTE:
 * Linux device driver for the IP tunneling device written by Kazuhiro Fukumura.
 * Much of this driver was taken from Julian Onions' tunnel device driver,
 * so it bears his copyright.
 */

/*
 * USERLINK: userlink network driver for Linux.
 *
 * Modifies:
 *    1996/01/01
 *	- dynamic allocation manabe@papilio.tutics.tut.ac.jp.
 *    1996/01/14: 0.3
 *	- fix close problem manabe@papilio.tutics.tut.ac.jp.
 *    1996/02/13: 0.5
 *	- compile with 1.2.x kernel manabe@papilio.tutics.tut.ac.jp.
 *    1996/02/14: 0.6
 *	- work with 1.2.x kernel manabe@papilio.tutics.tut.ac.jp.
 *    1996/02/19: 0.7
 *	- auto-alloc in register_chrdev() manabe@papilio.tutics.tut.ac.jp.
 *    1996/05/01: 0.8
 *	- dequeue all non-sent skb manabe@papilio.tutics.tut.ac.jp.
 *    1996/05/20: 0.9
 *	- use nqueue in old kernels manabe@papilio.tutics.tut.ac.jp.
 *    1996/11/22: 0.10
 *	- count alloc_skb error as dropping packet
 *	- fix typo in #if LINUX_VERSION_CODE check
 *					shimaz-n@jed.uec.ac.jp.
 *    1996/12/03: 0.11
 *	- removed 1.[23].x support code, added 2.1.x support code, instead
 *					manabe@papilio.tutics.tut.ac.jp.
 *    1996/12/09: 0.90
 *	- added 'BASE' encoding mode based on
 *	  "New User Link Network Device Driver Draft Specification
 *	                  v0.1 draft, Nov 28 1996, Shimazaki Naoto"
 *					manabe@papilio.tutics.tut.ac.jp.
 *    1997/01/21: 0.91
 *	- don't access to user space directly (fop_ul_write)
 *	  it causes segmentation violation in kernel space
 *					manabe@papilio.tutics.tut.ac.jp.
 *    1997/04/20: 0.92
 *	- support new 2.1.x kernel
 *					yosshy@jedi.seg.kobe-u.ac.jp.
 *					manabe@papilio.ics.tut.ac.jp.
 *    1997/11/06: 0.93
 *	- support new 2.1.x (x>60) kernel
 *					yosshy@jedi.seg.kobe-u.ac.jp.
 *    1997/11/06: 0.94
 *	- support new 2.1.x (x>68) kernel
 *					manabe@amitaj.or.jp.
 *
 *    1998/02/12: 0.95
 *	- support new 2.1.x (x>8?) kernel
 *	- call dev_close() from fop_ul_close()
 *					yosshy@jedi.seg.kobe-u.ac.jp.
 *					manabe@amitaj.or.jp.
 *
 *    1998/02/23: 0.96
 *	- net_ul_start(): reset dev->start/dev->tbusy
 *	- add dev->flags to IFF_NOARP
 *	- use dummy(0:0:0:0:0:0) MAC address
 *					manabe@amitaj.or.jp.
 *
 *    1998/03/09: 0.97
 *	- support new 2.1.x (x>89) kernel: poll_wait()
 *					yosshy@debian.or.jp
 *
 *    1998/08/29: 0.98a
 *	- for new file_operations structure (2.1.118)
 *					ishikawa@linux.or.jp
 *
 *    1999/04/09: 0.99
 *      - code cleanup
 *      - SMP support
 *                                      sam@debian.org
 *
 *    1999/08/22: 0.99(release)
 *	- Add 2.3 kernel support
 *        o struct device -> struct net_device
 *        o struct wait_queue* -> wait_queue_head_t
 *	- Remove 2.1 kernel support
 *					manabe@dsl.gr.jp
 *
 *    1999/12/09: 0.99a
 *	- Do not include asm/spinlock.h
 *					manabe@dsl.gr.jp
 *
 * This program is based on Kazuhiro Fukumura's IP tunneling device
 * driver which is derived from Julian Onions' tunnel device driver.
 *
 */

static const char *version = "0.99a";

#include <linux/version.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/in.h>
#include <linux/errno.h>
#include <linux/major.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#ifndef	__BIT_TYPES_DEFINED__
typedef unsigned char	u_int8_t;
typedef unsigned short int	u_int16_t;
typedef unsigned int	u_int32_t;
#endif

#include <linux/if_userlink.h>

#include <linux/poll.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE < 0x20300
#include <asm/spinlock.h>
# define net_device	device
typedef struct wait_queue *	wait_queue_head_t;
#define	init_waitqueue_head(a)
#endif

/*
#define	DEBUG
*/

#ifdef	DEBUG
static int prevjif=0;
#define	JIFDEBUG(a)\
{\
    printk("ul@%s: %ld(%ld)\n", a, jiffies, jiffies - prevjif);\
    prevjif=jiffies;\
}
#else
#define	JIFDEBUG(a)
#endif

#define	UL_MTU		1500
#define	UL_MAX_MTU	2000
#define	UL_NAME		"userlink"
#define	MAX_NQUEUE	300

#define UL_MAJOR        0

static int ul_major=UL_MAJOR;

static struct ullink {
    char name[IFNAMSIZ];
    __u32 minor;		/* minor number of char dev */
    __u32 rx_packets;		/* received packets */
    __u32 tx_packets;		/* transmitted packets */
    __u32 tx_errors;
    __u32 tx_dropped;
    __u32 rx_dropped;
    wait_queue_head_t wait;
    struct net_device *dev;	/* pointer to network device structure */
    struct sk_buff_head	queue;
    struct ullink *next;
    __u16 max_mtu;
    __u8 version;		/* frame type */
} *ullHead;

static spinlock_t ullHead_lock __attribute__((unused)) = SPIN_LOCK_UNLOCKED;

static struct enet_statistics *
net_ul_stats(struct net_device *dev)
{
    struct ullink *ullp = (struct ullink *)dev->priv;
    static struct enet_statistics stats;

    stats.rx_packets = ullp->rx_packets;
    stats.tx_packets = ullp->tx_packets;
    stats.tx_errors = ullp->tx_errors;
    stats.tx_dropped = ullp->tx_dropped;
    stats.rx_dropped = ullp->rx_dropped;
    return(&stats);
}

static int
net_ul_start(struct net_device *dev)
{
    dev->start = 1;
    dev->tbusy = 0;
    return(0);
}

static int
net_ul_stop(struct net_device *dev)
{
    dev->start = 0;
    dev->tbusy = 1;
    return(0);
}

static int
net_ul_header(struct sk_buff *skb, struct net_device *dev,
	      unsigned short type, void *daddr, void *saddr,
	      unsigned len)
{
    struct ullink *ullp = (struct ullink *)dev->priv;
    struct ul_fs_header_base *ulhp;

    if (ullp->version == UL_V_NONE) return(0);
    ulhp = (struct ul_fs_header_base *)
	skb_push(skb, sizeof(struct ul_fs_header_base));
    memset(ulhp, 0, sizeof(struct ul_fs_header_base));
    ulhp->version = ullp->version;
    ulhp->protocol = htons(type);
    return(sizeof(struct ul_fs_header_base));
}

static int
net_ul_rebuild(struct sk_buff *skb)
{
    return(0);
}

static int
net_ul_tx(struct sk_buff *skb, struct net_device *dev)
{
    struct ullink *ullp = (struct ullink *)dev->priv;

JIFDEBUG("ul@net_ul_tx");
#ifdef DEBUG
printk("ul@net_ul_tx: ullp=%x\n", ullp);
#endif
    if (dev->tbusy) return(-EBUSY);

    if (skb == NULL) {
/*	dev_tint(dev);*/
	return(0);
    }

    if (skb->len > dev->mtu) {
	printk("%s: packet too big, %d.\n", dev->name, (int)skb->len);
	ullp->tx_dropped++;
	return(0);
    }
    if (skb_queue_len(&ullp->queue) == MAX_NQUEUE
	&& (skb = skb_dequeue(&ullp->queue)) != NULL) {
	dev_kfree_skb(skb);
	ullp->tx_errors++;
    }
    if (skb_queue_len(&ullp->queue) < MAX_NQUEUE) {
#ifdef DEBUG
printk("net_ul_tx:queue_len=%d\n", skb_queue_len(&ullp->queue));
#endif
	skb_queue_tail(&ullp->queue, skb);
/*	ullp->tx_packets++;*/
/*	wake_up(&ullp->wait);*/
#ifdef DEBUG
printk("net_ul_tx:timeout0=%d\n", current->timeout);
#endif
	wake_up_interruptible(&ullp->wait);
#ifdef DEBUG
printk("net_ul_tx:timeout1=%d\n", current->timeout);
#endif
	return(0);
    }
    return(0);
}

static int
net_ul_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
    struct ullink *ullp = (struct ullink *)dev->priv;
    struct ul_ifru_data uld;
    int ret;

    if (cmd != SIOCDEVPRIVATE) return(-EINVAL);
    if ((ret = verify_area(VERIFY_WRITE, ifr->ifr_data,
			   sizeof(struct ul_ifru_data))) != 0)
	return(ret);
    copy_from_user(&uld, ifr->ifr_data, sizeof(struct ul_ifru_data));
    switch(uld.command) {
    case UL_IOCGET:
	uld.version = ullp->version;
	uld.max_mtu = ullp->max_mtu;
	copy_to_user(ifr->ifr_data, &uld, sizeof(struct ul_ifru_data));
	ret = 0;
	break;
    case UL_IOCSET:
	ullp->version = uld.version;
	if (uld.max_mtu) ullp->max_mtu = uld.max_mtu;
	if (uld.version == UL_V_NONE)
	    dev->hard_header_len = 0;
	else
	    dev->hard_header_len = sizeof(struct ul_fs_header_base);
	ret = 0;
	break;
    default:
	ret = -EINVAL;
    }
    return(ret);
}

static int
net_ul_mtu(struct net_device *dev, int new_mtu)
{
    struct ullink *ullp = (struct ullink *)dev->priv;

    if ((new_mtu < sizeof(struct ul_fs_header_base)) ||
	(new_mtu > ullp->max_mtu)) return(-EINVAL);
    dev->mtu = new_mtu;
    return(0);
}

/*
 * file operations
 */

static int
fop_ul_open(struct inode *inode, struct file *file)
{
    int i;
    register int minor = MINOR(inode->i_rdev);
    struct net_device *dev;
    struct ullink *ullp;
    unsigned long lock_flags;

    spin_lock_irqsave (&ullHead_lock, lock_flags);
    for (ullp = ullHead; ullp != NULL; ullp = ullp->next) {
	if (ullp->minor == minor) {
	  spin_unlock_irqrestore (&ullHead_lock, lock_flags);
	  return(-EBUSY);
	}
    }
    ullp = kmalloc(sizeof(struct ullink), GFP_KERNEL);
    memset(ullp, 0, sizeof(struct ullink));
    ullp->next = ullHead;
    ullHead = ullp;
    ullp->minor = minor;
    spin_unlock_irqrestore (&ullHead_lock, lock_flags);

    sprintf(ullp->name, "ul%d", minor);
    dev = kmalloc(sizeof(struct net_device), GFP_KERNEL);
    memset(dev, 0, sizeof(struct net_device));
    skb_queue_head_init(&ullp->queue);
    init_waitqueue_head(&ullp->wait);
    ullp->max_mtu = UL_MAX_MTU;
    dev->mtu = UL_MTU;
    dev->init = net_ul_start;
    dev->open = net_ul_start;
    dev->stop = net_ul_stop;
    dev->do_ioctl = net_ul_ioctl;
    dev->hard_start_xmit = net_ul_tx;
    dev->hard_header = net_ul_header;
    dev->rebuild_header = net_ul_rebuild;
    dev->change_mtu = net_ul_mtu;
    dev->type = ARPHRD_PPP;
    dev->addr_len = ETH_HLEN;
    dev->get_stats = net_ul_stats;
    dev->priv = file->private_data = (void *)ullp;
    dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

    dev_init_buffers(dev);

    dev->name = ullp->name;
    if ((i = register_netdev(dev))) {
	printk("%s: allocation failed.\n", dev->name);
	kfree (dev);
	kfree (ullp);
	return(i);
    }
    dev->start = 1;
    dev->tbusy = 0;

    ullp->dev = dev;

    MOD_INC_USE_COUNT;
    return(0);
}

static int
fop_ul_close(struct inode *inode, struct file * file)
{
    struct net_device	*dev;
    struct sk_buff	*skb;
    struct ullink *ullp, *ull0;
    unsigned long lock_flags;

    ullp = (struct ullink *)file->private_data;
    dev = ullp->dev;
    /*
     * junk all pending output
     */

    dev->start = 0;
    dev->tbusy = 1;

    if (dev->flags & IFF_UP) dev_close(dev);
    while((skb = skb_dequeue(&ullp->queue)) != NULL) {
	dev_kfree_skb(skb);
    }
    unregister_netdev(dev);

    spin_lock_irqsave (&ullHead_lock, lock_flags);
    if (ullp == ullHead) ullHead = ullp->next;
    else for (ull0 = ullHead; ull0 != NULL; ull0 = ull0->next) {
	if (ull0->next == ullp) {
	    ull0->next = ullp->next;
	    break;
	}
    }
    spin_unlock_irqrestore (&ullHead_lock, lock_flags);
    kfree(dev);
    kfree(ullp);
    MOD_DEC_USE_COUNT;
    return(0);
}

static ssize_t
fop_ul_write(struct file *file, const char *buffer,
	     size_t count, loff_t *ppos)
{
    unsigned long xsize;
    struct ullink *ullp;
    struct sk_buff *skb;

    ullp = (struct ullink *)file->private_data;

    xsize = (ullp->version == UL_V_NONE)
	? count: count - sizeof(struct ul_fs_header_base);
    if (xsize < 0) return(-EINVAL);
    if ((skb = dev_alloc_skb(xsize)) == NULL) {
	printk("%s: memory squeeze, dropping packet.\n", ullp->name);
	ullp->rx_dropped++;
	return 0;
    }
    skb->dev = ullp->dev;
    if (ullp->version == UL_V_NONE) {
	skb->protocol = htons(ETH_P_IP);
    } else {
	struct ul_fs_header_base ulhb;

	copy_from_user(&ulhb, buffer, sizeof(ulhb));
	skb->protocol = ulhb.protocol;
	buffer += sizeof(struct ul_fs_header_base);
    }
    memcpy(skb_put(skb, xsize), buffer, xsize);
    skb->mac.raw=skb->data;

    netif_rx(skb);
    ullp->rx_packets++;
    JIFDEBUG("write_ret");
    return(count);
}

static ssize_t
fop_ul_read(struct file *file, char *buffer,
	    size_t count, loff_t *ppos)
{
    struct ullink *ullp;
    struct sk_buff	*skb;

    ullp = (struct ullink *)file->private_data;
    if (count < 0) return(-EINVAL);
    if ((skb = skb_dequeue(&ullp->queue)) == NULL) return(0);

    ullp->tx_packets++;

    count = (skb->len < count) ? skb->len : count;
    memcpy(buffer, skb->data, count);

    dev_kfree_skb(skb);
    JIFDEBUG("read_ret");
    return(count);
}

static unsigned int
fop_ul_poll(struct file *file, poll_table *wait)
{
    struct ullink *ullp;
    unsigned ret = POLLOUT|POLLWRNORM;

    ullp = (struct ullink *)file->private_data;

    JIFDEBUG("poll");
    poll_wait(file, &ullp->wait, wait);
    if (skb_queue_len(&ullp->queue) > 0) ret |= POLLIN|POLLRDNORM;
#ifdef	DEBUG
    printk("ul@poll: ret=%x\n", ret);
#endif
    return(ret);
}

static struct file_operations fops_ul = {
	NULL,		/* seek */
	fop_ul_read,
	fop_ul_write,
	NULL, 		/* readdir */
	fop_ul_poll,
	NULL,		/* ioctl */
	NULL,		/* mmap */
	fop_ul_open,
	NULL,           /* flush */
	fop_ul_close,
	NULL,           /* fsync */
	NULL,           /* change */
	NULL,           /* revalidate */
	NULL,           /* lock */
};

#ifdef MODULE
#define userlink_init init_module

void
cleanup_module(void)
{

    if (MOD_IN_USE) {
	printk(UL_NAME": device busy.\n");
	return;
    } else {
	unregister_chrdev(ul_major, UL_NAME);
/*	UL_DEBUG(1,printk(UL_NAME": successfully unregistered.\n"));*/
    }
}

#endif /* MODULE */

int
userlink_init(void)
{
    int ul_new_major;
    if ((ul_new_major = register_chrdev(ul_major, UL_NAME, &fops_ul)) < 0) {
	printk(UL_NAME": registration failed.\n");
	return(-EIO);
    } else {
        if (ul_major == 0) ul_major = ul_new_major;
	printk(UL_NAME" version %s registered major %d\n",
	       version, ul_major);
	return(0);
    }
}

/*
 * Local variables:
 * compile-command: "gcc -DMODULE -DMODVERSIONS -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -g -fomit-frame-pointer -pipe -m486 -c userlink.c"
 * End:
 */
