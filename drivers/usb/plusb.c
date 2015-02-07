/*****************************************************************************/

/*
 *      plusb.c  --  prolific pl-2302 driver.
 *
 *      Copyright (C) 2000  Deti Fliegl (deti@fliegl.de)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *
 *  $Id: plusb.c,v 1.2 2001/02/06 09:14:10 abe Exp $
 *
 */

/*****************************************************************************/

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#undef DEBUG
#include <linux/usb.h>

#include "plusb.h"

/* --------------------------------------------------------------------- */

#define NRPLUSB 4

/*-------------------------------------------------------------------*/

static plusb_t plusb[NRPLUSB];

#ifdef CONFIG_PS2
/*-------------------------------------------------------------------*
 *                     COMPLETION HANDLERS                           *
 *-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*
 * completion handler for compatibility wrappers (sync control/bulk) *
 *-------------------------------------------------------------------*/
static void plusb_api_blocking_completion(urb_t *urb)
{
	api_wrapper_data *awd = (api_wrapper_data *)urb->context;

	if (waitqueue_active(awd->wakeup))
		wake_up(awd->wakeup);
#if 0
	else
		dbg("(blocking_completion): waitqueue empty!"); 
		// even occurs if urb was unlinked by timeout...
#endif
}

/*-------------------------------------------------------------------*
 *                         COMPATIBILITY STUFF                       *
 *-------------------------------------------------------------------*/

// Starts urb and waits for completion or timeout
static int plusb_start_wait_urb(urb_t *urb, int timeout, int* actual_length)
{ 
	DECLARE_WAITQUEUE(wait, current);
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	api_wrapper_data awd;
	int status;
  
	awd.wakeup = &wqh;
	awd.handler = 0;
	init_waitqueue_head(&wqh); 	
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&wqh, &wait);
	urb->context = &awd;
	status = usb_submit_urb(urb);
	if (status) {
		// something went wrong
		usb_free_urb(urb);
		remove_wait_queue(&wqh, &wait);
		return status;
	}

{ int i; for (i = 0; i < 100000000; i++) ; }

	if (urb->status == -EINPROGRESS) {
		while (timeout && urb->status == -EINPROGRESS)
			status = timeout = schedule_timeout(timeout);
	} else
		status = 1;

	remove_wait_queue(&wqh, &wait);

	if (!status) {
		// timeout
		printk("usb_control/bulk_msg: timeout\n");
		usb_unlink_urb(urb);  // remove urb safely
		status = -ETIMEDOUT;
	} else
		status = urb->status;

	if (actual_length)
		*actual_length = urb->actual_length;

	usb_free_urb(urb);
  	return status;
}

/*-------------------------------------------------------------------*/
/* compatibility wrapper, builds bulk urb, and waits for completion */
/* synchronous behavior */

int plusb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, 
			void *data, int len, int *actual_length, int timeout)
{
	urb_t *urb;

	if (len < 0)
		return -EINVAL;

	urb=usb_alloc_urb(0);
	if (!urb)
		return -ENOMEM;

	FILL_BULK_URB(urb,usb_dev,pipe,(unsigned char*)data,len,   /* build urb */
			(usb_complete_t)plusb_api_blocking_completion,0);

	return plusb_start_wait_urb(urb,timeout,actual_length);
}
#endif

/* --------------------------------------------------------------------- */
static int plusb_add_buf_tail (plusb_t *s, struct list_head *dst, struct list_head *src)
{
	unsigned long flags;
	struct list_head *tmp;
	int ret = 0;

	spin_lock_irqsave (&s->lock, flags);

	if (list_empty (src)) {
		// no elements in source buffer
		ret = -1;
		goto err;
	}
	tmp = src->next;
	list_del (tmp);
	list_add_tail (tmp, dst);

  err:	spin_unlock_irqrestore (&s->lock, flags);
	return ret;
}
/*-------------------------------------------------------------------*/

static int plusb_my_bulk(plusb_t *s, int pipe, void *data, int size, int *actual_length)
{
	int ret;

	dbg("plusb_my_bulk: len:%d",size);

#ifdef CONFIG_PS2
	ret=plusb_bulk_msg(s->usbdev, pipe, data, size, actual_length, 500);
#else
	ret=usb_bulk_msg(s->usbdev, pipe, data, size, actual_length, 500);
#endif
	if(ret<0) {
		err("plusb: usb_bulk_msg failed(%d)",ret);
	}
	
	if( ret == -EPIPE ) {
		warn("CLEAR_FEATURE request to remove STALL condition.");
		if(usb_clear_halt(s->usbdev, usb_pipeendpoint(pipe)))
			err("request failed");
		}

	dbg("plusb_my_bulk: finished act: %d", *actual_length);		
	return ret;
}

/* --------------------------------------------------------------------- */

static void plusb_bh(void *context)
{
	plusb_t *s=context;
	struct net_device_stats *stats=&s->net_stats;
	int ret=0;
	int actual_length;
	skb_list_t *skb_list;
	struct sk_buff *skb;

	dbg("plusb_bh: i:%d",in_interrupt());

	while(!list_empty(&s->tx_skb_list)) {

		if(!(s->status&_PLUSB_TXOK))
			break; 
		
		skb_list = list_entry (s->tx_skb_list.next, skb_list_t, skb_list);
		if(!skb_list->state) {
			dbg("plusb_bh: not yet ready");
			schedule();
			continue;
		}

		skb=skb_list->skb;
		ret=plusb_my_bulk(s, usb_sndbulkpipe (s->usbdev, _PLUSB_BULKOUTPIPE),
		                  skb->data, skb->len, &actual_length);
	
		if(ret || skb->len != actual_length ||!(skb->len%64)) {
			plusb_my_bulk(s, usb_sndbulkpipe (s->usbdev, _PLUSB_BULKOUTPIPE),
		              NULL, 0, &actual_length);
		}

		if(!ret) {
			stats->tx_packets++;
			stats->tx_bytes+=skb->len;
                }
		else {
			stats->tx_errors++;
			stats->tx_aborted_errors++;
		}

		dbg("plusb_bh: dev_kfree_skb");

		dev_kfree_skb(skb);
		skb_list->state=0;
		plusb_add_buf_tail (s, &s->free_skb_list, &s->tx_skb_list);	
	}

	dbg("plusb_bh: finished");
	s->in_bh=0;
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PS2
static int plusb_net_xmit(struct sk_buff *skb, struct device *dev)
#else
static int plusb_net_xmit(struct sk_buff *skb, struct net_device *dev)
#endif
{
	plusb_t *s=dev->priv;
	skb_list_t *skb_list;
	int ret=NET_XMIT_SUCCESS;

	dbg("plusb_net_xmit: len:%d i:%d",skb->len,in_interrupt());

        if(!s->connected || list_empty(&s->free_skb_list)) {
		ret=NET_XMIT_CN;
		goto lab;
	}	

	plusb_add_buf_tail (s, &s->tx_skb_list, &s->free_skb_list);
	skb_list = list_entry (s->tx_skb_list.prev, skb_list_t, skb_list);
	skb_list->skb=skb;
	skb_list->state=1;

lab:
	if(s->in_bh)
		return ret;

	dbg("plusb_net_xmit: queue_task");

	s->in_bh=1;
	queue_task(&s->bh, &tq_scheduler);

	dbg("plusb_net_xmit: finished");
	return ret;

}

/* --------------------------------------------------------------------- */

static void plusb_bulk_complete(urb_t *purb)
{
	plusb_t *s=purb->context;

	dbg("plusb_bulk_complete: status:%d length:%d",purb->status,purb->actual_length);
	if(!s->connected)
		return;

	if( !purb->status) {
		struct sk_buff *skb;
		unsigned char *dst;
		int len=purb->transfer_buffer_length;
		struct net_device_stats *stats=&s->net_stats;

		skb=dev_alloc_skb(len);

		if(!skb) {
			err("plusb_bulk_complete: dev_alloc_skb(%d)=NULL, dropping frame",len);
			stats->rx_dropped++;
			return;
		}

		dst=(char *)skb_put(skb, len);
		memcpy( dst, purb->transfer_buffer, len);

		skb->dev=&s->net_dev;
		skb->protocol=eth_type_trans(skb, skb->dev);
		stats->rx_packets++;
		stats->rx_bytes+=len;
		netif_rx(skb);
	}
	else
		purb->status=0;
}

/* --------------------------------------------------------------------- */

static void plusb_int_complete(urb_t *purb)
{
	plusb_t *s=purb->context;
	s->status=((unsigned char*)purb->transfer_buffer)[0]&255;

#if 0
	if((s->status&0x3f)!=0x20) {
		warn("invalid device status %02X", s->status);
		return;
	}
#endif	
	if(!s->connected)
		return;

	if(s->status&_PLUSB_RXD) {
		int ret;
		
		if(s->bulkurb->status) {
			err("plusb_int_complete: URB still in use");
			return;
		}
		
		ret=usb_submit_urb(s->bulkurb);
		if(ret && ret!=-EBUSY) {
			err("plusb_int_complete: usb_submit_urb failed");
		}
	}
		
	if(purb->status || s->status!=160)
		dbg("status: %p %d buf: %02X", purb->dev, purb->status, s->status);

}

/* --------------------------------------------------------------------- */

static void plusb_free_all(plusb_t *s)
{
	struct list_head *skb;
	skb_list_t *skb_list;
	
	dbg("plusb_free_all");
	run_task_queue(&tq_immediate);	

	if(s->inturb) {
		dbg("unlink inturb");
		usb_unlink_urb(s->inturb);
	}

	if(s->inturb && s->inturb->transfer_buffer) {
		dbg("kfree inturb->transfer_buffer");
		kfree(s->inturb->transfer_buffer);
		s->inturb->transfer_buffer=NULL;
	}
	
	if(s->inturb) {
		dbg("free_urb inturb");
		usb_free_urb(s->inturb);
		s->inturb=NULL;
	}

	if(s->bulkurb) {
		dbg("unlink bulkurb");
		usb_unlink_urb(s->bulkurb);
	}
	
	if(s->bulkurb && s->bulkurb->transfer_buffer) {
		dbg("kfree bulkurb->transfer_buffer");
		kfree(s->bulkurb->transfer_buffer);
		s->bulkurb->transfer_buffer=NULL;
	}
	if(s->bulkurb) {
		dbg("free_urb bulkurb");
		usb_free_urb(s->bulkurb);
		s->bulkurb=NULL;
	}
	
	while(!list_empty(&s->free_skb_list)) {
		skb=s->free_skb_list.next;
		list_del(skb);
		skb_list = list_entry (skb, skb_list_t, skb_list);
		kfree(skb_list);
	}

	while(!list_empty(&s->tx_skb_list)) {
		skb=s->tx_skb_list.next;
		list_del(skb);
		skb_list = list_entry (skb, skb_list_t, skb_list);
		kfree(skb_list);	
	}
	dbg("plusb_free_all: finished");	
}

/*-------------------------------------------------------------------*/

static int plusb_alloc(plusb_t *s)
{
	int i;
	skb_list_t *skb;

	dbg("plusb_alloc");
	
	for(i=0 ; i < _SKB_NUM ; i++) {
		skb=kmalloc(sizeof(skb_list_t), GFP_KERNEL);
		if(!skb) {
			err("kmalloc for skb_list failed");
			goto reject;
		}
		memset(skb, 0, sizeof(skb_list_t));
		list_add(&skb->skb_list, &s->free_skb_list);
	}

	dbg("inturb allocation:");
	s->inturb=usb_alloc_urb(0);
	if(!s->inturb) {
		err("alloc_urb failed");
		goto reject;
	}

	dbg("bulkurb allocation:");	
	s->bulkurb=usb_alloc_urb(0);
	if(!s->bulkurb) {
		err("alloc_urb failed");
		goto reject;
	}
	
	dbg("bulkurb/inturb init:");
	s->inturb->dev=s->usbdev;
	s->inturb->pipe=usb_rcvintpipe (s->usbdev, _PLUSB_INTPIPE);
	s->inturb->transfer_buffer=kmalloc(64, GFP_KERNEL);
	if(!s->inturb->transfer_buffer) {
		err("kmalloc failed");
		goto reject;
	}
	
	s->inturb->transfer_buffer_length=1;
	s->inturb->complete=plusb_int_complete;
	s->inturb->context=s;
	s->inturb->interval=10;

	dbg("inturb submission:");
	if(usb_submit_urb(s->inturb)<0) {
		err("usb_submit_urb failed");
		goto reject;
	}

	dbg("bulkurb init:");
	s->bulkurb->dev=s->usbdev;
	s->bulkurb->pipe=usb_rcvbulkpipe (s->usbdev, _PLUSB_BULKINPIPE);
	s->bulkurb->transfer_buffer=kmalloc(_BULK_DATA_LEN, GFP_KERNEL);
	if(!s->bulkurb->transfer_buffer) {
		err("kmalloc failed");
		goto reject;
	}
	
	s->bulkurb->transfer_buffer_length=_BULK_DATA_LEN;
	s->bulkurb->complete=plusb_bulk_complete;
	s->bulkurb->context=s;
	
	dbg("plusb_alloc: finished");
	
	return 0;

  reject:
  	dbg("plusb_alloc: failed");
	
	plusb_free_all(s);
	return -ENOMEM;
}

/*-------------------------------------------------------------------*/

#ifdef CONFIG_PS2
static int plusb_net_open(struct device *dev)
#else
static int plusb_net_open(struct net_device *dev)
#endif
{
	plusb_t *s=dev->priv;
	
	dbg("plusb_net_open");
	
	if(plusb_alloc(s))
		return -ENOMEM;

	s->opened=1;
	MOD_INC_USE_COUNT;
	
	dbg("plusb_net_open: success");
	
	return 0;
	
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PS2
static int plusb_net_stop(struct device *dev)
#else
static int plusb_net_stop(struct net_device *dev)
#endif
{
	plusb_t *s=dev->priv;
	
	dbg("plusb_net_stop");	
	
	plusb_free_all(s);
	s->opened=0;
	MOD_DEC_USE_COUNT;
	dbg("plusb_net_stop:finished");
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PS2
static struct net_device_stats *plusb_net_get_stats(struct device *dev)
#else
static struct net_device_stats *plusb_net_get_stats(struct net_device *dev)
#endif
{
	plusb_t *s=dev->priv;
	
	dbg("net_device_stats");
	
	return &s->net_stats;
}

/* --------------------------------------------------------------------- */

static plusb_t *plusb_find_struct (void)
{
	int u;

	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s = &plusb[u];
		if (!s->connected)
			return s;
	}
	return NULL;
}

/* --------------------------------------------------------------------- */

static void plusb_disconnect (struct usb_device *usbdev, void *ptr)
{
	plusb_t *s = ptr;

	printk ("plusb_net_disconnect: Starting\n");
	
	dbg("plusb_disconnect");
	
	s->connected = 0;
	
	plusb_free_all(s);

	if(!s->opened && s->net_dev.name) {
		dbg("unregistering netdev: %s",s->net_dev.name);
		unregister_netdev(&s->net_dev);
		s->net_dev.name[0] = '\0';
#if (LINUX_VERSION_CODE < 0x020300)		
		kfree (s->net_dev.name);
		s->net_dev.name = NULL;
#endif
	}
	
	dbg("plusb_disconnect: finished");
	
	printk ("plusb_net_disconnect: Finished\n");
	
	MOD_DEC_USE_COUNT;
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PS2
int plusb_net_init(struct device *dev)
#else
int plusb_net_init(struct net_device *dev)
#endif
{
	dbg("plusb_net_init");
	
	dev->open=plusb_net_open;
	dev->stop=plusb_net_stop;
	dev->hard_start_xmit=plusb_net_xmit;
	dev->get_stats	= plusb_net_get_stats;
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags = IFF_POINTOPOINT|IFF_NOARP;

	
	dbg("plusb_net_init: finished");
	return 0;
}

/* --------------------------------------------------------------------- */

static void *plusb_probe (struct usb_device *usbdev, unsigned int ifnum)
{
	plusb_t *s;

	printk ("plusb_probe: Starting\n");

	if (usbdev) {
		printk("plusb: probe: vendor id 0x%x, device id 0x%x ifnum:%d\n",
		    usbdev->descriptor.idVendor, usbdev->descriptor.idProduct, ifnum);
	} else {
		printk ("plusb: usbdev is NULL!\n");
	}

	if (usbdev->descriptor.idVendor != 0x067b || usbdev->descriptor.idProduct > 0x1)
		return NULL;

	/* We don't handle multiple configurations */
	if (usbdev->descriptor.bNumConfigurations != 1)
		return NULL;

	printk ("plusb_probe: Looking for Struct\n");
	s = plusb_find_struct ();
	if (!s)
		return NULL;

	s->usbdev = usbdev;

	printk ("plusb_probe: Setting Configuration\n");
	if (usb_set_configuration (s->usbdev, usbdev->config[0].bConfigurationValue) < 0) {
		err("set_configuration failed");
		return NULL;
	}

	printk ("plusb_probe: Setting Interface\n");	
	if (usb_set_interface (s->usbdev, 0, 0) < 0) {
		err("set_interface failed");
		return NULL;
	}

	printk ("plusb_probe: Checking device name\n");
	
#if (LINUX_VERSION_CODE < 0x020300)
	{
		int i;
		
		/* EZA: find the device number... we seem to have lost it...*/
		for (i=0; i<NRPLUSB; i++) {
			if (&plusb[i] == s)
				break;
		}
	
		/* EZA: for Kernel version 2.2, the driver is responsible for
		   allocating this memory. For version 2.4, the rules
		   have apparently changed, but there is a nifty function
		   'init_netdev' that might make this easier...  It's in 
		   ../net/net_init.c - but can we get there from here?  (no)
		*/
		if(!s->net_dev.name) {
			s->net_dev.name = kmalloc(strlen("plusbXXXX"), GFP_KERNEL);
			sprintf (s->net_dev.name, "plusb%d", i);
			s->net_dev.init=plusb_net_init;
			s->net_dev.priv=s;
			
			printk ("plusb_probe: Registering Device\n");	
			if(!register_netdev(&s->net_dev))
				info("registered: %s", s->net_dev.name);
			else {
				err("register_netdev failed");
				s->net_dev.name[0] = '\0';
			}
			printk ("plusb_probe: Connected!\n");
		}
	}
#else
	/* Kernel version 2.3+ works a little bit differently  */
	if(!s->net_dev.name[0]) {
		strcpy(s->net_dev.name, "plusb%d");
		s->net_dev.init=plusb_net_init;
		s->net_dev.priv=s;
		if(!register_netdev(&s->net_dev))
			info("registered: %s", s->net_dev.name);
		else {
			err("register_netdev failed");
			s->net_dev.name[0] = '\0';
		}
	}
	
#endif
	s->connected = 1;
	printk ("plusb_probe: Set Connected\n");		

	if(s->opened) {
		dbg("net device already allocated, restarting USB transfers");
		plusb_alloc(s);
	}

	info("bound to interface: %d dev: %p", ifnum, usbdev);
	MOD_INC_USE_COUNT;
	return s;
}
/* --------------------------------------------------------------------- */

static struct usb_driver plusb_driver =
{
	name: "plusb",
	probe: plusb_probe,
	disconnect: plusb_disconnect,
};

/* --------------------------------------------------------------------- */

static int __init plusb_init (void)
{
	unsigned u;
	
	dbg("plusb_init");
	
	/* initialize struct */
	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s;
		dbg("plusb_init: u=%ud about to assign s\n", u);
		s= &plusb[u];
		dbg("plusb_init: u=%ud about to memset\n", u);
		memset (s, 0, sizeof (plusb_t));
		s->bh.routine = (void (*)(void *))plusb_bh;
		s->bh.data = s;
		dbg("plusb_init: u=%ud about to init list head\n", u);
		INIT_LIST_HEAD (&s->tx_skb_list);
		INIT_LIST_HEAD (&s->free_skb_list);
		dbg("plusb_init: u=%ud about to init spin lock\n", u);
		spin_lock_init (&s->lock);
	}
	
	dbg ("plusb_init: Initialized structure\n");
	
	/* register misc device */
	usb_register (&plusb_driver);

	dbg("plusb_init: driver registered");

	return 0;
}

/* --------------------------------------------------------------------- */

static void __exit plusb_cleanup (void)
{
	unsigned u;

	dbg("plusb_cleanup");
	for (u = 0; u < NRPLUSB; u++) {
		plusb_t *s = &plusb[u];
		if(s->net_dev.name[0]) {
			dbg("unregistering netdev: %s",s->net_dev.name);
			unregister_netdev(&s->net_dev);
		}
	}
	usb_deregister (&plusb_driver);
	dbg("plusb_cleanup: finished");
}

/* --------------------------------------------------------------------- */

MODULE_AUTHOR ("Deti Fliegl, deti@fliegl.de");
MODULE_DESCRIPTION ("PL-2302 USB Interface Driver for Linux (c)2000");


module_init (plusb_init);
module_exit (plusb_cleanup);

/* --------------------------------------------------------------------- */
