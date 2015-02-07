/*
 * ps2_irq.S - Interrupt exception dispatch code for PlayStation 2
 *
 *	Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <linux/config.h>
#include <asm/asm.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set	noreorder
	.set	noat
	.align	5
	NESTED(ps2_irq, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at
	mfc0	s0, CP0_CAUSE		# get pending interrupt
	li	t1, 0xb000f000		# irq stat reg
	lw	t0, 0(t1)		# get irq stat

	andi	a0, t0, 0x0200		# timer 0 intr?
	beq	a0, zero, 1f
	 andi	a0, t0, 0x0001		# delay slot, gs intr?

	li	t0, 0x0200
	sw	t0, 0(t1)		# clear timer 0 intr (INTC)
	jal	timer_interrupt
	 move	a0, sp			# delay slot
	j	ret_from_irq
	 nop				# delay slot

1:
	beq	a0, zero, 1f
	 andi	a0, t0, 0x0002		# delay slot, sbus intr?

	li	t0, 0x0001
	sw	t0, 0(t1)		# clear gs intr (INTC)
	jal	ps2_gs_irqdispatch
	 move	a0, sp			# delay slot
	j	ret_from_irq
	 nop				# delay slot

1:
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP2	# delay slot, check INT0

	li	t0, 0x0002
	sw	t0, 0(t1)		# clear sbus intr (INTC)
	jal	ps2_sbus_irqdispatch
	 move	a0, sp			# delay slot
	j	ret_from_irq
	 nop				# delay slot

1:
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP3	# delay slot, check INT1

	jal	ps2_intc_irqdispatch
	 move	a0, sp			# delay slot
	j	ret_from_irq
	 nop				# delay slot

1:
	beq	a0, zero, 1f
	 nop

	jal	ps2_dmac_irqdispatch
	 move	a0, sp			# delay slot
1:
	j	ret_from_irq
	 nop
	END(ps2_irq)
