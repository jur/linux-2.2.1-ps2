/*
 *  include/asm-mips/stackframe.h
 *
 *  Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Paul M. Antoine.
 *  Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * $Id: stackframe.h,v 1.8 1999/05/01 10:08:19 harald Exp $
 */
#ifndef __ASM_MIPS_STACKFRAME_H
#define __ASM_MIPS_STACKFRAME_H

#include <linux/autoconf.h>
#include <asm/asm.h>
#include <asm/offset.h>

#ifdef CONFIG_CPU_R5900
#define SYNC_AFTER_MTC0	sync.p
#else
#define SYNC_AFTER_MTC0	
#endif

#ifdef CONFIG_CONTEXT_R5900
#include <asm/r5900_offset.h>
#define L_GREG	lq
#define S_GREG	sq
#define MOVE(dst,src)	por	dst, src, $0
#else
#define L_GREG	lw
#define S_GREG	sw
#define MOVE(dst,src)	move	dst, src
#endif

#define SAVE_AT                                          \
		S_GREG	$1, PT_R1(sp)

#ifdef CONFIG_CONTEXT_R5900
#define SAVE_TEMP                                        \
		pmfhi	v1;                              \
		S_GREG	$8, PT_R8(sp);                   \
		S_GREG	$9, PT_R9(sp);                   \
		S_GREG	v1, PT_HI(sp);                   \
		pmflo	v1;                              \
		S_GREG	$10, PT_R10(sp);                 \
		S_GREG	$11, PT_R11(sp);                 \
		S_GREG	v1,  PT_LO(sp);                  \
		S_GREG	$12, PT_R12(sp);                 \
		S_GREG	$13, PT_R13(sp);                 \
		S_GREG	$14, PT_R14(sp);                 \
		S_GREG	$15, PT_R15(sp);                 \
		mfsa    v1;				 \
		S_GREG	$24, PT_R24(sp);		 \
		sw	v1,  PT_SA(sp)
#else /* CONFIG_CONTEXT_R5900 */
#define SAVE_TEMP                                        \
		mfhi	v1;                              \
		S_GREG	$8, PT_R8(sp);                   \
		S_GREG	$9, PT_R9(sp);                   \
		S_GREG	v1, PT_HI(sp);                   \
		mflo	v1;                              \
		S_GREG	$10, PT_R10(sp);                 \
		S_GREG	$11, PT_R11(sp);                 \
		S_GREG	v1,  PT_LO(sp);                  \
		S_GREG	$12, PT_R12(sp);                 \
		S_GREG	$13, PT_R13(sp);                 \
		S_GREG	$14, PT_R14(sp);                 \
		S_GREG	$15, PT_R15(sp);                 \
		S_GREG	$24, PT_R24(sp)
#endif /* CONFIG_CONTEXT_R5900 */

#define SAVE_STATIC                                      \
		S_GREG	$16, PT_R16(sp);                 \
		S_GREG	$17, PT_R17(sp);                 \
		S_GREG	$18, PT_R18(sp);                 \
		S_GREG	$19, PT_R19(sp);                 \
		S_GREG	$20, PT_R20(sp);                 \
		S_GREG	$21, PT_R21(sp);                 \
		S_GREG	$22, PT_R22(sp);                 \
		S_GREG	$23, PT_R23(sp);                 \
		S_GREG	$30, PT_R30(sp)

#define __str2(x) #x
#define __str(x) __str2(x)

#ifdef CONFIG_CONTEXT_R5900
#define save_static(frame)                               \
	__asm__ __volatile__(                            \
		"sq\t$16,"__str(PT_R16)"(%0)\n\t"        \
		"sq\t$17,"__str(PT_R17)"(%0)\n\t"        \
		"sq\t$18,"__str(PT_R18)"(%0)\n\t"        \
		"sq\t$19,"__str(PT_R19)"(%0)\n\t"        \
		"sq\t$20,"__str(PT_R20)"(%0)\n\t"        \
		"sq\t$21,"__str(PT_R21)"(%0)\n\t"        \
		"sq\t$22,"__str(PT_R22)"(%0)\n\t"        \
		"sq\t$23,"__str(PT_R23)"(%0)\n\t"        \
		"sq\t$30,"__str(PT_R30)"(%0)\n\t"        \
		: /* No outputs */                       \
		: "r" (frame))
#else /* CONFIG_CONTEXT_R5900 */
#define save_static(frame)                               \
	__asm__ __volatile__(                            \
		"sw\t$16,"__str(PT_R16)"(%0)\n\t"        \
		"sw\t$17,"__str(PT_R17)"(%0)\n\t"        \
		"sw\t$18,"__str(PT_R18)"(%0)\n\t"        \
		"sw\t$19,"__str(PT_R19)"(%0)\n\t"        \
		"sw\t$20,"__str(PT_R20)"(%0)\n\t"        \
		"sw\t$21,"__str(PT_R21)"(%0)\n\t"        \
		"sw\t$22,"__str(PT_R22)"(%0)\n\t"        \
		"sw\t$23,"__str(PT_R23)"(%0)\n\t"        \
		"sw\t$30,"__str(PT_R30)"(%0)\n\t"        \
		: /* No outputs */                       \
		: "r" (frame))
#endif /* CONFIG_CONTEXT_R5900 */

#define SAVE_SOME                                        \
		.set	push;                            \
		.set	reorder;                         \
		mfc0	k0, CP0_STATUS;                  \
		sll	k0, 3;     /* extract cu0 bit */ \
		.set	noreorder;                       \
		bltz	k0, 8f;                          \
		move	k1, sp;                          \
		.set	reorder;                         \
		/* Called from user mode, new stack. */  \
		lui	k1, %hi(kernelsp);               \
		lw	k1, %lo(kernelsp)(k1);           \
8:                                                       \
		MOVE	(k0, sp);                        \
		subu	sp, k1, PT_SIZE;                 \
		S_GREG	k0, PT_R29(sp);                  \
		S_GREG	$3, PT_R3(sp);                   \
		S_GREG	$0, PT_R0(sp);			 \
		mfc0	v1, CP0_STATUS;                  \
		S_GREG	$2, PT_R2(sp);                   \
		sw	v1, PT_STATUS(sp);               \
		S_GREG	$4, PT_R4(sp);                   \
		mfc0	v1, CP0_CAUSE;                   \
		S_GREG	$5, PT_R5(sp);                   \
		sw	v1, PT_CAUSE(sp);                \
		S_GREG	$6, PT_R6(sp);                   \
		mfc0	v1, CP0_EPC;                     \
		S_GREG	$7, PT_R7(sp);                   \
		sw	v1, PT_EPC(sp);                  \
		S_GREG	$25, PT_R25(sp);                 \
		S_GREG	$28, PT_R28(sp);                 \
		S_GREG	$31, PT_R31(sp);                 \
		ori	$28, sp, 0x1fff;                 \
		xori	$28, 0x1fff;                     \
		.set	pop

#define SAVE_ALL                                         \
		SAVE_SOME;                               \
		SAVE_AT;                                 \
		SAVE_TEMP;                               \
		SAVE_STATIC

#define RESTORE_AT                                       \
		L_GREG	$1,  PT_R1(sp)

#define RESTORE_SP                                       \
		L_GREG	sp,  PT_R29(sp)

#ifdef CONFIG_CONTEXT_R5900
#define RESTORE_TEMP                                     \
		L_GREG	$24, PT_LO(sp);                  \
		L_GREG	$8, PT_R8(sp);                   \
		pmtlo	$24;                             \
		L_GREG	$9, PT_R9(sp);                   \
		L_GREG	$24, PT_HI(sp);                  \
		L_GREG	$10,PT_R10(sp);                  \
		pmthi	$24;                             \
		L_GREG	$11, PT_R11(sp);                 \
		L_GREG	$12, PT_R12(sp);                 \
		L_GREG	$13, PT_R13(sp);                 \
		lw	$24, PT_SA(sp);			 \
		L_GREG	$14, PT_R14(sp);                 \
		mtsa	$24;				 \
		L_GREG	$15, PT_R15(sp);                 \
		L_GREG	$24, PT_R24(sp)
#else /* CONFIG_CONTEXT_R5900 */
#define RESTORE_TEMP                                     \
		L_GREG	$24, PT_LO(sp);                  \
		L_GREG	$8, PT_R8(sp);                   \
		L_GREG	$9, PT_R9(sp);                   \
		mtlo	$24;                             \
		L_GREG	$24, PT_HI(sp);                  \
		L_GREG	$10,PT_R10(sp);                  \
		L_GREG	$11, PT_R11(sp);                 \
		mthi	$24;                             \
		L_GREG	$12, PT_R12(sp);                 \
		L_GREG	$13, PT_R13(sp);                 \
		L_GREG	$14, PT_R14(sp);                 \
		L_GREG	$15, PT_R15(sp);                 \
		L_GREG	$24, PT_R24(sp)
#endif /* CONFIG_CONTEXT_R5900 */

#define RESTORE_STATIC                                   \
		L_GREG	$16, PT_R16(sp);                 \
		L_GREG	$17, PT_R17(sp);                 \
		L_GREG	$18, PT_R18(sp);                 \
		L_GREG	$19, PT_R19(sp);                 \
		L_GREG	$20, PT_R20(sp);                 \
		L_GREG	$21, PT_R21(sp);                 \
		L_GREG	$22, PT_R22(sp);                 \
		L_GREG	$23, PT_R23(sp);                 \
		L_GREG	$30, PT_R30(sp)

#define RESTORE_SOME                                     \
		.set	push;                            \
		.set	reorder;                         \
		mfc0	t0, CP0_STATUS;                  \
		.set	pop;                             \
		ori	t0, 0x1f;                        \
		xori	t0, 0x1f;                        \
		.set	push;                            \
		.set	noreorder;                        \
		mtc0	t0, CP0_STATUS;                  \
		SYNC_AFTER_MTC0;			 \
		.set	pop;                             \
		li	v1, 0xff00;                      \
		and	t0, v1;				 \
		lw	v0, PT_STATUS(sp);               \
		nor	v1, $0, v1;			 \
		and	v0, v1;				 \
		or	v0, t0;				 \
		.set	push;                            \
		.set	noreorder;                        \
		mtc0	v0, CP0_STATUS;                  \
		SYNC_AFTER_MTC0;			 \
		.set	pop;                             \
		lw	v1, PT_EPC(sp);                  \
		.set	push;                            \
		.set	noreorder;                        \
		mtc0	v1, CP0_EPC;                     \
		SYNC_AFTER_MTC0;			 \
		.set	pop;                             \
		L_GREG	$31, PT_R31(sp);                 \
		L_GREG	$28, PT_R28(sp);                 \
		L_GREG	$25, PT_R25(sp);                 \
		L_GREG	$7,  PT_R7(sp);                  \
		L_GREG	$6,  PT_R6(sp);                  \
		L_GREG	$5,  PT_R5(sp);                  \
		L_GREG	$4,  PT_R4(sp);                  \
		L_GREG	$3,  PT_R3(sp);                  \
		L_GREG	$2,  PT_R2(sp)


#define RESTORE_ALL                                      \
		RESTORE_SOME;                            \
		RESTORE_AT;                              \
		RESTORE_TEMP;                            \
		RESTORE_STATIC;                          \
		RESTORE_SP

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
#define CLI                                             \
		mfc0	t0,CP0_STATUS;                  \
		li	t1,ST0_CU0|0x1f;                \
		or	t0,t1;                          \
		xori	t0,0x1f;                        \
		.set	push;				\
		.set	noreorder;			\
		mtc0	t0,CP0_STATUS;			\
		SYNC_AFTER_MTC0;			\
		.set	pop

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
#define STI                                             \
		mfc0	t0,CP0_STATUS;                  \
		li	t1,ST0_CU0|0x1f;                \
		or	t0,t1;                          \
		xori	t0,0x1e;                        \
		.set	push;				\
		.set	noreorder;			\
		mtc0	t0,CP0_STATUS;			\
		SYNC_AFTER_MTC0;			\
		.set	pop

/*
 * Just move to kernel mode and leave interrupts as they are.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
#define KMODE                                           \
		mfc0	t0,CP0_STATUS;                  \
		li	t1,ST0_CU0|0x1e;                \
		or	t0,t1;                          \
		xori	t0,0x1e;                        \
		.set	push;				\
		.set	noreorder;			\
		mtc0	t0,CP0_STATUS;			\
		SYNC_AFTER_MTC0;			\
		.set	pop

#endif /* __ASM_MIPS_STACKFRAME_H */
