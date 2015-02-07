/* $Id: head.S,v 1.11 1998/10/18 13:27:12 tsbogend Exp $
 *
 * arch/mips/kernel/head.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * Head.S contains the MIPS exception handler and startup code.
 */
#include <linux/config.h>
#include <linux/tasks.h>

#include <asm/asm.h>
#include <asm/cacheops.h>
#include <asm/current.h>
#include <asm/offset.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/stackframe.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/page.h>
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
#include <linux/tst_dev.h>
#endif

	.text
	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 * FIXME: Use the initcode feature to get rid of unused handler
	 * variants.
	 */
	.fill	0x280
/*	
 * This is space for the interrupt handlers.
 * After trap_init() they are located at virtual address KSEG0.
 *
 * These handlers much be written in a relocatable manner
 * because based upon the cpu type an arbitrary one of the
 * following pieces of code will be copied to the KSEG0
 * vector location.
 */
	/* TLB refill, EXL == 0, R4xx0, non-R4600 version */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_r4000)
	.set	mips3
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22			# get pgd only bits
	lw	k1, THREAD_PGDIR(k1)		# get task pg_dir
	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	mfc0	k0, CP0_CONTEXT			# get context reg
	lw	k1, (k1)
	srl	k0, k0, 1			# get pte offset
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
#ifdef CONFIG_CPU_R5900
	bltz	k0, 2f				# PTE for SPR
#endif
	 lw	k1, 4(k1)			# get odd pte  (delay slot)
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	b	1f
	 tlbwr					# write random tlb entry
1:	
	nop
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#endif
	eret					# return from trap
#ifdef CONFIG_CPU_R5900
2:		# trampoline code for jump to real TLB refill routine
	la	k0, except_vec0_ps2_spr
	jr	k0	
	 nop					# delay slot
#endif
	END(except_vec0_r4000)

/* TLB refill for scratchpad RAM */
#ifdef CONFIG_CPU_R5900
	LEAF(except_vec0_ps2_spr)
	mfc0	k0, CP0_ENTRYHI			# VPN must be 16KB aligned
	or	k0, k0, (1 << 13)
	xor	k0, k0, (1 << 13)
	mtc0	k0, CP0_ENTRYHI
	li	k0, 0x80000006			# entrylo0: S,D,V
	mtc0	k0, CP0_ENTRYLO0		# load it
	li	k1, 0x00000006			# entrylo1:   D,V
	mtc0	k1, CP0_ENTRYLO1		# load it
	b	1f
	 tlbwr					# write random tlb entry
1:	
	nop
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#endif
	eret					# return from trap
	END(except_vec0_ps2_spr)
#endif	/* CONFIG_CPU_R5900 */


#if !defined(CONFIG_CPU_R5900)
	/* TLB refill, EXL == 0, R4600 version */
	LEAF(except_vec0_r4600)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	k1, CP0_ENTRYLO1
	nop
	tlbwr
	nop
	eret
	END(except_vec0_r4600)

	/* TLB refill, EXL == 0, R4xx0, non-R4600 version */
	.set	noreorder
	.set	noat
	LEAF(except_vec0_nevada)
	.set	mips3
	mfc0	k0, CP0_BADVADDR		# Get faulting address
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22			# get pgd only bits
	lw	k1, THREAD_PGDIR(k1)		# get task pg_dir
	sll	k0, k0, 2
	addu	k1, k1, k0			# add in pgd offset
	lw	k1, (k1)
	mfc0	k0, CP0_CONTEXT			# get context reg
	srl	k0, k0, 1			# get pte offset
	and	k0, k0, 0xff8
	addu	k1, k1, k0			# add in offset
	lw	k0, 0(k1)			# get even pte
	lw	k1, 4(k1)			# get odd pte
	srl	k0, k0, 6			# convert to entrylo0
	mtc0	k0, CP0_ENTRYLO0		# load it
	srl	k1, k1, 6			# convert to entrylo1
	mtc0	k1, CP0_ENTRYLO1		# load it
	tlbwr					# write random tlb entry
	nop
	nop
	eret					# return from trap
	END(except_vec0_nevada)

	/* TLB refill, EXL == 0, R4[40]00/R5000 badvaddr hwbug version */
	LEAF(except_vec0_r45k_bvahwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r45k_bvahwbug)

#ifdef __SMP__
	/* TLB refill, EXL == 0, R4000 MP badvaddr hwbug version */
	LEAF(except_vec0_r4k_mphwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mfc0	k0, CP0_INDEX
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_mphwbug)
#endif /* __SMP__ */

	/* TLB refill, EXL == 0, R4000 UP 250MHZ entrylo[01] hwbug version */
	LEAF(except_vec0_r4k_250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	b	1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_250MHZhwbug)

#ifdef __SMP__
	/* TLB refill, EXL == 0, R4000 MP 250MHZ entrylo[01]+badvaddr bug version */
	LEAF(except_vec0_r4k_MP250MHZhwbug)
	.set	mips3
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xff8
	addu	k1, k1, k0
	lw	k0, 0(k1)
	lw	k1, 4(k1)
	nop				/* XXX */
	tlbp
	srl	k0, k0, 6
	mtc0	zero, CP0_ENTRYLO0
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k0, CP0_INDEX
	srl	k1, k1, 6
	mtc0	zero, CP0_ENTRYLO1
	mtc0	k1, CP0_ENTRYLO1
	bltzl	k0, 1f
	tlbwr
1:
	nop
	eret
	END(except_vec0_r4k_MP250MHZhwbug)
#endif /* __SMP__ */

	/* TLB refill, EXL == 0, R[23]00 version */
	LEAF(except_vec0_r2300)
	.set	mips1
	mfc0	k0, CP0_BADVADDR
	_GET_CURRENT(k1)			# get current task ptr
	srl	k0, k0, 22
	lw	k1, THREAD_PGDIR(k1)
	sll	k0, k0, 2
	addu	k1, k1, k0
	mfc0	k0, CP0_CONTEXT
	lw	k1, (k1)
	srl	k0, k0, 1
	and	k0, k0, 0xffc
	addu	k1, k1, k0
	lw	k0, (k1)
	srl	k0, k0, 12
	mtc0	k0, CP0_ENTRYLO0
	mfc0	k1, CP0_EPC
	tlbwr
	nop
	nop
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	move	k0,k1
	li	k1, _TST_ACCESS_MAGIC
	jr	k0
	rfe
#else
	nop
	nop
	jr	k1
	rfe
	END(except_vec0_r2300)
#endif
#endif /* !CONFIG_CPU_R5900 */

	/* XTLB refill, EXL == 0, R4xx0 cpus only use this... */
	NESTED(except_vec1_generic, 0, sp)
	.set	noat
	.set	mips3
	/* Register saving is delayed as long as we don't know
	 * which registers really need to be saved.
	 */
	mfc0	k1, CP0_CONTEXT
	dsra	k1, 1
	lwu	k0,  (k1)		# May cause another exception
	lwu	k1, 4(k1)
	dsrl	k0, 6			# Convert to EntryLo format
	dsrl	k1, 6			# Convert to EntryLo format
	dmtc0	k0, CP0_ENTRYLO0
	dmtc0	k1, CP0_ENTRYLO1
	nop				# Needed for R4[04]00 pipeline
	tlbwr
	nop				# Needed for R4[04]00 pipeline
	nop
	nop
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#endif
	eret
	nop				/* Workaround for R4000 bug. */
	eret
	END(except_vec1_generic)


#if !defined(CONFIG_CPU_R5900)
	/* Cache Error */
	LEAF(except_vec2_generic)
	/* Famous last words: unreached */
	mfc0	a1,CP0_ERROREPC
	PRINT("Cache error exception: c0_errorepc == %08x\n")
1:
	j	1b
	 nop
	END(except_vec2_generic)

	/* General exception vector R4000 version. */
	NESTED(except_vec3_r4000, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
	li	k0, 31<<2
	beq	k1, k0, handle_vced
	 li	k0, 14<<2
	beq	k1, k0, handle_vcei
	 la	k0, exception_handlers
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop

/*
 * Big shit, we now may have two dirty primary cache lines for the same
 * physical address.  We can savely invalidate the line pointed to by
 * c0_badvaddr because after return from this exception handler the load /
 * store will be re-executed.
 */
handle_vced:
	mfc0	k0, CP0_BADVADDR
 li k1, -4
 and k0, k1
	mtc0	zero, CP0_TAGLO
 //	nop;nop
	cache	Index_Store_Tag_D,(k0)
 //	nop;nop
	cache	Hit_Writeback_Inv_SD,(k0)
	lui	k0, %hi(vced_count)
	lw	k1, %lo(vced_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vced_count)(k0)
	eret

handle_vcei:
	mfc0	k0, CP0_BADVADDR
	cache	Hit_Writeback_Inv_SD,(k0)		# also cleans pi
	lui	k0, %hi(vcei_count)
	lw	k1, %lo(vcei_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vcei_count)(k0)
	eret

	END(except_vec3_r4000)
#endif /* ! CONFIG_CPU_R5900 */
	.set	at

	/* General exception vector. */
	NESTED(except_vec3_generic, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	la	k0, exception_handlers
	andi	k1, k1, 0x7c
	addu	k0, k0, k1
	lw	k0, (k0)
	nop
	jr	k0
	 nop
	END(except_vec3_generic)
	.set	at

	/*
	 * Special interrupt vector for embedded MIPS.  This is a
	 * dedicated interrupt vector which reduces interrupt processing
	 * overhead.  The jump instruction will be inserted here at
	 * initialization time.  This handler may only be 8 bytes in size!
	 */
	NESTED(except_vec4, 0, sp)
1:	j	1b			/* Dummy, will be replaced */
	 nop
	END(except_vec4)

/*
 * Kernel entry point
 */
NESTED(kernel_entry, 16, sp)
	.set	noreorder
	/* The following two symbols are used for kernel profiling. */
	EXPORT(stext)
	EXPORT(_stext)

	/* Determine which MIPS variant we are running on. */
	b	cpu_probe
	 nop

probe_done:

#ifdef CONFIG_CPU_R5900
	/*
	 *************************************************************
	 * NOTE: EE#2.9 is the first revision released to the public.*
	 * So, blow codes are needless for released PS2 or T10000.   *
	 *************************************************************
	 *
	 * Disable branch prediction for EE#2.5 or predcessor,
	 * due to miss-prediction of branch targets for short-loop. 
	 * Now compiler produces propper codes for EE#2.9 or successor,
	 * but EE#2.5 would miss-predict even if with that compiler.
	 */
	
	mfc0	t0, CP0_PRID
	andi	t0, 0xff
	bgt	t0, 0x13, 1f		/* 0x13: EE#2.5 */
	mfc0	t0, CP0_CONFIG
	lui	t1, %hi(~ CONF_R5900_BPE)
	ori	t1, t1, %lo(~ CONF_R5900_BPE)
	and	t0,t0,t1
	mtc0	t0, CP0_CONFIG
	sync.p
     1:
#endif
#ifdef CONFIG_CPU_R5900
	li	sp, 0x82000000 - 1024*1024*2	/* 30M */
#endif
	/* The firmware/bootloader passes argc/argp/envp
	 * to us as arguments.  But clear bss first because
	 * the romvec and other important info is stored there
	 * by prom_init().
	 */
	la	t0, _edata
	sw	zero, (t0)
	la	t1, (_end - 4)
1:
#ifdef CONFIG_CPU_R5900 /* inhibit short loop */
	addiu	t0, 4
	beq	t0, t1, 2f
	 sw	zero, (t0)
	addiu	t0, 4
	bne	t0, t1, 1b
	 sw	zero, (t0)
2:
#else
	addiu	t0, 4
	bne	t0, t1, 1b
	 sw	zero, (t0)
#endif

	jal	prom_init /* prom_init(argc, argv, envp); */
	 nop

#ifdef CONFIG_SGI
	jal	sgi_sysinit
	 nop
#endif

#ifdef CONFIG_COBALT_MICRO_SERVER
	jal	SetUpBootInfo
	 nop
#endif

	/*
	 * Determine the mmu/cache attached to this machine,
	 * then flush the tlb and caches.  On the r4xx0
	 * variants this also sets CP0_WIRED to zero.
	 */
	jal	loadmmu
	 nop

	/*
	 * Stack for kernel and init, current variable
	 */
	la	$28, init_task_union
	addiu	t0, $28, KERNEL_STACK_SIZE-32
	sw	t0, kernelsp
	subu	sp, t0, 4*SZREG

	/* Disable coprocessors */
	mfc0	t0, CP0_STATUS
	li	t1, ~(ST0_CU1|ST0_CU2|ST0_CU3|ST0_KX|ST0_SX)
	and	t0, t1
#ifdef CONFIG_CPU_R5900
	and	t0, ~(1<<17)			# clear EDI bit
#endif
	or	t0, ST0_CU0
	mtc0	t0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
	sync.p
#endif

1:	jal	start_kernel
	 nop
	/*
	 * Main should never return here, but
	 * just in case, we know what happens.
	 */
	b	1b
	 nop					# delay slot
	END(kernel_entry)

	/* CPU type probing code, called at Kernel entry. */
	LEAF(cpu_probe)
	mfc0	t0, CP0_PRID
	la	t3, mips_cputype
	andi	t1, t0, 0xff00
	li	t2, PRID_IMP_R2000
	bne	t1, t2, 1f
	 andi	t0, 0x00ff

	li	t2, CPU_R2000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R3000
	bne	t1, t2, 1f
	 nop

	li	t2, PRID_REV_R3000A
	bne	t0, t2, 9f
	 nop

	li	t2, CPU_R3000A
	b	probe_done
	sw	t2, (t3)
9:
	li	t2, CPU_R3000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R6000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R6000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4000
	bne	t1, t2, 1f
	 nop

	li	t2, PRID_REV_R4400
	bne	t0, t2, 9f
	 nop

	li	t2, CPU_R4400SC
	b	probe_done
	 sw	t2, (t3)
9:
	li	t2, CPU_R4000SC
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R6000A
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R6000A
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R10000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R10000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R8000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R8000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4600
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4600
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4700
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4700
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R4650
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R4650
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R5000
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R5000
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_NEVADA
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_NEVADA
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, PRID_IMP_R5900
	bne	t1, t2, 1f
	 nop

	li	t2, CPU_R5900
	b	probe_done
	 sw	t2, (t3)
1:
	li	t2, CPU_UNKNOWN
	sw	t2, (t3)

	b	probe_done
	 nop
	END(cpu_probe)

/*
 * This buffer is reserved for the use of the cache error handler.
 */
		.data
		EXPORT(cache_error_buffer)
		.fill	32*4,1,0

EXPORT(kernelsp)
		PTR	0
		.text

		.org	0x1000
EXPORT(swapper_pg_dir)

		.org	0x2000
EXPORT(empty_bad_page)

		.org	0x3000
EXPORT(empty_bad_page_table)

		.org	0x4000
EXPORT(invalid_pte_table)

		.org	0x5000
/* XXX This label is required to keep GAS trying to be too clever ...
   Bug?  */
dummy:
/*
 * Align to 8kb boundary for init_task_union which follows in the
 * .text segment.
 */
		.align	13
