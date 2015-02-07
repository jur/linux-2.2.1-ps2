/*
 *  linux/drivers/video/ps2dma.c
 *  PlayStation 2 DMA driver
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
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
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/ps2/irq.h>

#include <linux/ps2/dev.h>
#include "ps2dma.h"
#include "ps2dev.h"

static int dma_intr_initialized = 0;

struct dma_channel ps2dma_channels[] = {
    { IRQ_DMAC_0, KSEG1ADDR(0x10008000), DMA_SENDCH, 0, "VIF0 DMA", },
    { IRQ_DMAC_1, KSEG1ADDR(0x10009000), DMA_SENDCH, 0, "VIF1 DMA", },
    { IRQ_DMAC_2, KSEG1ADDR(0x1000a000), DMA_SENDCH, 0, "GIF DMA", },
    { IRQ_DMAC_3, KSEG1ADDR(0x1000b000), DMA_RECVCH, 0, "fromIPU DMA", },
    { IRQ_DMAC_4, KSEG1ADDR(0x1000b400), DMA_SENDCH, 0, "toIPU DMA", },
    { IRQ_DMAC_8, KSEG1ADDR(0x1000d000), DMA_RECVCH, 1, "fromSPR DMA", },
    { IRQ_DMAC_9, KSEG1ADDR(0x1000d400), DMA_SENDCH, 1, "toSPR DMA", },
};

extern struct file_operations ps2spr_fops, ps2mem_fops;

/*
 *  memory page list management functions
 */

struct page_list *ps2pl_alloc(int pages)
{
    int i;
    struct page_list *list;

    if ((list = kmalloc(sizeof(struct page_list) + pages * sizeof(unsigned long), GFP_KERNEL)) == NULL)
	return NULL;
    list->pages = pages;

    for (i = 0; i < list->pages; i++) {
	if (!(list->page[i] = get_free_page(GFP_KERNEL))) {
	    /* out of memory */
	    while (--i >= 0)
		free_page(list->page[i]);
	    kfree(list);
	    return NULL;
	}
	DPRINT("ps2pl_alloc: %08X\n", list->page[i]);
    }
    return list;
}

struct page_list *ps2pl_realloc(struct page_list *list, int newpages)
{
    int i;
    struct page_list *newlist;

    if (list->pages >= newpages)
	return list;
    if ((newlist = kmalloc(sizeof(struct page_list) + newpages * sizeof(unsigned long), GFP_KERNEL)) == NULL)
	return NULL;

    memcpy(newlist->page, list->page, list->pages * sizeof(unsigned long));
    newlist->pages = newpages;
    for (i = list->pages; i < newpages; i++) {
	if (!(newlist->page[i] = get_free_page(GFP_KERNEL))) {
	    /* out of memory */
	    while (--i >= list->pages)
		free_page(newlist->page[i]);
	    kfree(newlist);
	    return NULL;
	}
    }
    kfree(list);
    return newlist;
}

void ps2pl_free(struct page_list *list)
{
    int i;

    for (i = 0; i < list->pages; i++) {
	free_page(list->page[i]);
	DPRINT("ps2pl_free: %08X\n", list->page[i]);
    }
    kfree(list);
}

int ps2pl_copy_from_user(struct page_list *list, void *from, long len)
{
    int size;
    int index = 0;

    if (list->pages < ((len + ~PAGE_MASK) >> PAGE_SHIFT))
	return -EINVAL;

    while (len) {
	size = len > PAGE_SIZE ? PAGE_SIZE : len;
	DPRINT("ps2pl_copy_from_user: %08X<-%08X %08X\n", list->page[index], from, size);
	if (copy_from_user((void *)list->page[index++], from, size))
	    return -EFAULT;
	from = (void *)((unsigned long)from + size);
	len -= size;
    }
    return 0;
}

int ps2pl_copy_to_user(void *to, struct page_list *list, long len)
{
    int size;
    int index = 0;

    if (list->pages < ((len + ~PAGE_MASK) >> PAGE_SHIFT))
	return -EINVAL;

    while (len) {
	size = len > PAGE_SIZE ? PAGE_SIZE : len;
	DPRINT("ps2pl_copy_to_user: %08X->%08X %08X\n", list->page[index], to, size);
	if (copy_to_user(to, (void *)list->page[index++], size))
	    return -EFAULT;
	to = (void *)((unsigned long)to + size);
	len -= size;
    }
    return 0;
}

/*
 *  make DMA tag
 */

static int ps2dma_make_tag_spr(unsigned long offset, int len, struct dma_tag **tagp, struct dma_tag **lastp)
{
    struct dma_tag *tag;

    DPRINT("ps2dma_make_tag_spr: %08X %08X\n", offset, len);
    if ((tag = kmalloc(sizeof(struct dma_tag) * 2, GFP_KERNEL)) == NULL)
	return -ENOMEM;
    *tagp = tag;

    tag->id = DMATAG_REF;
    tag->qwc = len >> 4;
    tag->addr = offset | (1 << 31);	/* SPR address */
    tag++;

    if (lastp)
	*lastp = tag;

    tag->id = DMATAG_END;
    tag->qwc = 0;

    return BUFTYPE_SPR;
}

static int ps2dma_make_tag_mem(unsigned long offset, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list *mem)
{
    struct dma_tag *tag;
    int sindex, eindex;
    unsigned long vaddr, next, end;

    DPRINT("ps2dma_make_tag_mem: %08X %08X\n", offset, len);
    end = offset + len;
    sindex = offset >> PAGE_SHIFT;
    eindex = (end - 1) >> PAGE_SHIFT;
    if ((tag = kmalloc(sizeof(struct dma_tag) * (eindex - sindex + 2), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    *tagp = tag;

    while (sindex <= eindex) {
	vaddr = mem->page[sindex] + (offset & ~PAGE_MASK);
	next = (offset + PAGE_SIZE) & PAGE_MASK;
	tag->id = DMATAG_REF;
	tag->qwc = (next < end ? next - offset : end - offset) >> 4;
	tag->addr = virt_to_bus((void *)vaddr);
	DPRINT("ps2dma_make_tag_mem: tag %08X %08X\n", tag->addr, tag->qwc);
	tag++;
	offset = next;
	sindex++;
    }

    if (lastp)
	*lastp = tag;

    tag->id = DMATAG_END;
    tag->qwc = 0;

    return BUFTYPE_MEM;
}

static int ps2dma_make_tag_user(unsigned long start, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list **memp)
{
    struct page_list *mem;

    DPRINT("ps2dma_make_tag_user: %08X %08X\n", start, len);
    if (memp == NULL)
	return -EINVAL;
    if ((mem = ps2pl_alloc((len + PAGE_SIZE - 1) >> PAGE_SHIFT)) == NULL)
	return -ENOMEM;
    if (ps2dma_make_tag_mem(0, len, tagp, lastp, mem) < 0) {
	ps2pl_free(mem);
	return -ENOMEM;
    }
    *memp = mem;
    return BUFTYPE_USER;
}

int ps2dma_make_tag(unsigned long start, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list **memp)
{
    struct vm_area_struct *vma;
    unsigned long offset;

    DPRINT("ps2dma_make_tag: %08X %08X\n", start, len);

    /* alignment check */
    if ((start & (DMA_TRUNIT - 1)) != 0 ||
	(len & (DMA_TRUNIT - 1)) != 0 || len <= 0)
	return -EINVAL;

    if ((vma = find_vma(current->mm, start)) == NULL)
	return -EINVAL;
    DPRINT("ps2dma_make_tag: vma %08X-%08X\n", vma->vm_start, vma->vm_end);

    /* get buffer type */
    if (vma->vm_file != NULL) {
	if (vma->vm_file->f_op == &ps2spr_fops) {
	    if (start + len > vma->vm_end)
		return -EINVAL;			/* illegal address range */
	    offset = start - vma->vm_start + vma->vm_offset;
	    return ps2dma_make_tag_spr(offset, len, tagp, lastp);
	}
	if (vma->vm_file->f_op == &ps2mem_fops) {
	    if (start + len > vma->vm_end)
		return -EINVAL;			/* illegal address range */
	    offset = start - vma->vm_start + vma->vm_offset;
	    return ps2dma_make_tag_mem(offset, len, tagp, lastp, (struct page_list *)vma->vm_file->private_data);
	}
    }

    return ps2dma_make_tag_user(start, len, tagp, lastp, memp);
}

/*
 *  common DMA interrupt handler
 */

void ps2dma_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    struct dma_channel *ch = (struct dma_channel *)dev_id;
    struct dma_device *dev;
    struct dma_devch *devch;
    struct dma_request *cur_req, *next_req;

    /* to prevent stray interrupt while DMA polling */
    *PS2_D_STAT = 1 << (irq - IRQ_DMAC);

    cur_req = ch->tail;
    if (cur_req == NULL)
	return;
    next_req = cur_req->next;

    if (cur_req->ops->isdone)
	if (!cur_req->ops->isdone(cur_req, ch))
	    return;			/* current request is not yet done */

    /* start next DMA request */
    if ((ch->tail = next_req) != NULL)
	next_req->ops->start(next_req, ch);

    /* current request is done */
    if ((dev = cur_req->device) != NULL) {
	DPRINT("ps2dma_intr_handler: direction=%d\n", ch->direction);
	devch = &dev->devch[ch->direction];
	DPRINT("ps2dma_intr_handler: qct=%d\n", devch->qct);
	devch->qsize -= cur_req->qsize;
	if (--devch->qct <= 0) {	/* request queue empty */
	    if (dev->empty_wq)
		wake_up_interruptible(&dev->empty_wq);
	    if (dev->intr_mask & (1 << ch->direction)) {
		dev->intr_flag |= 1 << ch->direction;
		if (dev->sig)
		    send_sig(dev->sig, dev->ts, 1);
	    }
	}
	if (devch->done_wq)
	    wake_up_interruptible(&devch->done_wq);
    }
    cur_req->ops->free(cur_req, ch);	/* free request structure */
}

void ps2dma_add_queue(struct dma_request *req, struct dma_channel *ch)
{
    flush_cache_all();

    if (ch->tail == NULL) {
	/* start first DMA request */
	ch->tail = ch->head = req;
	req->ops->start(req, ch);
    } else {
	ch->head->next = req;
	ch->head = req;
    }
}

/*
 * DMA operations for send
 */

static void dma_send_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;

    DPRINT("dma_send_start: %08X %08X %08X\n", ureq->tag->addr, ureq->tag->qwc);
    DMAREG(ch, PS2_Dn_TADR) = virt_to_bus(ureq->tag);
    DMAREG(ch, PS2_Dn_QWC) = 0;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0105;
}

static void dma_send_spr_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;

    DPRINT("dma_send_spr_start: %08X %08X %08X\n", ureq->tag->addr, ureq->tag->qwc, ureq->saddr);
    DMAREG(ch, PS2_Dn_SADR) = ureq->saddr;
    DMAREG(ch, PS2_Dn_TADR) = virt_to_bus(ureq->tag);
    DMAREG(ch, PS2_Dn_QWC) = 0;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0105;
}

static unsigned long dma_stop(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;
    unsigned long vaddr = ureq->vaddr;
    struct dma_tag *tag = ureq->tag;
    unsigned long maddr;

    /* DMA force break */
    DMABREAK(ch);
    maddr = DMAREG(ch, PS2_Dn_MADR);
    DPRINT("dma_stop :%08X\n", maddr);

    while (tag->qwc) {
	if (maddr >= tag->addr && maddr <= tag->addr + (tag->qwc << 4)) {
	    /* if maddr points the last address of DMA request,
	       the request is already finished */
	    if (maddr == tag->addr + (tag->qwc << 4) &&
		tag[1].qwc == 0)
		return 0;
	    return vaddr + (maddr - tag->addr);
	}
	vaddr += tag->qwc << 4;
	tag++;
    }
    return 0;		/* cannot get virtual address */
}

static void dma_free(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;

    DPRINT("dma_free %08X\n", ureq->mem);
    if (ureq->mem)
	ps2pl_free(ureq->mem);
    if (ureq->done)
	*ureq->done = 1;
    kfree(ureq->tag);
    kfree(ureq);
}

static struct dma_ops dma_send_ops =
{ dma_send_start, NULL, dma_stop, dma_free };
static struct dma_ops dma_send_spr_ops =
{ dma_send_spr_start, NULL, dma_stop, dma_free };

/*
 * DMA operations for receive
 */

static void dma_recv_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;

    ch->tagp = ureq->tag;
    DPRINT("dma_recv_start: %08X %08X\n", ch->tagp->addr, ch->tagp->qwc);
    DMAREG(ch, PS2_Dn_MADR) = ch->tagp->addr;
    DMAREG(ch, PS2_Dn_QWC) = ch->tagp->qwc;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0100;
    ch->tagp++;
}

static void dma_recv_spr_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = (struct udma_request *)req;

    ch->tagp = ureq->tag;
    DPRINT("dma_recv_spr_start: %08X %08X %08X\n", ch->tagp->addr, ch->tagp->qwc, ureq->saddr);
    DMAREG(ch, PS2_Dn_SADR) = ureq->saddr;
    DMAREG(ch, PS2_Dn_MADR) = ch->tagp->addr;
    DMAREG(ch, PS2_Dn_QWC) = ch->tagp->qwc;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0100;
    ch->tagp++;
}

static int dma_recv_isdone(struct dma_request *req, struct dma_channel *ch)
{
    DPRINT("dma_recv_isdone: %08X %08X\n", ch->tagp->addr, ch->tagp->qwc);
    if (ch->tagp->qwc <= 0)
	return 1;		/* chain DMA is finished */

    DMAREG(ch, PS2_Dn_MADR) = ch->tagp->addr;
    DMAREG(ch, PS2_Dn_QWC) = ch->tagp->qwc;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0100;
    ch->tagp++;
    return 0;			/* chain DMA is not finished */
}

static struct dma_ops dma_recv_ops =
{ dma_recv_start, dma_recv_isdone, dma_stop, dma_free };
static struct dma_ops dma_recv_spr_ops =
{ dma_recv_spr_start, dma_recv_isdone, dma_stop, dma_free };

/*
 * DMA operations for send/receive request list
 */

static void dma_list_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = (struct udma_request_list *)req;

    ureql->index = 0;
    ureql->ureq[ureql->index]->r.ops->start((struct dma_request *)ureql->ureq[ureql->index], ch);
}

static int dma_list_isdone(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = (struct udma_request_list *)req;
    struct udma_request *ureq = ureql->ureq[ureql->index];

    if (ureq->r.ops->isdone)
	if (!ureq->r.ops->isdone((struct dma_request *)ureq, ch))
	    return 0;		/* not finished */

    if (++ureql->index < ureql->reqs) {
	ureql->ureq[ureql->index]->r.ops->start((struct dma_request *)ureql->ureq[ureql->index], ch);
	return 0;		/* not finished */
    }
    return 1;			/* finished */
}

static unsigned long dma_list_stop(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = (struct udma_request_list *)req;
    struct udma_request *ureq = ureql->ureq[ureql->index];

    return ureq->r.ops->stop((struct dma_request *)ureq, ch);
}

static void dma_list_free(struct dma_request *req, struct dma_channel *ch)
{
    int i;
    struct udma_request_list *ureql = (struct udma_request_list *)req;

    for (i = 0; i < ureql->reqs; i++)
	ureql->ureq[i]->r.ops->free((struct dma_request *)ureql->ureq[i], ch);
    kfree(ureql);
}

static struct dma_ops dma_send_list_ops =
{ dma_list_start, NULL, dma_list_stop, dma_list_free };
static struct dma_ops dma_recv_list_ops =
{ dma_list_start, dma_list_isdone, dma_list_stop, dma_list_free };

/*
 *  User mode DMA functions
 */

int ps2dma_check_and_add_queue(struct dma_request *req, struct dma_devch *devch, int nonblock)
{
    /* check queue limit */
    while (devch->qct >= devch->qlimit ||
	   (devch->qsize + req->qsize > DMA_USER_LIMIT && devch->qsize != 0)) {
	if (nonblock)
	    return -EAGAIN;
	interruptible_sleep_on(&devch->done_wq);
	if (signal_pending(current))
	    return -ERESTARTSYS;		/* signal arrived */
    }
    devch->qct++;
    devch->qsize += req->qsize;

    ps2dma_add_queue(req, devch->channel);
    return 0;
}

int ps2dma_write(struct dma_device *dev, struct ps2_packet *pkt, int nonblock)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    int result;

    DPRINT("dma_write %08X %08X %d\n", pkt->ptr, pkt->len, nonblock);

    /* alignment check */
    if (((unsigned long)pkt->ptr & (DMA_TRUNIT - 1)) != 0 ||
	(pkt->len & (DMA_TRUNIT - 1)) != 0 || pkt->len <= 0)
	return -EINVAL;

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_request(&ureq->r, &dma_send_ops, dev, pkt->len);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->done = NULL;

    cli();
    if ((result = ps2dma_make_tag_user((unsigned long)pkt->ptr, pkt->len, &ureq->tag, NULL, &ureq->mem)) < 0) {
	sti();
	kfree(ureq);
	return result;
    }
    if ((result = ps2pl_copy_from_user(ureq->mem, pkt->ptr, pkt->len))) {
	sti();
	ps2pl_free(ureq->mem);
	kfree(ureq->tag);
	kfree(ureq);
	return result;
    }
    result = ps2dma_check_and_add_queue((struct dma_request *)ureq, devch, nonblock);
    if (result < 0) {
	sti();
	dma_free((struct dma_request *)ureq, ch);
	return result;
    }
    sti();
    return pkt->len;
}

static int make_send_request(struct udma_request **ureqp,
			     struct dma_device *dev, struct dma_channel *ch,
			     struct ps2_packet *pkt, struct dma_tag **lastp)
{
    struct udma_request *ureq;
    int result, len;

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_request(&ureq->r, &dma_send_ops, dev, 0);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->mem = NULL;
    ureq->done = NULL;

    if (!ch->isspr) {
	len = pkt->len;
    } else {
	DPRINT("make_send_request : SPR\n");
	ureq->r.ops = &dma_send_spr_ops;
	ureq->saddr = ((struct ps2_packet_spr *)pkt)->offset;
	len = ((struct ps2_packet_spr *)pkt)->len;
    }
    switch (result = ps2dma_make_tag((unsigned long)pkt->ptr, len, &ureq->tag, lastp, &ureq->mem)) {
    case BUFTYPE_MEM:
	DPRINT("make_send_request : BUFTYPE_MEM\n");
	break;
    case BUFTYPE_SPR:
	DPRINT("make_send_request : BUFTYPE_SPR\n");
	if (!ch->isspr)
	    break;
	/* both src and dest are SPR */
	kfree(ureq->tag);
	kfree(ureq);
	return -EINVAL;
    case BUFTYPE_USER:
	DPRINT("make_send_request : BUFTYPE_USER\n");
	if ((result = ps2pl_copy_from_user(ureq->mem, pkt->ptr, len))) {
	    ps2pl_free(ureq->mem);
	    kfree(ureq->tag);
	    kfree(ureq);
	    return result;
	}
	ureq->r.qsize = len;
	break;
    default:
	kfree(ureq);
	return result;
    }

    *ureqp = ureq;
    return 0;
}

int ps2dma_send(struct dma_device *dev, struct ps2_packet *pkt, int async)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    volatile int done = 0;
    int result;

    DPRINT("dma_send %08X %08X %d\n", pkt->ptr, pkt->len, async);
    if ((result = make_send_request(&ureq, dev, ch, pkt, NULL)))
	return result;

    cli();
    result = ps2dma_check_and_add_queue((struct dma_request *)ureq, devch, 0);
    if (result < 0) {
	sti();
	dma_free((struct dma_request *)ureq, ch);
	return result;
    }
    if (!async) {
	DPRINT("dma_send: sleep\n");
	ureq->done = &done;
	while (!done) {
            interruptible_sleep_on(&devch->done_wq);
            if (signal_pending(current)) {
                result = -ERESTARTNOHAND; /* already queued - don't restart */
		break;
            }
        }
	if (!done)
	    ureq->done = NULL;
	DPRINT("dma_send: done\n");
    }
    sti();
    return result;
}

int ps2dma_send_list(struct dma_device *dev, int num, struct ps2_packet *pkts)
{
    struct udma_request_list *ureql;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    struct dma_tag *last, *prevlast;
    int result, i;

    if (num <= 0)
	return -EINVAL;
    if ((ureql = kmalloc(sizeof(struct udma_request_list) + sizeof(struct udma_request *) * num, GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_request(&ureql->r, &dma_send_list_ops, dev, 0);
    ureql->reqs = num;
    ureql->index = 0;
    prevlast = NULL;

    for (i = 0; i < num; i++) {
	if ((result = make_send_request(&ureql->ureq[i], dev, ch, &pkts[i], &last))) {
	    while (--i >= 0)
		dma_free((struct dma_request *)ureql->ureq[i], ch);
	    kfree(ureql);
	    return result;
	}
	ureql->r.qsize += ureql->ureq[i]->r.qsize;

	if (prevlast != NULL) {
	    prevlast->id = DMATAG_NEXT;
	    prevlast->qwc = 0;
	    prevlast->addr = virt_to_bus(ureql->ureq[i]->tag);
	}
	prevlast = last;
    }

    cli();
    result = ps2dma_check_and_add_queue((struct dma_request *)ureql, devch, 0);
    if (result < 0) {
	sti();
	dma_list_free((struct dma_request *)ureql, ch);
	return result;
    }
    sti();
    return 0;
}

static int make_recv_request(struct udma_request **ureqp,
			     struct dma_device *dev, struct dma_channel *ch,
			     struct ps2_packet *pkt, 
			     struct page_list **memp, int async)
{
    struct udma_request *ureq;
    int result, len;

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_request(&ureq->r, &dma_recv_ops, dev, 0);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->mem = NULL;
    ureq->done = NULL;

    if (!ch->isspr) {
	len = pkt->len;
    } else {
	ureq->r.ops = &dma_recv_spr_ops;
	ureq->saddr = ((struct ps2_packet_spr *)pkt)->offset;
	len = ((struct ps2_packet_spr *)pkt)->len;
	DPRINT("make_recv_request : SPR\n");
    }
    switch (result = ps2dma_make_tag((unsigned long)pkt->ptr, len, &ureq->tag, NULL, memp)) {
    case BUFTYPE_MEM:
	DPRINT("make_recv_request : BUFTYPE_MEM\n");
	break;
    case BUFTYPE_SPR:
	DPRINT("make_recv_request : BUFTYPE_SPR\n");
	if (!ch->isspr)
	    break;
	/* both src and dest are SPR */
	kfree(ureq->tag);
	kfree(ureq);
	return -EINVAL;
    case BUFTYPE_USER:
	DPRINT("make_recv_request : BUFTYPE_USER\n");
	if (async) {
	    /* no asynchronous copy_to_user function */
	    kfree(ureq->tag);
	    kfree(ureq);
	    ps2pl_free(*memp);
	    return -EINVAL;
	}
	ureq->r.qsize = len;
	break;
    default:
	kfree(ureq);
	return result;
    }

    *ureqp = ureq;
    return 0;
}

int ps2dma_recv(struct dma_device *dev, struct ps2_packet *pkt, int async)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_RECVCH];
    struct dma_channel *ch = devch->channel;
    struct page_list *recv_mem = NULL;
    volatile int done = 0;
    int result;

    DPRINT("dma_recv %08X %08X %d\n", pkt->ptr, pkt->len, async);
    if ((result = make_recv_request(&ureq, dev, ch, pkt, &recv_mem, async)))
	return result;

    cli();
    result = ps2dma_check_and_add_queue((struct dma_request *)ureq, devch, 0);
    if (result < 0) {
	sti();
	dma_free((struct dma_request *)ureq, ch);
	return result;
    }
    if (!async) {
	DPRINT("dma_recv: sleep\n");
	ureq->done = &done;
	while (!done) {
            interruptible_sleep_on(&devch->done_wq);
            if (signal_pending(current)) {
                result = -ERESTARTNOHAND; /* already queued - don't restart */
		break;
            }
        }
	if (!done)
	    ureq->done = NULL;
	DPRINT("dma_recv: done\n");
	if (recv_mem != NULL && result == 0) {
	    sti();
	    if (!ch->isspr)
		result = ps2pl_copy_to_user(pkt->ptr, recv_mem, pkt->len);
	    else
		result = ps2pl_copy_to_user(pkt->ptr, recv_mem, ((struct ps2_packet_spr *)pkt)->len);
	    ps2pl_free(recv_mem);
	    return result;
	}
    }
    sti();
    return result;
}

int ps2dma_recv_list(struct dma_device *dev, int num, struct ps2_packet *pkts)
{
    struct udma_request_list *ureql;
    struct dma_devch *devch = &dev->devch[DMA_RECVCH];
    struct dma_channel *ch = devch->channel;
    int result, i;

    if (num <= 0)
	return -EINVAL;
    if ((ureql = kmalloc(sizeof(struct udma_request_list) + sizeof(struct udma_request *) * num, GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_request(&ureql->r, &dma_recv_list_ops, dev, 0);
    ureql->reqs = num;
    ureql->index = 0;

    for (i = 0; i < num; i++) {
	if ((result = make_recv_request(&ureql->ureq[i], dev, ch, &pkts[i], NULL, 1))) {
	    while (--i >= 0)
		dma_free((struct dma_request *)ureql->ureq[i], ch);
	    kfree(ureql);
	    return result;
	}
    }

    cli();
    result = ps2dma_check_and_add_queue((struct dma_request *)ureql, devch, 0);
    if (result < 0) {
	sti();
	dma_list_free((struct dma_request *)ureql, ch);
	return result;
    }
    sti();
    return 0;
}

int ps2dma_stop(struct dma_device *dev, int dir, struct ps2_pstop *pstop)
{
    struct dma_channel *ch = dev->devch[dir].channel;
    struct dma_request *hreq, **reqp, *next;
    int stop = 0;

    if (ch == NULL)
	return -1;

    pstop->ptr = NULL;
    pstop->qct = 0;
    cli();

    /* delete all DMA requests from the queue */
    reqp = &ch->tail;
    hreq = NULL;
    while (*reqp != NULL) {
	if ((*reqp)->device == dev) {
	    if (!stop && reqp == &ch->tail) {
		/* the request is processing now - stop DMA */
		if ((pstop->ptr = (void *)(*reqp)->ops->stop(*reqp, ch)))
		    stop = 1;
	    } else {
		if (pstop->ptr == NULL)
		    pstop->ptr = (void *)((struct udma_request *)(*reqp))->vaddr;
	    }		
	    pstop->qct++;

	    next = (*reqp)->next;
	    (*reqp)->ops->free(*reqp, ch);
	    *reqp = next;
	} else {
	    hreq = *reqp;
	    reqp = &(*reqp)->next;
	}
    }
    ch->head = hreq;
    dev->devch[dir].qct = 0;
    dev->devch[dir].qsize = 0;

    if (stop)
	ps2dma_intr_handler(ch->irq, ch, NULL);
    sti();
    return 0;
}

int ps2dma_get_qct(struct dma_device *dev, int dir, int param)
{
    int qct;
    struct dma_devch *devch = &dev->devch[dir];

    if (param <= 0)
	return devch->qct;

    cli();
    while ((qct = devch->qct) >= param) {
	interruptible_sleep_on(&devch->done_wq);
	if (signal_pending(current)) {
	    sti();
	    return -ERESTARTSYS;		/* signal arrived */
	}
    }
    sti();
    return qct;
}

int ps2dma_set_qlimit(struct dma_device *dev, int dir, int param)
{
    int oldlimit;
    struct dma_devch *devch = &dev->devch[dir];

    if (param <= 0 || param > DMA_QUEUE_LIMIT_MAX)
	return -EINVAL;

    oldlimit = devch->qlimit;
    devch->qlimit = param;
    return oldlimit;
}

struct dma_device *ps2dma_dev_init(int send, int recv)
{
    struct dma_device *dev;

    if ((dev = kmalloc(sizeof(struct dma_device), GFP_KERNEL)) == NULL)
	return NULL;
    memset(dev, 0, sizeof(struct dma_device));
    dev->ts = current;
    if (send >= 0) {
	dev->devch[DMA_SENDCH].channel = &ps2dma_channels[send];
	dev->devch[DMA_SENDCH].qlimit = DMA_QUEUE_LIMIT_MAX;
    }
    if (recv >= 0) {
	dev->devch[DMA_RECVCH].channel = &ps2dma_channels[recv];
	dev->devch[DMA_RECVCH].qlimit = DMA_QUEUE_LIMIT_MAX;
    }

    return dev;
}

int ps2dma_finish(struct dma_device *dev)
{
    struct ps2_pstop pstop;
    long timeout;

    DPRINT("finish\n");

    cli();
    while (dev->devch[DMA_SENDCH].qct != 0 ||
	   dev->devch[DMA_RECVCH].qct != 0) {
	if (current->flags & PF_SIGNALED) {
	    /* closed by a signal */
	    DPRINT("closed by a signal\n");
	    flush_signals(current);
	}

	timeout = interruptible_sleep_on_timeout(&dev->empty_wq, DMA_TIMEOUT);

	if (signal_pending(current) ||
	    (current->flags & PF_SIGNALED && timeout == 0)) {
	    /* reset device FIFO */
	    if (dev->devch[DMA_SENDCH].channel->reset != NULL)
		dev->devch[DMA_SENDCH].channel->reset();
	    /* force break by a signal */
	    ps2dma_stop(dev, DMA_SENDCH, &pstop);
	    ps2dma_stop(dev, DMA_RECVCH, &pstop);
	    sti();
	    return -1;
	}
    }
    sti();
    return 0;
}

/*
 *  Kernel mode DMA functions
 */

#define DMA_POLLING_TIMEOUT	1500000

static int dma_polling_wait(struct dma_channel *ch, unsigned long flags)
{
    struct dma_request *req;
    int count;
    int result = 0;

    disable_irq(ch->irq);

    if (DMAREG(ch, PS2_Dn_CHCR) & 0x0100) {
	restore_flags(flags);
	for (count = DMA_POLLING_TIMEOUT; count > 0; count--)
	    if (!(DMAREG(ch, PS2_Dn_CHCR) & 0x0100))
		break;
	save_flags(flags); cli();
	if (count <= 0) {
	    /* timeout - DMA force break */
	    DMABREAK(ch);
	    /* reset device FIFO */
	    if (ch->reset != NULL)
		ch->reset();
	    result = -1;
	}
    }
    req = ch->tail;
    ps2dma_intr_handler(ch->irq, ch, NULL);

    enable_irq(ch->irq);
    return result;
}

static int dma_sleeping_wait(struct dma_channel *ch, struct wait_queue **wqp)
{
    if (sleep_on_timeout(wqp, DMA_TIMEOUT) == 0) {
	/* timeout - DMA force break */
	DMABREAK(ch);
	/* reset device FIFO */
	if (ch->reset != NULL)
	    ch->reset();
	/* next request */
	ps2dma_intr_handler(ch->irq, ch, NULL);
	return -1;
    }
    return 0;
}

int ps2dma_intr_safe_wait(struct dma_channel *ch, int poll, struct wait_queue **wqp, unsigned long flags)
{
    if (poll)
	return dma_polling_wait(ch, flags);
    else
	return dma_sleeping_wait(ch, wqp);
}

static void kdma_send_start(struct dma_request *req, struct dma_channel *ch)
{
    struct kdma_request *kreq = (struct kdma_request *)req;

    if (ch == &ps2dma_channels[DMA_GIF]) {
	int count = DMA_POLLING_TIMEOUT;
	/*
	 * If PATH3 is active and no data exists in GIF FIFO,
	 * previous GS packet may not be terminated.
	 */
	while ((GIFREG(PS2_GIFREG_STAT) & 0x1f000c00) == 0x00000c00) {
	    if (--count <= 0) {
		GIFREG(PS2_GIFREG_CTRL) = 1;	/* reset GIF */
		printk("ps2dma: GS packet is not terminated\n");
		break;
	    }
	}
    }

    DMAREG(ch, PS2_Dn_MADR) =
	virt_to_bus((void *)kreq + sizeof(struct kdma_request));
    DMAREG(ch, PS2_Dn_QWC) = kreq->qwc;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0101;
}

static void kdma_free(struct dma_request *req, struct dma_channel *ch)
{
    struct kdma_request *kreq = (struct kdma_request *)req;

    kreq->kdb->bottom = kreq->next;
    if (kreq->kdb->free_wq)
	wake_up(&kreq->kdb->free_wq);
}

static struct dma_ops kdma_send_ops =
{ kdma_send_start, NULL, NULL, kdma_free };

void ps2kdma_init(struct kdma_buffer *kdb, int ch, void *buf, int len)
{
    len = kdb->size = len & ~(DMA_TRUNIT - 1);
    kdb->channel = &ps2dma_channels[ch];
    kdb->start = buf;
    kdb->end = buf + len;
    kdb->top = kdb->bottom = NULL;
    kdb->allocmax = DMA_ALIGN(len - (len >> 2));
    kdb->allocated = 0;
    kdb->free_wq = kdb->alloc_wq = NULL;
    kdb->error = 0;
}

void *ps2kdma_alloc(struct kdma_buffer *kdb, int min, int max, int *size)
{
    unsigned long flags;
    int free, amin;
    int poll;

    save_flags(flags); cli();
#ifdef __mips__
    /* polling wait is used when
     *  - called from interrupt handler
     *  - interrupt is already disabled (in printk()) 
     */
    poll = in_interrupt() | !(flags & ST0_IE);
#else
#error "for MIPS CPU only"
#endif
    while (kdb->allocated) {		/* already allocated */
	if (poll) {
	    restore_flags(flags);
	    return NULL;		/* cannot sleep */
	}
	sleep_on(&kdb->alloc_wq);
    }

    amin = DMA_ALIGN(min) + sizeof(struct kdma_request);
    if (amin > kdb->size) {
	restore_flags(flags);
	return NULL;			/* requested size is too large */
    }

    kdb->allocated = 1;
    while (1) {
	if (kdb->top == kdb->bottom) {		/* whole buffer is free */
	    kdb->top = kdb->bottom = kdb->start;
	    free = kdb->size - DMA_TRUNIT;
	    break;
	}
	if (kdb->top > kdb->bottom) {		/* [...#####...] */
	    free = kdb->end - kdb->top;
	    if (amin <= free)
		break;
	    if (kdb->bottom > kdb->start) {
		kdb->top = kdb->start;		/* wrap around */
		continue;
	    }
	} else if (kdb->top < kdb->bottom) {	/* [###.....###] */
	    free = kdb->bottom - kdb->top - DMA_TRUNIT;
	    if (amin <= free)
		break;
	}

	kdb->error |= ps2dma_intr_safe_wait(kdb->channel, poll, &kdb->free_wq, flags);
    }
    if (amin < kdb->allocmax && free > kdb->allocmax)
	free = kdb->allocmax;
    free -= sizeof(struct kdma_request);
    if (size)
	*size = free > max ? max : free;
    kdb->kreq = (struct kdma_request *)kdb->top;
    restore_flags(flags);

    return (void *)kdb->kreq + sizeof(struct kdma_request);
}

void ps2kdma_send(struct kdma_buffer *kdb, int len)
{
    unsigned long flags;
    int alen;
    struct kdma_request *kreq = kdb->kreq;

    save_flags(flags); cli();
    alen = sizeof(struct kdma_request) + DMA_ALIGN(len);
    kdb->top = (void *)kreq + alen;

    init_dma_request(&kreq->r, &kdma_send_ops, NULL, 0);
    kreq->next = kdb->top;
    kreq->kdb = kdb;
    kreq->qwc = len >> 4;

    if (dma_intr_initialized) {
	ps2dma_add_queue((struct dma_request *)kreq, kdb->channel);
    } else {
	/* interrupt handler is not registered while system startup */
	kdma_send_start((struct dma_request *)kreq, kdb->channel);
	restore_flags(flags);
	while (DMAREG(kdb->channel, PS2_Dn_CHCR) & 0x0100)
	    ;
	save_flags(flags); cli();
	kdma_free((struct dma_request *)kreq, kdb->channel);
    }

    kdb->allocated = 0;
    if (kdb->alloc_wq)
	wake_up(&kdb->alloc_wq);
    restore_flags(flags);
    if (kdb->error) {
	kdb->error = 0;
	printk("ps2dma: %s timeout\n", kdb->channel->device);
    }
}

/*
 *  Simple DMA functions
 */

static void sdma_send_start(struct dma_request *req, struct dma_channel *ch)
{
    struct sdma_request *sreq = (struct sdma_request *)req;

    DMAREG(ch, PS2_Dn_MADR) = sreq->madr;
    DMAREG(ch, PS2_Dn_QWC) = sreq->qwc;
    DMAREG(ch, PS2_Dn_CHCR) = 0x0101;
}

static void sdma_free(struct dma_request *req, struct dma_channel *ch)
{
    struct sdma_request *sreq = (struct sdma_request *)req;

    sreq->done = 1;
    if (sreq->wq)
	wake_up(&sreq->wq);
}

static struct dma_ops sdma_send_ops =
{ sdma_send_start, NULL, NULL, sdma_free };

void ps2sdma_send(int chno, void *ptr, int len)
{
    unsigned long flags;
    struct sdma_request sreq;
    struct dma_channel *ch = &ps2dma_channels[chno];
    int result = 0;

    init_dma_request(&sreq.r, &sdma_send_ops, NULL, 0);
    sreq.madr = virt_to_bus(ptr);
    sreq.qwc = len >> 4;
    sreq.wq = NULL;
    sreq.done = 0;

    save_flags(flags); cli();
    if (dma_intr_initialized) {
	ps2dma_add_queue((struct dma_request *)&sreq, ch);
	while (!sreq.done)
	    result |= ps2dma_intr_safe_wait(ch, in_interrupt(), &sreq.wq, flags);
    } else {
	/* interrupt handler is not registered while system startup */
	sdma_send_start((struct dma_request *)&sreq, ch);
	restore_flags(flags);
	while (DMAREG(ch, PS2_Dn_CHCR) & 0x0100)
	    ;
	save_flags(flags); cli();
    }
    restore_flags(flags);
    if (result)
	printk("ps2dma: %s timeout\n", ch->device);
}

/*
 *  DMA initialize
 */

__initfunc(void ps2dma_init(void))
{
    int i;

    for (i = 0; i < sizeof(ps2dma_channels) / sizeof(ps2dma_channels[0]); i++)
	if (request_irq(ps2dma_channels[i].irq, ps2dma_intr_handler,
			SA_INTERRUPT, ps2dma_channels[i].device,
			&ps2dma_channels[i]))
	    printk("unable to get irq %d\n", i);

    dma_intr_initialized = 1;
}
