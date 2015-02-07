/*
 * r5900_perf.h - r5900 spcific perf counter serivce 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <asm/sys_r5900.h>

extern volatile struct r5900_upper_ctrs r5900_upper_ctrs;


inline static u32 enter_cs(void)
{
	// pause perf counter
	u32 ccr, ret;
	ccr = get_CCR();
	ret = ccr;
	ccr = CCR_SET_CTE (ccr,0);
	set_CCR(ccr);
	return ret;
}

inline static void leave_cs(u32 val)
{
	// continue perf counter
	set_CCR(val);
}


static
__inline__
__u64 get_CTR0_64(void)
{
	__u64 r;
	__u32 ccr;

	ccr= enter_cs();
	r = (__u64) get_CTR0();
	r |=  ((__u64 ) r5900_upper_ctrs.ctr0  << 31);
	leave_cs(ccr);

	return r;
}

static
__inline__
void set_CTR0_64(__u64 r)
{
	__u32 ccr;

	ccr= enter_cs();
	set_CTR0( (__u32) (r & (__u32) 0x7fffffff) );
	r5900_upper_ctrs.ctr0 = (__u32) (r >> 31);
	leave_cs(ccr);
}



static
__inline__
__u64 get_CTR1_64(void)
{
	__u64   r;
	__u32 ccr;

	ccr= enter_cs();
	r = (__u64) get_CTR1();
	r |=  ((__u64 ) r5900_upper_ctrs.ctr1  << 31);
	leave_cs(ccr);

	return r;
}

static
__inline__
void set_CTR1_64(__u64 r)
{

	__u32 ccr;

	ccr= enter_cs();
	set_CTR1( (__u32) (r & (__u32) 0x7fffffff) );
	r5900_upper_ctrs.ctr1 = (__u32) (r >> 31);
	leave_cs(ccr);
}

