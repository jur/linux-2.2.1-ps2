/* $Id: r2300_misc.S,v 1.3 1999/05/01 22:40:36 ralf Exp $
 * r2300_misc.S: Misc. exception handling code for R3000/R2000.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Multi-CPU abstraction reworking:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * Further modifications to make this work:
 * Copyright (c) 1998 Harald Koerfgen
 * Copyright (c) 1998 Gleb Raiko & Vladimir Roganov
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 */
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/current.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/segment.h>
#include <asm/stackframe.h>

	.text
	.set	mips1
	.set	noreorder

#undef NOTLB_OPTIMIZE /* If you are paranoid, define this. */

	/* ABUSE of CPP macros 101. */

	/* After this macro runs, the pte faulted on is
	 * in register PTE, a ptr into the table in which
	 * the pte belongs is in PTR.
	 */
#define LOAD_PTE(pte, ptr) \
	mfc0	pte, CP0_BADVADDR; \
	_GET_CURRENT(ptr); \
	srl	pte, pte, 22; \
	lw	ptr, THREAD_PGDIR(ptr); \
	sll	pte, pte, 2; \
	addu	ptr, pte, ptr; \
	mfc0	pte, CP0_CONTEXT; \
	lw	ptr, (ptr); \
	andi	pte, pte, 0xffc; \
	addu	ptr, ptr, pte; \
	lw	pte, (ptr); \
	nop;

	/* This places the even/odd pte pair in the page
	 * table at PTR into ENTRYLO0 and ENTRYLO1 using
	 * TMP as a scratch register.
	 */
#define PTE_RELOAD(ptr) \
	lw	ptr, (ptr)	; \
	nop			; \
	mtc0	ptr, CP0_ENTRYLO0; \
	nop;

#define DO_FAULT(write) \
	.set	noat; \
	.set	macro; \
	SAVE_ALL; \
	mfc0	a2, CP0_BADVADDR; \
	STI; \
	.set	at; \
	move	a0, sp; \
	jal	do_page_fault; \
	 li	a1, write; \
	j	ret_from_sys_call; \
	 nop; \
	.set	noat; \
	.set	nomacro;

	/* Check is PTE is present, if not then jump to LABEL.
	 * PTR points to the page table where this PTE is located,
	 * when the macro is done executing PTE will be restored
	 * with it's original value.
	 */
#define PTE_PRESENT(pte, ptr, label) \
	andi	pte, pte, (_PAGE_PRESENT | _PAGE_READ); \
	xori	pte, pte, (_PAGE_PRESENT | _PAGE_READ); \
	bnez	pte, label; \
	.set	push;       \
	.set	reorder;    \
	 lw	pte, (ptr); \
	.set	pop; 

	/* Make PTE valid, store result in PTR. */
#define PTE_MAKEVALID(pte, ptr) \
	ori	pte, pte, (_PAGE_VALID | _PAGE_ACCESSED); \
	sw	pte, (ptr);

	/* Check if PTE can be written to, if not branch to LABEL.
	 * Regardless restore PTE with value from PTR when done.
	 */
#define PTE_WRITABLE(pte, ptr, label) \
	andi	pte, pte, (_PAGE_PRESENT | _PAGE_WRITE); \
	xori	pte, pte, (_PAGE_PRESENT | _PAGE_WRITE); \
	bnez	pte, label; \
	.set    push;       \
	.set    reorder;    \
	lw      pte, (ptr); \
	.set    pop;
 

	/* Make PTE writable, update software status bits as well,
	 * then store at PTR.
	 */
#define PTE_MAKEWRITE(pte, ptr) \
	ori	pte, pte, (_PAGE_ACCESSED | _PAGE_MODIFIED | \
			   _PAGE_VALID | _PAGE_DIRTY); \
	sw	pte, (ptr);

	.set	noreorder

	.align	5
NESTED(r2300_handle_tlbl, PT_SIZE, sp)
	.set	noat

#ifndef NOTLB_OPTIMIZE
	/* Test present bit in entry. */
	LOAD_PTE(k0, k1)
        tlbp
	nop
        PTE_PRESENT(k0, k1, nopage_tlbl)
        PTE_MAKEVALID(k0, k1)
        PTE_RELOAD(k1)
	tlbwi
	nop
	mfc0	k0, CP0_EPC
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#else
	nop
#endif
	jr	k0
	 rfe
nopage_tlbl:
#endif

	DO_FAULT(0)
END(r2300_handle_tlbl)

NESTED(r2300_handle_tlbs, PT_SIZE, sp)
	.set	noat

#ifndef NOTLB_OPTIMIZE
	LOAD_PTE(k0, k1)
	tlbp                            # find faulting entry
	nop
	PTE_WRITABLE(k0, k1, nopage_tlbs)
	PTE_MAKEWRITE(k0, k1)
	PTE_RELOAD(k1)
	tlbwi
	nop
	mfc0	k0, CP0_EPC
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#else
	nop
#endif
	jr	k0
	 rfe
nopage_tlbs:
#endif

	DO_FAULT(1)
END(r2300_handle_tlbs)

	.align	5
NESTED(r2300_handle_mod, PT_SIZE, sp)
	.set	noat
#ifndef NOTLB_OPTIMIZE
	LOAD_PTE(k0, k1)
	tlbp					# find faulting entry
	andi	k0, k0, _PAGE_WRITE
	beqz	k0, nowrite_mod
	.set	push
	.set    reorder
	lw	k0, (k1)
	.set    pop

	/* Present and writable bits set, set accessed and dirty bits. */
	PTE_MAKEWRITE(k0, k1)

	/* Now reload the entry into the tlb. */
	PTE_RELOAD(k1)
	nop
	tlbwi
	nop
	mfc0	k0, CP0_EPC
	nop
	jr	k0
#if defined (CONFIG_MIPS_TST_DEV) || defined (CONFIG_MIPS_TST_DEV_MODULE)
	li k1, _TST_ACCESS_MAGIC
#endif
	 rfe
#endif

nowrite_mod:
	DO_FAULT(1)
END(r2300_handle_mod)
