/* Assembler version of memcpy() using SIMD instructions. */
	
/*

Copyright (C) 2001  Sony Computer Entertainment Inc.
Copyright 2001  Sony Corportaion.

This file is subject to the terms and conditions of the GNU General Public
License.  See the file "COPYING" in the main directory of this archive
for more details.

*/
/*
 * Unified implementation of memcpy, memmove and the __copy_user backend.
 * For __rmemcpy and memmove an exception is always a kernel bug, therefore
 * they're not protected.  In order to keep the exception fixup routine
 * simple all memory accesses in __copy_user to src rsp. dst are stricly
 * incremental.  The fixup routine depends on $at not being changed.
 */

/*
 * NOTE:
 * __copy_user is called though asm() which clobbers "$4", "$5", "$6", "$8", 
 * "$9", "$10", "$11", "$12", "$15", "$24", "$31". This means you can 
 * NOT use t5($13), t6($14) and t9($25) as temporay register.
 * Please read include/asm-mips/uaccess.h, for details.
 */


#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/regdef.h>

/*
 * The fixup routine for copy_to_user depends on copying strictly in
 * increasing order.  Gas expands the ulw/usw macros in the wrong order for
 * little endian machines, so we cannot depend on them.
 */
#ifdef __MIPSEB__
#define usdL	sdl
#define usdU	sdr
#define uldL	ldl
#define uldU	ldr
#elif defined __MIPSEL__
#define usdL	sdr
#define usdU	sdl
#define uldL	ldr
#define uldU	ldl
#else
#error "???"
#endif

#define EX(insn,reg,addr,handler)			\
10009:	insn	reg, addr;				\
	.section __ex_table,"a"; 			\
	PTR	10009b, handler; 			\
	.previous

#define UEX(insn,reg,addr,handler)			\
10019:	insn ## L reg, addr;				\
10010:	insn ## U reg, 7 + addr;			\
	.section __ex_table,"a"; 			\
	PTR	10019b, handler; 			\
	PTR	10010b, handler; 			\
	.previous


	.set	mips3
	.text
	.p2align	4
LEAF(memmove)
	/* NOTE: 						    */
	/*	small_memcpy() is always overlap-safe, but 	    */
	/*	memcpy() needs 8 bytes margin to align double word. */

	/* if ((src-dst)>8 || (src+len)<dst)	*/
	/*	goto memcpy			*/
	/* if (src>dst)				*/
	/*	goto small_memcpy		*/

	subu	t0, a1, a0;	// t0 = src - dst

	bgt	t0, 8, $L_memcpy	// if (src>dst+8) goto memcpy

	.set	noreorder
	bgtz	t0, small_memcpy	// if (src>dst) goto small_memcpy
	 move	v0, a0
	.set	reorder

	addu	t0, a2, t0	// t0 = src + len - dst

	.set	noreorder
	bltz	t0,  $L_memcpy	// if (src+len<dst) goto memcpy
	 xor	t0, a0, a1
	.set	reorder

	/* if (dst == src)		*/
	/*	return dst;		*/

	.set	noreorder
	beqz	t0, memcpy_out
	 move	v0, a0
	.set	reorder

//	b	small_rmemcpy

small_rmemcpy:
	/* a0 += a2; a1 += a2;			*/
	/* if (a2<8) goto small_byte_memcpy	*/
	/* a0 -= 7; a1 -= 7;			*/
	/* for (; a2>=8; a2-=8) { 		*/
	/*	t0 = * (dword *) a1     // possibly unaligned */
	/*	* (dword *) a0     = t0 // possibly unaligned */
	/*	a0 -= 8 			*/
	/*	a1 -= 8 			*/
	/* }					*/

	addu	a0, a2
	addu	a1, a2
	subu	t2, a2, 8
	bltz	t2, small_byte_rmemcpy
	.p2align	3
0:	
		uld	t0, -8(a1)
		subu	t2, 8
		usd	t0, -8(a0)
		subu	a1, 8
		.set	noreorder
		bgez	t2, 0b
		 subu	a0, 8
		.set	reorder

	addu	a2, t2, 8

small_byte_rmemcpy:
	beqz	a2, memcpy_out

	.set	noreorder
#define BYTE_CPY_BWD(offset) \
	lb	t0, offset(a1);	\
	subu	a2, 1;	\
	beqz	a2, memcpy_out; \
	 sb	t0, offset(a0)

	BYTE_CPY_BWD(-1)
	BYTE_CPY_BWD(-2)
	BYTE_CPY_BWD(-3)
	BYTE_CPY_BWD(-4)
	BYTE_CPY_BWD(-5)
	BYTE_CPY_BWD(-6)
	BYTE_CPY_BWD(-7)
	j	ra;
	  move	a2, zero
	.set	reorder

END(memmove)



	/*
	       void *memcpy(void *dest, const void *src, size_t n);
	 */

	.text
	.align	4
	.set	noat

LEAF(memcpy)
$L_memcpy:
EXPORT(__copy_user)
	addu	AT, a1, a2
	/* ret_val = a0;			*/
	/* if (a2<32) goto small_memcpy;	*/

	slt	v0, a2, 32
	.set	noreorder
	bne	v0, zero, small_memcpy
	  move	v0, a0
	.set	reorder

	/* t2 = a0 ^ a1;			*/
	/* if (((t2&0x7)!=0) 			*/
	/*	 goto ungligned_memcpy;		*/
	/* if ((t2&0xf)==0)			*/
	/*	max_align=qword			*/
	/* else 				*/
	/*	max_align=dword;		*/

	xor	t0, a1, a0
	and	t3, t0, 0x7
	.set	noreorder
	bnez	t3, unaligned_memcpy
	 and	t2, t0, 0xf
	.set	reorder


	/* t1 = (8 - (a0 & 7)) & 7 		*/
	/* if (t1 != 0) {			*/
	/*	t0 = * (dword *) a1 // unaligned	*/
	/*	* (dword *) a0 = t0 // unaligned	*/
	/*	a0 += t1 			*/
	/*	a1 += t1 			*/
	/*	a2 -= t1 			*/
	/* }					*/

	subu	t1, zero, a0
	and	t1, t1, 7	// t1: distance to be double word aligned 
	.set	noreorder
	beqz	t1, 0f
	 subu	a2, t1
	.set	reorder
		// align first double word
		//	NOTE: This way is NOT overlap-safe.
		UEX(uld, t0, 0(a1), l_fixup)
		addu	a1, t1
		UEX(usd, t0, 0(a0), s_fixup_a2_t1)
		addu	a0, t1
	0:

	/* if (max_align==dword) {		*/
	/*	memcpy_dword(a0,a1,a2)		*/
	/*	return ret_val 			*/
	/* }					*/

try_double_word_cpy:
	.set	noreorder
	bnez	t2, memcpy_dword
	  and	t1, a0, 8
	.set	reorder
	
	/* if (a0 & 8) {			*/
	/* 	*(dword *) a0 =  *(dword *) a1;	*/
	/* 	a2 -= 8;			*/
	/* 	a0 += 8; a1 += 8;		*/
	/* }					*/
	/*					*/
	/*					*/
	/* memcpy_qword(a0,a1,a2);		*/
	/* return ret_val			*/

align_quad_word:
	.set	noreorder
	beqz	t1, memcpy_qword
	 subu	t2, a2, 32
	.set	reorder

		EX(ld,	t0, (a1), l_fixup)
		addu	a1, 8
		EX(sd,	t0, (a0), s_fixup)
		addu	a0, 8
		subu	a2, 8
	 	subu	t2, a2, 32

memcpy_qword:
	/* for (; a2>=32; a2-=32) { 		*/
	/*	t0 = * (qword *) a1     // aligned */
	/*	t1 = * (qword *) (a1+16) // aligned */
	/*	* (qword *) a0     = t0 // aligned */
	/*	* (qword *) (a0+16) = t1 // aligned */
	/*	a0 += 32 			*/
	/*	a1 += 32 			*/
	/* }					*/
	/* goto small_memcpy;			*/

	// assume (a2 >= 16)
	bltz	t2, memcpy_dword
	.p2align	3
0:
		EX(lq,	t0, (a1), l_fixup)
		subu	t2, 32
		EX(lq,	t1, 16(a1), l_fixup)
		addu	a1, 32
		EX(sq,	t0, (a0), s_fixup_t2_64)
		EX(sq,	t1, 16(a0), s_fixup_t2_64)
		.set	noreorder
		bgez	t2, 0b
		 addu	a0, 32
		.set	reorder

	.set	noreorder
	b	small_memcpy
	 addu	a2, t2, 32
	.set	reorder

memcpy_dword: 
	/* for (; a2>=16; a2-=16) { 		*/
	/*	t0 = * (dword *) a1     // aligned */
	/*	t1 = * (dword *) (a1+8) // aligned */
	/*	* (dword *) a0     = t0 // aligned */
	/*	* (dword *) (a0+8) = t1 // aligned */
	/*	a0 += 16 			*/
	/*	a1 += 16 			*/
	/* }					*/
	/* goto small_memcpy;			*/

	// assume (a2 >= 16)
	subu	t2, a2, 16
	.p2align	3
0:	
		EX(ld,	t0, (a1), l_fixup)
		subu	t2, 16
		EX(ld,	t1, 8(a1), l_fixup)
		addu	a1, 16
		EX(sd,	t0, (a0), s_fixup_t2_32)
		EX(sd,	t1, 8(a0), s_fixup_t2_32)
		.set	noreorder
		bgez	t2, 0b
		 addu	a0, 16
		.set	reorder

	.set	noreorder
	b	small_memcpy
	 addu	a2, t2, 16
	.set	reorder

	.align	4
unaligned_memcpy:

	// align soruce to double word

	/* t1 = (8 - (a1 & 7)) & 7 		*/
	/* if (t1 != 0) {			*/
	/*	t0 = * (dword *) a1 // unaligned		*/
	/*	* (dword *) a0 = t0 // possibly unaligned	*/
	/*	a0 += t1 			*/
	/*	a1 += t1 			*/
	/*	a2 -= t1 			*/
	/* }					*/

	subu	t1, zero, a1
	and	t1, t1, 7	// t1: distance to be double word aligned 
	.set	noreorder
	beqz	t1, 1f
	 subu	a2, t1
	.set	reorder
		// align first double word
		//	NOTE: This way is NOT overlap-safe.
		UEX(uld, t0, 0(a1), l_fixup)
		addu	a1, t1
		UEX(usd, t0, 0(a0), s_fixup_a2_t1)
		addu	a0, t1

1:
	/* for (; a2>=16; a2-=16) { 		*/
	/*	t0 = * (dword *) a1     // aligned */
	/*	t1 = * (dword *) (a1+8) // aligned */
	/*	* (dword *) a0     = t0 // possibly unaligned */
	/*	* (dword *) (a0+8) = t1 // possibly unaligned */
	/*	a0 += 16 			*/
	/*	a1 += 16 			*/
	/* }					*/
	subu	t2, a2, 16
	.p2align	3
0:	
		EX(ld,	t0, (a1), l_fixup)
		subu	t2, 16
		EX(ld,	t1, 8(a1), l_fixup)
		addu	a1, 16
		UEX(usd, t0, 0(a0), s_fixup_t2_32)
		UEX(usd, t1, 8(a0), s_fixup_t2_32)
		.set	noreorder
		bgez	t2, 0b
		 addu	a0, 16
		.set	reorder

	 addu	a2, t2, 16
	.align	4
small_memcpy:
	/* if (a2<8) goto small_byte_memcpy	*/
	/* for (; a2>=8; a2-=8) { 		*/
	/*	t0 = * (dword *) a1     // possibly unaligned */
	/*	* (dword *) a0     = t0 // possibly unaligned */
	/*	a0 += 8 			*/
	/*	a1 += 8 			*/
	/* }					*/

	subu	t2, a2, 8
	bltz	t2, small_byte_memcpy
	.p2align	3
0:	
		UEX(uld, t0, 0(a1), l_fixup)
		subu	t2, 8
		UEX(usd, t0, 0(a0), s_fixup_t2_16)
		addu	a0, 8
		.set	noreorder
		bgez	t2, 0b
		 addu	a1, 8
		.set	reorder

	addu	a2, t2, 8

small_byte_memcpy:
	beqz	a2, memcpy_out

	.set	noreorder
#define BYTE_CPY_FWD(offset) \
	EX(lb,	t0, offset(a1), l_fixup);	\
	subu	a2, 1;	\
	beqz	a2, memcpy_out; \
	 EX(sb,	t0, offset(a0), s_fixup_a2_1)

	BYTE_CPY_FWD(0)
	BYTE_CPY_FWD(1)
	BYTE_CPY_FWD(2)
	BYTE_CPY_FWD(3)
	BYTE_CPY_FWD(4)
	BYTE_CPY_FWD(5)
	BYTE_CPY_FWD(6)
	.set	reorder
memcpy_out:
	.set	noreorder
	j	ra
	  move	a2, zero
	.set	reorder

END(memcpy)

	.set	noreorder

l_fixup:					# clear the rest of the buffer
	lw	t0, THREAD_BUADDR($28)
	subu	a2, AT, t0			# a2 bytes to go
	addu	a0, t0				# compute start address in a1
	subu	a0, a1
	j	__bzero
	 move	a1, zero

s_fixup_t2_64:
	jr	ra
	 addu	a2, t2, 64
s_fixup_t2_32:
	jr	ra
	 addu	a2, t2, 32
s_fixup_t2_16:
	jr	ra
	 addu	a2, t2, 16
s_fixup_a2_t1:
	jr	ra
	 addu	a2, t1
s_fixup_a2_1:
	jr	ra
	 addu	a2, 1
s_fixup:
	jr	ra
	 nop
