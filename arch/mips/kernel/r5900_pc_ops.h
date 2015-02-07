/*
 * r5900_pc_rec.h - r5900 spcific pc sampling serivce 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#define PC_SAMPLE_NUM_IPIDS		5
#define PC_SAMPLE_DEFAULT_NUM_POOL	16
#define PC_SAMPLE_NUM_POOL		128

#ifndef _LANGUAGE_ASSEMBLY

struct ctrpair_struct {
	u8	event0, event1;
	u32	val0,	val1;
	volatile u32	current0, current1;
};


struct pc_buffer_descr {
	volatile u32	num_entry;		/* num of items */
	struct perf_sample_entry (*p_buffer)[];
};

struct pc_record {
	volatile u32	debug_base;
	volatile u32	debug_ptr;
	volatile u32	pool_in;
	volatile u32	pool_out;
	int		pool_num;
	u32		max_entry;	/* max items in each buffer */
	volatile u32	lost;	/* number of lost record */
	u32	reload0;	/* reload value for ctr0 */
	u32	reload1;	/* reload value for ctr1 */
	int	sample_mode;
	int	selector;
	int	num_of_ctrpair;
	int	cur_ctrpair_num;	
	struct	ctrpair_struct   *cur_ctrpair;
	struct	ctrpair_struct   *init_ctrpair;
	int	reg_a0 __attribute__((mode(TI),aligned(16)));	/* a0 reg */
	int	reg_a1 __attribute__((mode(TI),aligned(16)));	/* a1 reg */
	int	reg_a2 __attribute__((mode(TI),aligned(16)));	/* a2 reg */
	int	num_ipid;
	u8	ignore;
	pid_t	ipid[PC_SAMPLE_NUM_IPIDS];
	struct pc_buffer_descr buffer_pool[PC_SAMPLE_NUM_POOL];
};


#endif /* _LANGUAGE_ASSEMBLY */
