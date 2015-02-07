/*
 * r5900_perf_counter.h - r5900 performance counter description
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */



/* TARGET0/1 (EXL0/1, K0/1, S0/1, U0/1) fields of CCR register */
#define CCR_TARGET_EXL	1
#define CCR_TARGET_K	2
#define CCR_TARGET_S	4
#define CCR_TARGET_U	8
#define CCR_TARGET_ALL	(CCR_TARGET_EXL|CCR_TARGET_K|CCR_TARGET_S|CCR_TARGET_U)

/* EVENT0 fields of CCR register */
#define CCR_CTR0_CPU_CYCLE           1   /* Processor cycle		*/
#define CCR_CTR0_SINGLE_ISSUE        2   /* Single instructions issue	*/
#define CCR_CTR0_BRANCH_ISSUED       3   /* Branch issued		*/
#define CCR_CTR0_BTAC_MISS           4   /* BTAC miss			*/
#define CCR_CTR0_TLB_MISS            5   /* TLB miss			*/
#define CCR_CTR0_ICACHE_MISS         6   /* Instruction cache miss	*/
#define CCR_CTR0_DTLB_ACCESSED       7   /* DTLB accessed		*/
#define CCR_CTR0_NONBLOCK_LOAD       8   /* Non-blocking load		*/
#define CCR_CTR0_WBB_SINGLE_REQ      9   /* WBB single request		*/
#define CCR_CTR0_WBB_BURST_REQ       10  /* WBB burst request		*/
#define CCR_CTR0_ADDR_BUS_BUSY       11  /* CPU address bus busy	*/
#define CCR_CTR0_INST_COMP           12  /* Instruction completed	*/
#define CCR_CTR0_NON_BDS_COMP        13  /* Non-BDS instruction completed	*/
#define CCR_CTR0_COP2_COMP           14  /* COP2 instruction completed	*/
#define CCR_CTR0_LOAD_COMP           15  /* Load completed		*/
#define CCR_CTR0_NO_EVENT            16  /* No event			*/

/* EVENT1 fields of CCR register */
#define CCR_CTR1_LOW_BRANCH_ISSUED   0   /* Low-order branch issued	*/
#define CCR_CTR1_CPU_CYCLE           1   /* Processor cycle		*/
#define CCR_CTR1_DUAL_ISSUE          2   /* Dual instructions issue	*/
#define CCR_CTR1_BRANCH_MISS_PREDICT 3   /* Branch miss-predicted	*/
#define CCR_CTR1_ITLB_MISS           4   /* ITLB miss			*/
#define CCR_CTR1_DTLB_MISS           5   /* DTLB miss			*/
#define CCR_CTR1_DCACHE_MISS         6   /* Data cache miss		*/
#define CCR_CTR1_WBB_SINGLE_UNAVAIL  7   /* WBB single request unavailable	*/
#define CCR_CTR1_WBB_BURST_UNAVAIL   8   /* WBB burst request unavailable	*/
#define CCR_CTR1_WBB_BURST_ALMOST    9   /* WBB burst request almost full	*/
#define CCR_CTR1_WBB_BURST_FULL      10  /* WBB burst request full	*/
#define CCR_CTR1_DATA_BUS_BUSY       11  /* CPU data bus busy		*/
#define CCR_CTR1_INST_COMP           12  /* Instruction completed	*/
#define CCR_CTR1_NON_BDS_COMP        13  /* Non-BDS instruction completed	*/
#define CCR_CTR1_COP1_COMP           14  /* COP1 instruction completed	*/
#define CCR_CTR1_STORE_COMP          15  /* Store completed		*/
#define CCR_CTR1_NO_EVENT            16  /* No event			*/


#define CCR_EVENT0_SHIFT 5
#define CCR_EVENT1_SHIFT 15
#define CCR_TARGET0_SHIFT 1
#define CCR_TARGET1_SHIFT 11
#define CCR_CTE_SHIFT 31

#define CCR_GET_BITS( src, mask) \
		((src) & (mask))

#define CCR_GET_VAL( src, pos, maskval) \
		((CCR_GET_BITS ((src), (maskval)<<(pos))) >> (pos))

#define CCR_GET_CTE(src) \
	( CCR_GET_VAL(src, CCR_CTE_SHIFT, 0x1) )

#define CCR_GET_CTR0_TARGET(src) \
	( CCR_GET_VAL(src, CCR_TARGET0_SHIFT, 0xf) )

#define CCR_GET_CTR0_EVENT(src) \
	( CCR_GET_VAL(src, CCR_EVENT0_SHIFT, 0x1f) )

#define CCR_GET_CTR1_TARGET(src) \
	( CCR_GET_VAL(src, CCR_TARGET1_SHIFT, 0xf) )

#define CCR_GET_CTR1_EVENT(src) \
	( CCR_GET_VAL(src, CCR_EVENT1_SHIFT, 0x1f) )


#define CCR_SET_BITS( src, val, mask) \
		(((src) & ~(mask)) | ((val) &( mask)))

#define CCR_SET_VAL( src, val, pos, maskval) \
		(CCR_SET_BITS ((src), ((val)<<(pos)), ((maskval)<<(pos))))

#define CCR_SET_CTE(src, val) \
	CCR_SET_VAL(src, val, CCR_CTE_SHIFT, 1)

#define CCR_SET_CTR0_TARGET(src, val) \
	CCR_SET_VAL(src, val, CCR_TARGET0_SHIFT, 0xf)

#define CCR_SET_CTR0_EVENT(src, val) \
	CCR_SET_VAL(src, val, CCR_EVENT0_SHIFT, 0x1f)

#define CCR_SET_CTR1_TARGET(src, val) \
	CCR_SET_VAL(src, val, CCR_TARGET1_SHIFT, 0xf)

#define CCR_SET_CTR1_EVENT(src, val) \
	CCR_SET_VAL(src, val, CCR_EVENT1_SHIFT, 0x1f)



#ifdef __KERNEL__

#ifndef _LANGUAGE_ASSEMBLY
#include <asm/types.h>
static
__inline__
__u32 get_CTR0(void)
{
	__u32 r;
	asm volatile (
		"mfpc	%0,0;"
                : "=r" (r) /* output */
		:
           );
	return r;

}

static
__inline__
__u32 get_CTR1(void)
{
	__u32 r;
	asm volatile (
		"mfpc	%0,1;"
                : "=r" (r) /* output */
		:
           );
	return r;

}


static
__inline__
__u32 get_CCR(void)
{
	__u32 r;
	asm volatile (
		"mfps	%0,0;"
                : "=r" (r) /* output */
	   	:
           );
	return r;

}

#define CTR_BIT_OFF(bit_num, var) \
		var & ~( 0x1 << bit_num )

static
__inline__
void set_CTR0(__u32 r)
{
	r = CTR_BIT_OFF(31, r);
	asm volatile (
		"mtpc	%0,0;"
		"sync.p;"
		:
                : "r" (r)
           );

}

static
__inline__
void set_CTR1(__u32 r)
{
	r = CTR_BIT_OFF(31, r);
	asm volatile (
		"mtpc	%0,1;"
		"sync.p;"
		:
                : "r" (r)
           );
}

static
__inline__
void set_CCR(__u32 r)
{
	asm volatile (
		"mtps	%0,0;"
		"sync.p;"
                :
                : "r" (r)
           );
}
#endif /* _LANGUAGE_ASSEMBLY */

#endif /* __KERNEL__ */

