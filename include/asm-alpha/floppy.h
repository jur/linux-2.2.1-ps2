/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995
 */
#ifndef __ASM_ALPHA_FLOPPY_H
#define __ASM_ALPHA_FLOPPY_H

#include <linux/config.h>

#define fd_inb(port)			inb_p(port)
#define fd_outb(port,value)		outb_p(port,value)

#define fd_enable_dma(channel)		enable_dma(channel)
#define fd_disable_dma(channel)		disable_dma(channel)
#define fd_request_dma(channel)		request_dma(channel, "floppy")
#define fd_free_dma(channel)		free_dma(channel)
#define fd_clear_dma_ff(channel)	clear_dma_ff(channel)
#define fd_set_dma_mode(channel, mode)	set_dma_mode(channel, mode)
#define fd_set_dma_addr(channel, addr)	set_dma_addr(channel,virt_to_bus(addr))
#define fd_set_dma_count(channel, count) set_dma_count(channel,count)

#define fd_enable_irq(irq)		enable_irq(irq)
#define fd_disable_irq(irq)		disable_irq(irq)
#define fd_request_irq(irq)		request_irq(irq, \
					            floppy_interrupt, \
					            SA_INTERRUPT \
					            | SA_SAMPLE_RANDOM, \
					            "floppy", NULL)
#define fd_free_irq(irq)		free_irq(irq,  NULL);

__inline__ void virtual_dma_init(void)
{
	/* Nothing to do on an Alpha */
}

static int FDC1 = 0x3f0;
static int FDC2 = -1;

/*
 * Again, the CMOS information doesn't work on the alpha..
 */
#define FLOPPY0_TYPE 6
#define FLOPPY1_TYPE 0

#define N_FDC 2
#define N_DRIVE 8

#define FLOPPY_MOTOR_MASK 0xf0

/*
 * Most Alphas have no problems with floppy DMA crossing 64k borders,
 * except for XL and RUFFIAN.  They are also the only one with DMA
 * limits, so we use that to test in the generic kernel.
 */

#define __CROSS_64KB(a,s)					\
({ unsigned long __s64 = (unsigned long)(a);			\
   unsigned long __e64 = __s64 + (unsigned long)(s) - 1;	\
   (__s64 ^ __e64) & ~0xfffful; })

#ifdef CONFIG_ALPHA_GENERIC
# define CROSS_64KB(a,s)   (__CROSS_64KB(a,s) && ~alpha_mv.max_dma_address)
#else
# if defined(CONFIG_ALPHA_XL) || defined(CONFIG_ALPHA_RUFFIAN)
#  define CROSS_64KB(a,s)  __CROSS_64KB(a,s)
# else
#  define CROSS_64KB(a,s)  (0)
# endif
#endif

#endif /* __ASM_ALPHA_FLOPPY_H */
