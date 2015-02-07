/*
 * linux/include/asm-mips/ps2/irq.h
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: irq.h,v 1.3 2000/09/25 07:19:30 takemura Exp $
 */

#ifndef __ASM_PS2_IRQ_H
#define __ASM_PS2_IRQ_H

/*
 * PlayStation 2 interrupts
 */

/* INTC */
#define IRQ_INTC	0
#define IRQ_INTC_GS	0
#define IRQ_INTC_SBUS	1
#define IRQ_INTC_VB_ON	2
#define IRQ_INTC_VB_OFF	3
#define IRQ_INTC_VIF0	4
#define IRQ_INTC_VIF1	5
#define IRQ_INTC_VU0	6
#define IRQ_INTC_VU1	7
#define IRQ_INTC_IPU	8
#define IRQ_INTC_TIMER0	9
#define IRQ_INTC_TIMER1	10
#define IRQ_INTC_TIMER2	11
#define IRQ_INTC_TIMER3	12
#define IRQ_INTC_SFIFO	13
#define IRQ_INTC_VU0WD	14
#define IRQ_INTC_PGPU	15

/* DMAC */
#define IRQ_DMAC	16
#define IRQ_DMAC_0	16
#define IRQ_DMAC_1	17
#define IRQ_DMAC_2	18
#define IRQ_DMAC_3	19
#define IRQ_DMAC_4	20
#define IRQ_DMAC_5	21
#define IRQ_DMAC_6	22
#define IRQ_DMAC_7	23
#define IRQ_DMAC_8	24
#define IRQ_DMAC_9	25
#define IRQ_DMAC_S	29
#define IRQ_DMAC_ME	30
#define IRQ_DMAC_BE	31

/* GS */
#define IRQ_GS		32
#define IRQ_GS_SIGNAL	32
#define IRQ_GS_FINISH	33
#define IRQ_GS_HSYNC	34
#define IRQ_GS_VSYNC	35
#define IRQ_GS_EDW	36
#define IRQ_GS_EXHSYNC	37
#define IRQ_GS_EXVSYNC	38

/* SBUS */
#define IRQ_SBUS	40
#define IRQ_SBUS_AIF	40
#define IRQ_SBUS_PCIC	41
#define IRQ_SBUS_USB	42

#endif /* __ASM_PS2_IRQ_H */
