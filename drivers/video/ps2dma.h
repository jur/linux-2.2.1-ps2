#ifndef __PS2DMA_H
#define __PS2DMA_H

#include <linux/sched.h>
#include <linux/param.h>
#include <asm/types.h>
#include <asm/io.h>
#include <linux/ps2/dev.h>

#undef DEBUG
#ifdef DEBUG
#define DPRINT(fmt, args...) \
	printk(__FILE__ ": " fmt, ## args)
#define DSPRINT(fmt, args...) \
	prom_printf(__FILE__ ": " fmt, ## args)
#else
#define DPRINT(fmt, args...)
#define DSPRINT(fmt, args...)
#endif


#define DMA_VIF0	0
#define DMA_VIF1	1
#define DMA_GIF		2
#define DMA_IPU_from	3
#define DMA_IPU_to	4
#define DMA_SPR_from	5
#define DMA_SPR_to	6

#define DMA_SENDCH	0
#define DMA_RECVCH	1

#define BUFTYPE_MEM	0	/* DMA buffer is allocated by ps2mem */
#define BUFTYPE_SPR	1	/* DMA buffer is scratchpad RAM */
#define BUFTYPE_USER	2	/* copy from/to user address space */

#define DMA_QUEUE_LIMIT_MAX	16
#define DMA_USER_LIMIT	(1 * 1024 * 1024)

#define DMA_TRUNIT	16
#define DMA_ALIGN(x)	((__typeof__(x))(((unsigned long)(x) + (DMA_TRUNIT - 1)) & ~(DMA_TRUNIT - 1)))

#define DMA_TRUNIT_IMG		128
#define DMA_ALIGN_IMG(x)	((__typeof__(x))(((unsigned long)(x) + (DMA_TRUNIT_IMG - 1)) & ~(DMA_TRUNIT_IMG - 1)))

#define DMA_TIMEOUT	(HZ / 2)

/* DMA registers */

#define PS2_D_STAT	((volatile unsigned long *)KSEG1ADDR(0x1000e010))
#define PS2_D_ENABLEW	((volatile unsigned long *)KSEG1ADDR(0x1000f590))

#define PS2_Dn_CHCR	0x0000
#define PS2_Dn_MADR	0x0010
#define PS2_Dn_QWC	0x0020
#define PS2_Dn_TADR	0x0030
#define PS2_Dn_SADR	0x0080
#define DMAREG(ch, x)	(*(volatile unsigned long *)((ch)->base + (x)))
#define DMABREAK(ch)	\
    do { *PS2_D_ENABLEW = 1 << 16; DMAREG(ch, PS2_Dn_CHCR) = 0; \
	 *PS2_D_ENABLEW = 0 << 16; } while (0)

struct dma_tag {
    u16 qwc;
    u16 id;
    u32 addr;
} __attribute__((aligned(DMA_TRUNIT)));

#define DMATAG_SET(qwc, id, addr)	\
	((u64)(qwc) | ((u64)(id) << 16) | ((u64)(addr) << 32))
#define DMATAG_REFE	0x0000
#define DMATAG_CNT	0x1000
#define DMATAG_NEXT	0x2000
#define DMATAG_REF	0x3000
#define DMATAG_REFS	0x4000
#define DMATAG_CALL	0x5000
#define DMATAG_RET	0x6000
#define DMATAG_END	0x7000

/* memory page list */

struct page_list {
    int pages;
    unsigned long page[0];
};

/* DMA channel structures */

struct dma_request;
struct dma_device;

struct dma_channel {
    int irq;				/* DMA interrupt IRQ # */
    unsigned long base;			/* DMA register base addr */
    int direction;			/* data direction */
    int isspr;				/* true if DMA for scratchpad RAM */
    char *device;			/* request_irq() device name */
    void (*reset)(void);		/* FIFO reset function */

    struct dma_request *head, *tail;	/* DMA request queue */
    struct dma_tag *tagp;		/* tag pointer (for destination DMA) */
};

struct dma_ops {
    void (*start)(struct dma_request *, struct dma_channel *);
    int (*isdone)(struct dma_request *, struct dma_channel *);
    unsigned long (*stop)(struct dma_request *, struct dma_channel *);
    void (*free)(struct dma_request *, struct dma_channel *);
};

struct dma_request {
    struct dma_request *next;		/* next request */
    struct dma_ops *ops;		/* DMA operation functions */
    struct dma_device *device;		/* request device */
    int qsize;				/* request data size */
};

#define init_dma_request(_req, _ops, _device, _qsize)	\
    do { (_req)->next = NULL; (_req)->ops = (_ops);	\
         (_req)->device = (_device); (_req)->qsize = (_qsize); } while (0)

/* user mode DMA request */

struct udma_request {
    struct dma_request r;
    struct dma_tag *tag;		/* DMA tag */
    unsigned long vaddr;		/* start virtual addr */
    unsigned long saddr;		/* scratchpad RAM addr */
    struct page_list *mem;		/* allocated buffer */
    volatile int *done;			/* pointer to DMA done flag */
};

struct udma_request_list {
    struct dma_request r;
    int reqs, index;
    struct udma_request *ureq[0];
};

struct dma_devch {
    struct dma_channel *channel;
    volatile int qct;
    volatile int qsize;
    int qlimit;
    struct wait_queue *done_wq;
};

struct dma_device {
    struct dma_devch devch[2];
    struct wait_queue *empty_wq;
    u32 intr_flag;
    u32 intr_mask;
    struct task_struct *ts;
    int sig;
    void *data;
};

/* kernel mode DMA request */

struct kdma_buffer;

struct kdma_request {
    struct dma_request r;
    void *next;
    struct kdma_buffer *kdb;
    unsigned long qwc;
} __attribute__((aligned(DMA_TRUNIT)));

struct kdma_buffer {
    struct dma_channel *channel;
    void *start, *end;
    void *top, *volatile bottom;
    int size, allocmax;
    int allocated;
    struct kdma_request *kreq;
    struct wait_queue *free_wq;
    struct wait_queue *alloc_wq;
    int error;
};

/* simple DMA request */

struct sdma_request {
    struct dma_request r;
    unsigned long madr;
    unsigned long qwc;
    struct wait_queue *wq;
    int done;
};

/* function prototypes */

struct page_list *ps2pl_alloc(int pages);
struct page_list *ps2pl_realloc(struct page_list *list, int newpages);
void ps2pl_free(struct page_list *list);
int ps2pl_copy_from_user(struct page_list *list, void *from, long len);
int ps2pl_copy_to_user(void *to, struct page_list *list, long len);

int ps2dma_make_tag(unsigned long start, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list **memp);

void ps2dma_intr_handler(int irq, void *dev_id, struct pt_regs *regs);
void ps2dma_add_queue(struct dma_request *req, struct dma_channel *ch);

int ps2dma_check_and_add_queue(struct dma_request *req, struct dma_devch *devch, int nonblock);
int ps2dma_write(struct dma_device *dev, struct ps2_packet *pkt, int nonblock);
int ps2dma_send(struct dma_device *dev, struct ps2_packet *pkt, int async);
int ps2dma_send_list(struct dma_device *dev, int num, struct ps2_packet *pkts);
int ps2dma_recv(struct dma_device *dev, struct ps2_packet *pkt, int async);
int ps2dma_recv_list(struct dma_device *dev, int num, struct ps2_packet *pkts);
int ps2dma_stop(struct dma_device *dev, int dir, struct ps2_pstop *pstop);
int ps2dma_get_qct(struct dma_device *dev, int dir, int param);
int ps2dma_set_qlimit(struct dma_device *dev, int dir, int param);
struct dma_device *ps2dma_dev_init(int send, int recv);
int ps2dma_finish(struct dma_device *dev);

int ps2dma_intr_safe_wait(struct dma_channel *ch, int poll, struct wait_queue **wqp, unsigned long flags);
void ps2kdma_init(struct kdma_buffer *kdb, int ch, void *buf, int len);
void *ps2kdma_alloc(struct kdma_buffer *kdb, int min, int max, int *size);
void ps2kdma_send(struct kdma_buffer *kdb, int len);

void ps2sdma_send(int chno, void *ptr, int len);

void ps2dma_init(void);

#endif /* __PS2DMA_H */
