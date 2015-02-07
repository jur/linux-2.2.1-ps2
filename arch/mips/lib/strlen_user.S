/* $Id: strlen_user.S,v 1.2 1998/05/04 09:12:54 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1998, 1999 by Ralf Baechle
 * Copyright (c) 1999 by Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/regdef.h>
#include <asm/sgidefs.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

#ifdef CONFIG_CPU_R5900	/* inhibit short loop */
#define EX2(insn,reg,addr,handler)			\
10:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	10b, handler;				\
	.previous
#endif
/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
LEAF(__strlen_user_asm)
	lw	v0, THREAD_CURDS($28)	# pointer ok?
	and	v0, a0
	bltz	v0, fault

EXPORT(__strlen_user_nocheck_asm)
	move	v0, a0
#ifdef CONFIG_CPU_R5900	/* inhibit short loop */
1:	EX(lb, t0, (v0), fault)
	.set noreorder
	beqz	t0, 2f
	addiu	v0, 1
	.set reorder

	EX2(lb, t0, (v0), fault)
	.set noreorder
	bnez		t0, 1b
	addiu	v0, 1
	.set reorder
2:
#else
1:	EX(lb, t0, (v0), fault)
	addiu	v0, 1
	bnez	t0, 1b
#endif
	subu	v0, a0
	jr	ra
	END(__strlen_user_nocheck_asm)

	.section __ex_table,"a"
	PTR	1b, fault
	.previous

fault:	move	v0, zero
	jr	ra
