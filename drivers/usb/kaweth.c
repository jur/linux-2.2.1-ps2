/****************************************************************
 *
 *     kawasaki.c - driver for KL5KUSB101 based USB->Ethernet
 *
 *     (c) 2000 Interlan Communications
 *
 *     Original author: The Zapman <zapman@interlan.net>
 *     	Inspired by, and much credit goes to Michael Rothwell <rothwell@interlan.net>
 *     		for the test equipment, help, and patience
 *			Based off of (and with thanks to) Petko Manolov's pegaus.c driver.
 *			Also many thanks to Joel Silverman at Kawasaki for providing the firmware
 *				and driver resources.
 *
 *		This program is free software; you can redistribute it and/or modify it
 *		under the terms of the GNU General Public License as published by the
 *		Free Software Foundation; either version 2, or (at your option) any
 *		later version.
 *
 *		This program is distributed in the hope that it will be useful,
 *		but WITHOUT ANY WARRANTY; without even the implied warranty of
 *		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *		GNU General Public License for more details.
 *		
 *		You should have received a copy of the GNU General Public License
 *		along with this program; if not, write to the Free Software Foundation,
 *		Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 
 *
 ****************************************************************/
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <asm/semaphore.h>

#include "kawethfw.h"

#define KAWETH_MTU								1514
#define KAWETH_BUF_SIZE						1664
#define KAWETH_TX_TIMEOUT					(1 * HZ)
#define KAWETH_FIRMWARE_BUF_SIZE	4096
#define KAWETH_CONTROL_TIMEOUT		(1 * HZ)

#define KAWETH_STATUS_BROKEN			0x0000001
#define KAWETH_STATUS_CLOSING			0x0000002

#define KAWETH_PACKET_FILTER_PROMISCUOUS		0x01
#define KAWETH_PACKET_FILTER_ALL_MULTICAST	0x02
#define KAWETH_PACKET_FILTER_DIRECTED				0x04
#define KAWETH_PACKET_FILTER_BROADCAST			0x08
#define KAWETH_PACKET_FILTER_MULTICAST			0x10

#define KAWETH_COMMAND_GET_ETHERNET_DESC		0x00
#define KAWETH_COMMAND_SET_PACKET_FILTER		0x02
#define KAWETH_COMMAND_GET_MAC							0x06
#define KAWETH_COMMAND_SET_URB_SIZE					0x08
#define KAWETH_COMMAND_SET_SOFS_WAIT				0x09
#define KAWETH_COMMAND_SCAN									0xFF

#define KAWETH_SOFS_TO_WAIT									0x05


MODULE_AUTHOR("Michael Zappe <zapman@interlan.net>");
MODULE_DESCRIPTION("KL5USB101 USB Ethernet driver");

static void *kaweth_probe(struct usb_device *dev, unsigned int ifnum);
static void kaweth_disconnect(struct usb_device *dev, void *ptr);

/****************************************************************
 *     kaweth_driver
 ****************************************************************/
static struct usb_driver kaweth_driver = {
	name:		"kaweth",
	probe:		kaweth_probe,
	disconnect:	kaweth_disconnect,
};
//  __attribute__ ((packed))
typedef __u8 eth_addr_t[6];

/****************************************************************
 *     usb_eth_dev
 ****************************************************************/
struct usb_eth_dev {
	char *name;
	__u16 vendor;
	__u16 device;
	void *pdata;
};

/****************************************************************
 *     kaweth_ethernet_configuration
 ****************************************************************/
struct kaweth_ethernet_configuration
{
	__u8 size;
	__u8 reserved1;
	__u8 reserved2;
	eth_addr_t hw_addr;
	__u32 statistics_mask;
	__u16 segment_size;
	__u16 max_multicast_filters;
	__u8 reserved3;
} __attribute__ ((packed));

/****************************************************************
 *     kaweth_device
 ****************************************************************/
struct kaweth_device
{
	spinlock_t device_lock;

	__u32 status;

	struct usb_device *dev;
	struct net_device *net;
	wait_queue_head_t control_wait;

	struct urb *rx_urb;
	struct urb *tx_urb;
	
	__u8 firmware_buf[KAWETH_FIRMWARE_BUF_SIZE];
	__u8 tx_buf[KAWETH_BUF_SIZE];
	__u8 rx_buf[KAWETH_BUF_SIZE];

	struct kaweth_ethernet_configuration configuration;

	struct net_device_stats stats;
} __attribute__ ((packed));

/****************************************************************
 *     usb_dev_id
 ****************************************************************/
static struct usb_eth_dev usb_dev_id[] = {
	{ "KLSI KL5KUSB101B", 0x05e9, 0x0008, NULL },
	{ "NetGear EA-101", 0x0846, 0x1001, NULL },
	{ "Peracom Enet", 0x0565, 0x0002, NULL },
	{ "Peracom Enet2", 0x0565, 0x0005, NULL },
	{ "3Com 3C19250", 0x0506, 0x03e8, NULL },
	{ "Linksys USB10T", 0x066b, 0x2202, NULL },
	{ "D-Link DSB-650C", 0x2001, 0x4000, NULL },
	{ "D-Link DSB-650", 0x0557, 0x2002, NULL },
	{ "AOX USB Ethernet", 0x03e8, 0x0008, NULL },
	{ "ADS USB-10BT", 0x06e1, 0x0008, NULL },
	{ "Entrega E45", 0x1645, 0x0005, NULL},
	{ "SMC 2202USB", 0x0707, 0x0100, NULL},
	{ "Correga K.K.", 0x07aa, 0x0001, NULL},
	{ NULL, 0, 0, NULL },
};

/****************************************************************
 *     kaweth_check_device_ids
 ****************************************************************/
static int kaweth_check_device_ids(__u16 vendor, __u16 product)
{
	int i=0;
	
	while (usb_dev_id[i].name)
	{
		if((usb_dev_id[i].vendor == vendor) && 
			 (usb_dev_id[i].device == product))
			{
				printk("%s connected...\n", usb_dev_id[i].name);
				
				return 0;
			}
		i++;
	}
	return	1;
}

/****************************************************************
 *     kaweth_control_complete
 ****************************************************************/
static void kaweth_control_complete(struct urb *urb)
{
	struct kaweth_device *kaweth = (struct kaweth_device *)urb->context;

	if(waitqueue_active(&kaweth->control_wait))
	{
		wake_up(&kaweth->control_wait);
	}
}

/****************************************************************
 *     kaweth_control
 ****************************************************************/
static int kaweth_control(struct kaweth_device *kaweth,
													unsigned int pipe, 
													__u8 request, 
													__u8 requesttype, 
													__u16 value, 
													__u16 index,
													void *data, 
													__u16 size, 
													int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	purb_t urb;
	devrequest *dr;
	int status;

	if(in_interrupt())
	{
		return -EBUSY;
	}

	urb = usb_alloc_urb(0);
	dr = kmalloc(sizeof(devrequest), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);

	if(!urb || !dr)
	{
		if(urb) kfree(urb);

		return -ENOMEM;
	}

	spin_lock(&kaweth->device_lock);

	urb->dev = kaweth->dev;
	urb->pipe = pipe;
	urb->setup_packet = (void *)dr;
	urb->transfer_flags = USB_DISABLE_SPD;
	urb->transfer_buffer = (void *)data;
	urb->transfer_buffer_length = size;
	urb->actual_length = size;
	urb->complete = kaweth_control_complete;
	urb->context = kaweth;

	dr->requesttype = requesttype;
	dr->request = request;
	dr->value = value;
	dr->index = index;
	dr->length = size;

	printk("Request type: %x  Request: %x  Value: %x Index: %x Length: %x\n",
				 (int)dr->requesttype,
				 (int)dr->request,
				 (int)dr->value,
				 (int)dr->index,
				 (int)dr->length); 

	init_waitqueue_head(&kaweth->control_wait);

	current->state = TASK_INTERRUPTIBLE;
	
	add_wait_queue(&kaweth->control_wait, &wait);

	status = usb_submit_urb(urb);

//	printk("pipe: %x\n", pipe);
//	printk("maxpacket: %x\n", usb_maxpacket(kaweth->dev, pipe, usb_pipeout(pipe)));

	if(status)
	{		
		usb_free_urb(urb);
		remove_wait_queue(&kaweth->control_wait, &wait);
		spin_unlock(&kaweth->device_lock);
		return status;
	}

	if(urb->status == -EINPROGRESS)
	{
		while(timeout && urb->status == -EINPROGRESS)
		{
			status = timeout = schedule_timeout(timeout);
		}
	}
	else
	{
		status = 1;
	}

	remove_wait_queue(&kaweth->control_wait, &wait);

	if(!status)
	{
		usb_unlink_urb(urb);
	
		printk("kaweth timeout\n");

		status = -ETIMEDOUT;
	}
	else
	{
		status = urb->status;

		if(urb->status)
		{
			printk("kaweth control message failed (urb addr: %x)\n", (int)urb);
			
			usb_clear_halt(kaweth->dev, usb_rcvctrlpipe(kaweth->dev, 0));
			usb_clear_halt(kaweth->dev, usb_sndctrlpipe(kaweth->dev, 0));

			usb_unlink_urb(urb);
		}
	}

	spin_unlock(&kaweth->device_lock);

	printk("Actual length: %d, length %d\n", urb->actual_length, urb->transfer_buffer_length);

//	printk("pipe: %x\n", pipe);
//	printk("maxpacket: %x\n", usb_maxpacket(kaweth->dev, pipe, usb_pipeout(pipe)));

	usb_free_urb(urb);
	kfree(dr);
	
	return status;	
}

/****************************************************************
 *     kaweth_read_configuration
 ****************************************************************/
static int kaweth_read_configuration(struct kaweth_device *kaweth)
{
	int retval;

	printk("Reading kaweth configuration\n");

	retval = kaweth_control(kaweth,
									usb_rcvctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_GET_ETHERNET_DESC,
									USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE,
									0,
									0,
									(void *)&kaweth->configuration,
									sizeof(kaweth->configuration),
									KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_urb_size
 ****************************************************************/
static int kaweth_set_urb_size(struct kaweth_device *kaweth, __u16 urb_size)
{
	int retval;

	printk("Setting URB size to %d\n", (unsigned)urb_size);

	retval = kaweth_control(kaweth,
									usb_sndctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_SET_URB_SIZE,
									USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
									urb_size,
									0,
									(void *)&kaweth->firmware_buf,
									0,
									KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_sofs_wait
 ****************************************************************/
static int kaweth_set_sofs_wait(struct kaweth_device *kaweth, __u16 sofs_wait)
{
	int retval;

	printk("Set SOFS wait to %d\n", (unsigned)sofs_wait);

	retval = kaweth_control(kaweth,
									usb_sndctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_SET_SOFS_WAIT,
									USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
									sofs_wait,
									0,
									(void *)&kaweth->firmware_buf,
									0,
									KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_set_receive_filter
 ****************************************************************/
static int kaweth_set_receive_filter(struct kaweth_device *kaweth, __u16 receive_filter)
{
	int retval;

	printk("Set receive filter to %d\n", (unsigned)receive_filter);

	retval = kaweth_control(kaweth,
									usb_sndctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_SET_PACKET_FILTER,
									USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
									receive_filter,
									0,
									(void *)&kaweth->firmware_buf,
									0,
									KAWETH_CONTROL_TIMEOUT);

	return retval;
}

/****************************************************************
 *     kaweth_download_firmware
 ****************************************************************/
static int kaweth_download_firmware(struct kaweth_device *kaweth, 
																		__u8 *data, 
																		__u16 data_len,
																		__u8 interrupt,
																		__u8 type)
{	
	if(data_len > KAWETH_FIRMWARE_BUF_SIZE)
	{
		printk("Firmware too big: %d\n", data_len);
		
		return -ENOSPC;
	}
	
	memcpy(kaweth->firmware_buf, data, data_len);
	
	kaweth->firmware_buf[2] = (data_len & 0xFF) - 7;
	kaweth->firmware_buf[3] = data_len >> 8;
	kaweth->firmware_buf[4] = type;
	kaweth->firmware_buf[5] = interrupt;

	printk("Downloading firmware at %x to kaweth device at %x...\n", (int)data, (int)kaweth);
	printk("Firmware length: %d\n", data_len);

	return kaweth_control(kaweth,
									usb_sndctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_SCAN,
									USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
									0,
									0,
									(void *)&kaweth->firmware_buf,
									data_len,
									KAWETH_CONTROL_TIMEOUT);
}

/****************************************************************
 *     kaweth_trigger_firmware
 ****************************************************************/
static int kaweth_trigger_firmware(struct kaweth_device *kaweth,
																	 __u8 interrupt)
{
	kaweth->firmware_buf[0] = 0xB6;
	kaweth->firmware_buf[1] = 0xC3;
	kaweth->firmware_buf[2] = 1;
	kaweth->firmware_buf[3] = 0;
	kaweth->firmware_buf[4] = 6;
	kaweth->firmware_buf[5] = 100;
	kaweth->firmware_buf[6] = 0;
	kaweth->firmware_buf[7] = 0;
	
	printk("Triggering firmware\n");

	return kaweth_control(kaweth,
									usb_sndctrlpipe(kaweth->dev, 0),
									KAWETH_COMMAND_SCAN,
									USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
									0,
									0,
									(void *)&kaweth->firmware_buf,
									8,
									KAWETH_CONTROL_TIMEOUT);
}

/****************************************************************
 *     kaweth_reset
 ****************************************************************/
static int kaweth_reset(struct kaweth_device *kaweth)
{
	int result;

	result = usb_set_configuration(kaweth->dev, 1);
	
	udelay(10000);

	return result;
}

static void kaweth_usb_receive(struct urb *);

/****************************************************************
 *     kaweth_resubmit_rx_urb
 ****************************************************************/
static inline void kaweth_resubmit_rx_urb(struct kaweth_device *kaweth)
{
	int result;
	
	memset(kaweth->rx_urb, 0, sizeof(*kaweth->rx_urb));

	spin_lock_init(&kaweth->rx_urb->lock);

	FILL_BULK_URB(kaweth->rx_urb,
								kaweth->dev,
								usb_rcvbulkpipe(kaweth->dev, 1),
								kaweth->rx_buf,
								KAWETH_BUF_SIZE,
								kaweth_usb_receive,
								kaweth);

//	kaweth->rx_urb->transfer_flags = USB_DISABLE_SPD | USB_URB_EARLY_COMPLETE;

	if((result = usb_submit_urb(kaweth->rx_urb)))
	{
		printk("kaweth: resubmitting rx_urb %d failed\n", result);
	}
}

/****************************************************************
 *     kaweth_usb_receive
 ****************************************************************/
static void kaweth_usb_receive(struct urb *urb)
{
	struct kaweth_device *kaweth = urb->context;
	struct net_device *net = kaweth->net;
	
	int count = urb->actual_length,
			count2 = urb->transfer_buffer_length;
			
	__u16 pkt_len = *(__u16 *)kaweth->rx_buf;

	struct sk_buff *skb;

	if(kaweth->status & KAWETH_STATUS_CLOSING)
	{
		return;
	}
	
#define RX_FAILED		kaweth_resubmit_rx_urb(kaweth);			\
										return

	if(urb->status && urb->status != -EREMOTEIO && count != 1) 
	{
		printk("%s: RX status: %d count: %d packet_len: %d\n", 
					 net->name, urb->status, count, (int)pkt_len);

		RX_FAILED;
	}

	if(kaweth->net && (count > 2))
	{
		if(pkt_len > (count - 2))
		{
			printk("kaweth error: packet length too long for USB frame (pkt_len: %x, count: %x)\n",
						 pkt_len, count);

			printk("Packet len & 2047: %x\n", pkt_len & 2047);
			printk("Count 2: %x\n", count2);

			RX_FAILED;
		}
		
		if(!(skb = dev_alloc_skb(pkt_len+2)))
		{
			RX_FAILED;
		}

		skb->dev = net;

		eth_copy_and_sum(skb, kaweth->rx_buf + 2, pkt_len, 0);
		
		skb_put(skb, pkt_len);

		skb->protocol = eth_type_trans(skb, net);
		
		netif_rx(skb);
		
		kaweth->stats.rx_packets++;
		kaweth->stats.rx_bytes += pkt_len;
	}

	kaweth_resubmit_rx_urb(kaweth);
}

/****************************************************************
 *     kaweth_open
 ****************************************************************/
static int kaweth_open(struct net_device *net)
{
	struct kaweth_device *kaweth = (struct kaweth_device *)net->priv;

	printk("Dev usage: %d\n", kaweth->dev->refcnt.counter);

	printk("Opening network device...\n");

	kaweth_resubmit_rx_urb(kaweth);

	netif_start_queue(net);

	MOD_INC_USE_COUNT;

	return 0;
}

/****************************************************************
 *     kaweth_close
 ****************************************************************/
static int kaweth_close(struct net_device *net)
{
	struct kaweth_device *kaweth = net->priv;

	netif_stop_queue(net);

	kaweth->status |= KAWETH_STATUS_CLOSING;

	usb_unlink_urb(kaweth->rx_urb);

	kaweth->status &= ~KAWETH_STATUS_CLOSING;

	MOD_DEC_USE_COUNT;

	printk("Dev usage: %d\n", kaweth->dev->refcnt.counter);

	return 0;
}

/****************************************************************
 *     kaweth_ioctl
 ****************************************************************/
static int kaweth_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
//	__u16 *data = (__u16 *)&rq->ifr_data;
//	struct kaweth_device *kaweth = net->priv;
/*
	switch(cmd) {
		default:
			return -EOPNOTSUPP;
	}
*/
	return -EOPNOTSUPP;
}

/****************************************************************
 *     kaweth_usb_transmit_complete
 ****************************************************************/
static void kaweth_usb_transmit_complete(struct urb *urb)
{
	struct kaweth_device *kaweth = urb->context;

	spin_lock(&kaweth->device_lock);

	if (urb->status)
		printk("%s: TX status %d", kaweth->net->name, urb->status);

	netif_wake_queue(kaweth->net);

	spin_unlock(&kaweth->device_lock);
}

/****************************************************************
 *     kaweth_start_xmit
 ****************************************************************/
static int kaweth_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct kaweth_device *kaweth = net->priv;
//	int count = ((skb->len) & 0x3f) ? skb->len+2 : skb->len+3;
	int count = skb->len;
	
	int res;

	spin_lock(&kaweth->device_lock);

	netif_stop_queue(net);

	*((__u16 *)kaweth->tx_buf) = skb->len;

	memcpy(kaweth->tx_buf + 2, skb->data, skb->len);

	memset(kaweth->tx_urb, 0, sizeof(*kaweth->tx_urb));
	spin_lock_init(&kaweth->tx_urb->lock);

	FILL_BULK_URB(kaweth->tx_urb,
							kaweth->dev,
							usb_sndbulkpipe(kaweth->dev, 2),
							kaweth->tx_buf,
							count + 2,
							kaweth_usb_transmit_complete,
							kaweth);

	if((res = usb_submit_urb(kaweth->tx_urb)))
	{
		warn("kaweth failed tx_urb %d", res);
		kaweth->stats.tx_errors++;
		
		netif_start_queue(net);
	} 
	else 
	{
		kaweth->stats.tx_packets++;
		kaweth->stats.tx_bytes += skb->len;
		net->trans_start = jiffies;
	}

	dev_kfree_skb(skb);

	spin_unlock(&kaweth->device_lock);

	return 0;
}

/****************************************************************
 *     kaweth_set_rx_mode
 ****************************************************************/
static void kaweth_set_rx_mode(struct net_device *net)
{
	struct kaweth_device *kaweth = net->priv;
	int result;
	
	__u16 packet_filter_bitmap = KAWETH_PACKET_FILTER_DIRECTED |
															 KAWETH_PACKET_FILTER_BROADCAST |
															 KAWETH_PACKET_FILTER_MULTICAST;

	printk("Setting Rx mode\n");
	
	netif_stop_queue(net);

	if (net->flags & IFF_PROMISC) 
	{
		packet_filter_bitmap |= KAWETH_PACKET_FILTER_PROMISCUOUS;
	} 
	else if ((net->mc_count) || //  > multicast_filter_limit) ||
					 (net->flags & IFF_ALLMULTI)) 
	{
		packet_filter_bitmap |= KAWETH_PACKET_FILTER_ALL_MULTICAST;
	}

	result = kaweth_control(kaweth,
								 usb_sndctrlpipe(kaweth->dev, 0),
								 KAWETH_COMMAND_SET_PACKET_FILTER,
								 USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE,
								 packet_filter_bitmap,
								 0,
								 (void *)&kaweth->firmware_buf,
								 0,
								 KAWETH_CONTROL_TIMEOUT);

	if(result < 0)
	{
		printk("Failed to set Rx mode: %d\n", result);
	}
	else
	{
		printk("%s: set Rx mode to %d\n", net->name, packet_filter_bitmap);
	}
		
	netif_wake_queue(net);
}

/****************************************************************
 *     kaweth_netdev_stats
 ****************************************************************/
static struct net_device_stats *kaweth_netdev_stats(struct net_device *dev)
{
	return &((struct kaweth_device *)dev->priv)->stats;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 0)
/****************************************************************
 *     kaweth_tx_timeout
 ****************************************************************/
static void kaweth_tx_timeout(struct net_device *net)
{
	struct kaweth_device *kaweth = net->priv;

	printk("%s: Tx timed out. Reseting...\n", net->name);
	kaweth->stats.tx_errors++;
	net->trans_start = jiffies;

	usb_unlink_urb(kaweth->tx_urb);

	netif_wake_queue(net);
}
#endif

/****************************************************************
 *     kaweth_probe
 ****************************************************************/
static void *kaweth_probe(struct usb_device *dev, unsigned int ifnum)
{
//	struct net_device *net;
//	eth_addr_t mac_addr;
	struct kaweth_device *kaweth;
	const eth_addr_t bcast_addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int result = 0;
	int i;
	
	printk("Kawasaki Device Probe (Device number:%x): 0x%4.4x:0x%4.4x\n",
				 dev->devnum, 
				 (int)dev->descriptor.idVendor, 
				 (int)dev->descriptor.idProduct);

	printk("Device at %p\n", dev);

	printk("Descriptor length: %x type: %x\n", 
				 (int)dev->descriptor.bLength,
				 (int)dev->descriptor.bDescriptorType);

	if (kaweth_check_device_ids(dev->descriptor.idVendor, 
															dev->descriptor.idProduct)) 
	{
		return NULL;
	}

	if(!(kaweth = kmalloc(sizeof(struct kaweth_device), GFP_KERNEL)))
	{
		printk("out of memory allocating device structure\n");
		return NULL;
	}

	memset(kaweth, 0, sizeof(*kaweth));

	kaweth->dev = dev;
	kaweth->status = 0;
	kaweth->net = NULL;
	kaweth->device_lock = SPIN_LOCK_UNLOCKED;

	if((result = kaweth_read_configuration(kaweth)) < 0)
	{
//		printk("Resetting (jiffies: %x)...\n", jiffies);
		printk("Resetting...\n");

		kaweth_reset(kaweth);

//		printk("Dowloading firmware (jiffies: %x)...\n", jiffies);
												 
		if((result = kaweth_download_firmware(kaweth, kaweth_new_code, len_kaweth_new_code, 100, 2)) < 0)
		{
			printk("Error downloading firmware (%d), no net device created\n", result);

			kfree(kaweth);
			
			return NULL;
		}

//		printk("Dowloading firmware fix (jiffies: %x)...\n", jiffies);

		if((result = kaweth_download_firmware(kaweth, kaweth_new_code_fix, len_kaweth_new_code_fix, 100, 3)) < 0)
		{
			printk("Error downloading firmware fix (%d), no net device created\n", result);

			kfree(kaweth);

			return NULL;
		}

//		printk("Dowloading firmware trigger (jiffies: %x)...\n", jiffies);

		if((result = kaweth_trigger_firmware(kaweth, 100)) < 0)
		{
			printk("Error triggering firmware (%d), no net device created\n", result);

			kfree(kaweth);

			return NULL;
		}

		udelay(1000);

		printk("Resetting device (jiffies: %lx)...\n", jiffies);

		if(kaweth_reset(kaweth))
		{
			printk("Error resetting device\n");

			kfree(kaweth);

			return NULL;
		}

		printk("Reset device (jiffies: %lx)...\n", jiffies);
	}

	result = kaweth_read_configuration(kaweth);

	printk("Statitstics collection: %x\n", kaweth->configuration.statistics_mask);
	printk("Multicast filter limit: %x\n", kaweth->configuration.max_multicast_filters & ((1 << 15) - 1));
	printk("MTU: %x\n", kaweth->configuration.segment_size);
	printk("Read MAC address %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
				 (int)kaweth->configuration.hw_addr[0],
				 (int)kaweth->configuration.hw_addr[1],
				 (int)kaweth->configuration.hw_addr[2],
				 (int)kaweth->configuration.hw_addr[3],
				 (int)kaweth->configuration.hw_addr[4],
				 (int)kaweth->configuration.hw_addr[5]);

	if(!memcmp(&kaweth->configuration.hw_addr, &bcast_addr, sizeof(bcast_addr)))
	{
		printk("Firmware not functioning properly, no net device created\n");

		kfree(kaweth);

		return NULL;
	}

	for(i = 0; i < sizeof(kaweth->configuration); i++)
	{
		printk(":%2.2x:", (unsigned int)((unsigned char *)&kaweth->configuration)[i]);
	}

	printk("\n");

	if(result < 0)
	{
		printk("Error reading configuration (%d), no net device created\n", result);

		return kaweth;
	}

	result = kaweth_set_urb_size(kaweth, KAWETH_BUF_SIZE);

	if(result < 0)
	{
		printk("Error setting URB size\n");

		return kaweth;
	}
	
	result = kaweth_set_sofs_wait(kaweth, KAWETH_SOFS_TO_WAIT);

	if(result < 0)
	{
		printk("Error setting SOFS wait\n");

		return kaweth;
	}

	result = kaweth_set_receive_filter(kaweth, KAWETH_PACKET_FILTER_DIRECTED |
															 KAWETH_PACKET_FILTER_BROADCAST |
															 KAWETH_PACKET_FILTER_MULTICAST);

	if(result < 0)
	{
		printk("Error setting receive filter\n");

		return kaweth;
	}
	
	printk("Initializing net device...\n");

	kaweth->tx_urb = usb_alloc_urb(0);
	kaweth->rx_urb = usb_alloc_urb(0);

	kaweth->net = init_etherdev(0, 0);

	memcpy(kaweth->net->broadcast, &bcast_addr, sizeof(bcast_addr));
	memcpy(kaweth->net->dev_addr, &kaweth->configuration.hw_addr, sizeof(kaweth->configuration.hw_addr));
	 
	kaweth->net->priv = kaweth;
	kaweth->net->open = kaweth_open;
	kaweth->net->stop = kaweth_close;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 0)
	kaweth->net->watchdog_timeo = KAWETH_TX_TIMEOUT;
	kaweth->net->tx_timeout = kaweth_tx_timeout;
#endif
	
	kaweth->net->do_ioctl = kaweth_ioctl;
	kaweth->net->hard_start_xmit = kaweth_start_xmit;
	kaweth->net->set_multicast_list = kaweth_set_rx_mode;
	kaweth->net->get_stats = kaweth_netdev_stats;
	kaweth->net->mtu = kaweth->configuration.segment_size;

	memset(&kaweth->stats, 0, sizeof(kaweth->stats));

	printk("kaweth interface created at %s\n", kaweth->net->name);
								
	printk("Kaweth probe returning...\n");

	return kaweth;
}

/****************************************************************
 *     kaweth_disconnect
 ****************************************************************/
static void kaweth_disconnect(struct usb_device *dev, void *ptr)
{
	struct kaweth_device *kaweth = ptr;

	printk("Unregistering kaweth\n");

	if (!kaweth) {
		warn("unregistering non-existant device");
		return;
	}

	if(kaweth->net)
	{
		if(kaweth->net->flags & IFF_UP)
		{
			printk("Closing net device\n");
			
			dev_close(kaweth->net);
		}

		printk("Unregistering net device\n");
		
		unregister_netdev(kaweth->net);
	}

	usb_free_urb(kaweth->rx_urb);
	usb_free_urb(kaweth->tx_urb);

	kfree(kaweth);
}


/****************************************************************
 *     kaweth_init
 ****************************************************************/
int __init kaweth_init(void)
{
	printk("Kawasaki USB->Ethernet Driver loading...\n");

	return usb_register(&kaweth_driver);
}

/****************************************************************
 *     kaweth_exit
 ****************************************************************/
void __exit kaweth_exit(void)
{
	usb_deregister(&kaweth_driver);
}

module_init(kaweth_init);
module_exit(kaweth_exit);

