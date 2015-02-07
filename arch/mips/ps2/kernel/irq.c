/* $Id: irq.c,v 1.39 2001/03/09 10:21:40 nakamura Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/bitops.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/ps2/irq.h>

atomic_t __mips_bh_counter;

static struct irqaction ps2timer0_action = {
	NULL, 0, 0, "Ch-0 timer/counter", NULL, NULL,
};

static struct irqaction *irq_action[NR_IRQS] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, &ps2timer0_action, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static int irq_enabled[NR_IRQS];

/*
 * INTC specific service functions
 */
#define INTC_STAT	((volatile u32 *)0xb000f000)
#define INTC_MASK	((volatile u32 *)0xb000f010)

static inline void mask_irq_intc(unsigned int irq_nr)
{
	if (*INTC_MASK & (1 << irq_nr))
		*INTC_MASK = 1 << irq_nr;
}

static inline void unmask_irq_intc(unsigned int irq_nr)
{
	if (!(*INTC_MASK & (1 << irq_nr)))
		*INTC_MASK = 1 << irq_nr;
}

static inline u32 get_istat_intc(void)
{
	u32 istat;

	istat = *INTC_STAT;
	istat &= *INTC_MASK & ~((1 <<IRQ_INTC_GS) | (1 << IRQ_INTC_SBUS) | (1 << IRQ_INTC_TIMER0));
	*INTC_STAT = istat;		/* clear IRQ */
	return istat;
}

/*
 * DMAC specific service functions
 */
#define DMAC_STAT	((volatile u32 *)0xb000e010)
#define DMAC_MASK	((volatile u32 *)0xb000e010)

static inline void mask_irq_dmac(unsigned int irq_nr)
{
	if (*DMAC_MASK & (1 << irq_nr))
		*DMAC_MASK = 1 << irq_nr;
}

static inline void unmask_irq_dmac(unsigned int irq_nr)
{
	if (!(*DMAC_MASK & (1 << irq_nr)))
		*DMAC_MASK = 1 << irq_nr;
}

static inline u32 get_istat_dmac(void)
{
	u32 istat;

	istat = *DMAC_STAT;
	istat &= *DMAC_MASK >> 16;
	*DMAC_STAT = istat;		/* clear IRQ */
	return istat;
}

/*
 * GS specific service functions
 */
#define GS_CSR		((volatile u32 *)0xb2001000)
#define GS_IMR		((volatile u32 *)0xb2001010)

static u32 gs_intr_mask = 0xff;

static inline void mask_irq_gs(unsigned int irq_nr)
{
	gs_intr_mask |= (1 << (irq_nr - IRQ_GS));
	*GS_IMR = gs_intr_mask << 8;
}

static inline void unmask_irq_gs(unsigned int irq_nr)
{
	gs_intr_mask &= ~(1 << (irq_nr - IRQ_GS));
	*GS_IMR = gs_intr_mask << 8;
}

static inline u32 get_istat_gs(void)
{
	u32 istat;

	istat = *GS_CSR;
	istat &= ~gs_intr_mask & 0x7f;
	*GS_CSR = istat;		/* clear IRQ */
	return istat;
}

/*
 * SBUS specific service functions
 */
#define SBUS_SMFLG	((volatile u32 *)0xb000f230)
#define SBUS_AIF_INTSR	((volatile u16 *)0xb8000004)
#define SBUS_AIF_INTEN	((volatile u16 *)0xb8000006)
#define SBUS_PCIC_EXC1	((volatile u16 *)0xbf801476)
#define SBUS_PCIC_CSC1	((volatile u16 *)0xbf801464)
#define SBUS_PCIC_IMR1	((volatile u16 *)0xbf801468)
#define SBUS_PCIC_TIMR	((volatile u16 *)0xbf80147e)
#define SBUS_PCIC3_TIMR	((volatile u16 *)0xbf801466)
#define SPD_R_INTR_ENA	((volatile u16 *)0xb400002a)

extern int ps2_pccard_present;
extern int ps2_pcic_type;
#ifdef CONFIG_T10000
extern int ps2_aif_probe;
#endif

static inline void mask_irq_sbus(unsigned int irq_nr)
{
	switch (irq_nr) {
#ifdef CONFIG_T10000
	case IRQ_SBUS_AIF:
		*SBUS_AIF_INTEN &= ~1;
		break;
	case IRQ_SBUS_PCIC:
		switch (ps2_pcic_type) {
		case 1:
			*SBUS_PCIC_IMR1 = 0xffff;
			break;
		case 2:
			*SBUS_PCIC_TIMR = 1;
			break;
		default:
			*SBUS_PCIC3_TIMR = 1;
			break;
		}
		break;
#else
	case IRQ_SBUS_PCIC:
		if (ps2_pcic_type < 3)
			*SBUS_PCIC_TIMR = 1;
		else
			*SBUS_PCIC3_TIMR = 1;
		break;
#endif
	case IRQ_SBUS_USB:
		break;
	}
}

static inline void unmask_irq_sbus(unsigned int irq_nr)
{
	switch (irq_nr) {
#ifdef CONFIG_T10000
	case IRQ_SBUS_AIF:
		*SBUS_AIF_INTEN |= 1;
		break;
	case IRQ_SBUS_PCIC:
		switch (ps2_pcic_type) {
		case 1:
			*SBUS_PCIC_IMR1 = 0xff7f;
			break;
		case 2:
			*SBUS_PCIC_TIMR = 0;
			break;
		default:
			*SBUS_PCIC3_TIMR = 0;
			break;
		}
		break;
#else
	case IRQ_SBUS_PCIC:
		if (ps2_pcic_type < 3)
			*SBUS_PCIC_TIMR = 0;
		else
			*SBUS_PCIC3_TIMR = 0;
		break;
#endif
	case IRQ_SBUS_USB:
		break;
	}
}

static inline u32 get_istat_sbus(void)
{
	u32 istat = 0;

#ifdef CONFIG_T10000
	if (irq_enabled[IRQ_SBUS_AIF] && (*SBUS_AIF_INTSR & 1)) {
		*SBUS_AIF_INTSR = 1;
		if (ps2_pcic_type == 1)
			*SBUS_PCIC_EXC1 = 1;
		istat |= 1 << (IRQ_SBUS_AIF - IRQ_SBUS);
	}
	if (irq_enabled[IRQ_SBUS_PCIC] && (*SBUS_SMFLG & (1 << 8))) {
		*SBUS_SMFLG = 1 << 8;
		if (!((ps2_pcic_type < 3) && !(*SBUS_PCIC_CSC1 & 0x080))) {
			switch (ps2_pcic_type) {
			case 2:
				if (ps2_aif_probe)
					*SBUS_AIF_INTSR = 4;
				/* fall through */
			case 1:
				*SBUS_PCIC_CSC1 = 0xffff;
				/* fall through */
			default:
				istat |= 1 << (IRQ_SBUS_PCIC - IRQ_SBUS);
				break;
			}
		}
	}
#else
	if (irq_enabled[IRQ_SBUS_PCIC] && (*SBUS_SMFLG & (1 << 8))) {
		*SBUS_SMFLG = 1 << 8;
		if (!((ps2_pcic_type < 3) && !(*SBUS_PCIC_CSC1 & 0x080))) {
			if (ps2_pcic_type < 3)
				*SBUS_PCIC_CSC1 = 0xffff;
			istat |= 1 << (IRQ_SBUS_PCIC - IRQ_SBUS);
		}
	}
#endif
	if (irq_enabled[IRQ_SBUS_USB] && (*SBUS_SMFLG & (1 << 10))) {
		*SBUS_SMFLG = 1 << 10;
		istat |= 1 << (IRQ_SBUS_USB - IRQ_SBUS);
	}
	return istat;
}

static inline void make_edge_sbus(void)
{
	u16 mask;

#ifdef CONFIG_T10000
	if (ps2_aif_probe) {
		mask = *SBUS_AIF_INTEN;
		*SBUS_AIF_INTEN = 0;
		*SBUS_AIF_INTEN = mask;
	}
#endif
	if (ps2_pccard_present == 0x0100) {
		mask = *SPD_R_INTR_ENA;
		*SPD_R_INTR_ENA = 0;
		*SPD_R_INTR_ENA = mask;
	}

	if (ps2_pcic_type < 3) {
		mask = *SBUS_PCIC_TIMR;
		*SBUS_PCIC_TIMR = 1;
		*SBUS_PCIC_TIMR = mask;
	} else {
		mask = *SBUS_PCIC3_TIMR;
		*SBUS_PCIC3_TIMR = 1;
		*SBUS_PCIC3_TIMR = mask;
	}
}

/*
 * generic functions
 */
void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	irq_enabled[irq_nr] = 0;
	if (irq_nr < IRQ_DMAC)
		mask_irq_intc(irq_nr);
	else if (irq_nr < IRQ_GS)
		mask_irq_dmac(irq_nr);
	else if (irq_nr < IRQ_SBUS)
		mask_irq_gs(irq_nr);
	else
		mask_irq_sbus(irq_nr);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	irq_enabled[irq_nr] = 1;
	if (irq_nr < IRQ_DMAC)
		unmask_irq_intc(irq_nr);
	else if (irq_nr < IRQ_GS)
		unmask_irq_dmac(irq_nr);
	else if (irq_nr < IRQ_SBUS)
		unmask_irq_gs(irq_nr);
	else
		unmask_irq_sbus(irq_nr);
	restore_flags(flags);
}

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			i, kstat.irqs[0][i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

int ps2_setup_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((old->flags ^ new->flags) & SA_INTERRUPT)
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	save_and_cli(flags);
	*p = new;

	if (!shared) {
		enable_irq(irq);
	}
	restore_flags(flags);
	return 0;
}

/*
 * Request_interrupt and free_interrupt ``sort of'' handle interrupts of
 * non i8259 devices.  They will have to be replaced by architecture
 * specific variants.  For now we still use this as broken as it is because
 * it used to work ...
 */
int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = ps2_setup_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq[irq_action])
			disable_irq(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

__initfunc(void init_IRQ(void))
{
	int i;

	/* mask all interrupts */
	for (i = 0; i < NR_IRQS; i++)
		disable_irq(i);

	irq_setup();
}

/*
 * IRQ dispatch routines for each interrupt sources
 */
static inline void irqdispatch_one(int irq, struct pt_regs *regs, void (*mask_irq)(unsigned int), void (*unmask_irq)(unsigned int))
{
	struct irqaction *action;
	int do_random;

	kstat.irqs[0][irq]++;
	action = irq_action[irq];
	if (action->flags & SA_INTERRUPT) {
		/* fast interrupt handler */
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	} else {
		/* normal interrupt handler */
		(*mask_irq)(irq);
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		do_random = 0;
		do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
		if (irq_enabled[irq])
			(*unmask_irq)(irq);
	}
}

void handleSimulatedIRQ(int irq)
{
	struct irqaction *action;
	int do_random;

	kstat.irqs[0][irq]++;
	action = irq_action[irq];
	if (action->flags & SA_INTERRUPT) {
		/* fast interrupt handler */
		do {
			action->handler(irq, action->dev_id, NULL);
			action = action->next;
		} while (action);
	} else {
		/* normal interrupt handler */
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		do_random = 0;
		do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, NULL);
			action = action->next;
		} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
	}
}

asmlinkage void ps2_intc_irqdispatch(struct pt_regs *regs)
{
	int irq, cpu = smp_processor_id();
	u32 istat;

	hardirq_enter(cpu);
	istat = get_istat_intc();
	for (irq = IRQ_INTC; istat != 0 && irq < IRQ_DMAC; irq++, istat >>= 1) {
		if ((istat & 1) == 0)
			continue;
		irqdispatch_one(irq, regs, mask_irq_intc, unmask_irq_intc);
	}
	hardirq_exit(cpu);
}

asmlinkage void ps2_dmac_irqdispatch(struct pt_regs *regs)
{
	int irq, cpu = smp_processor_id();
	u32 istat;

	hardirq_enter(cpu);
	istat = get_istat_dmac();
	for (irq = IRQ_DMAC; istat != 0 && irq < IRQ_GS; irq++, istat >>= 1) {
		if ((istat & 1) == 0)
			continue;
		irqdispatch_one(irq, regs, mask_irq_dmac, unmask_irq_dmac);
	}
	hardirq_exit(cpu);
}

asmlinkage void ps2_gs_irqdispatch(struct pt_regs *regs)
{
	int irq, cpu = smp_processor_id();
	u32 istat;

	hardirq_enter(cpu);
	istat = get_istat_gs();
	*GS_IMR = 0xff00;
	for (irq = IRQ_GS; istat != 0 && irq < IRQ_SBUS; irq++, istat >>= 1) {
		if ((istat & 1) == 0)
			continue;
		irqdispatch_one(irq, regs, mask_irq_gs, unmask_irq_gs);
	}
	*GS_IMR = gs_intr_mask << 8;
	hardirq_exit(cpu);
}

asmlinkage void ps2_sbus_irqdispatch(struct pt_regs *regs)
{
	int irq, cpu = smp_processor_id();
	u32 istat;
	struct irqaction *action;

	hardirq_enter(cpu);
    	istat = get_istat_sbus();
	for (irq = IRQ_SBUS; istat != 0 && irq < NR_IRQS; irq++, istat >>= 1) {
		if ((istat & 1) == 0)
			continue;
//irqdispatch_one(irq, regs, mask_irq_sbus, unmask_irq_sbus);
		kstat.irqs[0][irq]++;
		action = irq_action[irq];
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	}
	make_edge_sbus();
	hardirq_exit(cpu);
}
