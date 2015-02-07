/*
 * r5900_perf.S - r5900 spcific perf counter serivce 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
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

#include <asm/r5900_offset.h>
#if defined(CONFIG_PERF_DEV) \
	|| defined(CONFIG_PERF_DEV_MODULE)
#include <linux/perf_dev.h>
#include "r5900_pc_ops.h"
#endif

#define OVF_SHIFT	31

	.data
#if defined(CONFIG_PERF_DEV) \
	|| defined(CONFIG_PERF_DEV_MODULE)
	.globl	r5900_perf_mode
r5900_perf_mode:
	.byte	PERF_MODE_UNKOWN
#endif

	.local  save_kregs
	.comm   save_kregs,32,16
#define	SAVE_K0_OFFSET 0
#define	SAVE_K1_OFFSET 16





	.text

LEAF(except_vec2_r5900_counter)

	/*
	 * R5900 performace couner exception service.
	 *
	 * For saving/restoring k0 and k1, use following COP0 regs.
	 *  	$28: TagLo ( CP0_TAGLO )
	 *  	$29: TagHi ( CP0_TAGHI )
	 * These are not used in current R5900 MIPS Linux kernel.
	 */

	.set push
	.set noat


	/*
	 * save k0, k1
	 */
	/* save LS32B or k0 */
	mtc0	k0, CP0_TAGLO
	sync.p
	/* save next LS32B of k0 */
	.set push
	.set mips3
	dsrl32	k0, k0, 0		/* This preserves MS64B */
	.set pop
	mtc0	k0, CP0_TAGHI
	sync.p
	/* save MS64B of k0 */
	la	k0, save_kregs		/* This preserves MS64B */
	sq	k0, SAVE_K0_OFFSET(k0)	/* MS64B of k0 is ignored for address */

	/* save k1 */
	sq	k1, SAVE_K1_OFFSET(k0)


	/*
	 *
	 */
#if defined(CONFIG_PERF_DEV) \
	|| defined(CONFIG_PERF_DEV_MODULE)

	la	k1, r5900_perf_mode
	lb	k0,(k1)
	li	k1, PERF_MODE_SAMPLE
	bne	k0, k1, 1f

	la	k1, r5900_perf_sample_pc # trampoline code for jump 
	j	k1
	nop
1:
#endif /* defined(CONFIG_PERF_DEV) || defined(CONFIG_PERF_DEV_MODULE) */

	la	k1, r5900_perf_count_up # trampoline code for jump 
	j	k1
	nop
	.set pop
	END(except_vec2_r5900_counter)


#if defined(CONFIG_PERF_DEV) \
	|| defined(CONFIG_PERF_DEV_MODULE)

/* M_GET_PC_SAMPLE_ENTRY_ADDR  */
/*	base =  base + index * PERF_ENTRY_SIZE */

#if PERF_ENTRY_SIZE == 12 /* x8 + x4 */

#define	M_GET_PC_SAMPLE_ENTRY_ADDR(base, index, TMP1, TMP2) \
	sll	TMP1,	index, 3; 	/*x8*/	\
	sll	TMP2,	index, 2; 	/*x4*/	\
	addu	TMP1,	TMP2, TMP1; 		\
	addu	base,	TMP1, base; 

#elif PERF_ENTRY_SIZE == 16 /* x16 */

#define	M_GET_PC_SAMPLE_ENTRY_ADDR(base, index, TMP1, TMP2) \
	sll	TMP1,	index, 4;	/*x16*/\
	addu	base,	TMP1, base; 

#elif PERF_ENTRY_SIZE == 20 /* x16 + x4 */

#define	M_GET_PC_SAMPLE_ENTRY_ADDR(base, index, TMP1, TMP2) \
	sll	TMP1,	index, 4;	/*x16*/	\
	sll	TMP2,	index, 2;	/* x4*/	\
	addu	TMP1,	TMP2, TMP1; 		\
	addu	base,	TMP1, base; 

#else
#error "sizeof(struct perf_sample_entry)!=12 , !=16 or !=20"
#endif

/* M_GET_NUM_ENT(dst, pc_record, pool_in, tmp) */
/*	Get <pc_record>.buffer_pool[<pool_in>].index to <dst> */

/* M_SET_NUM_ENT(src, pc_record, pool_in, tmp) */
/*	Set <pc_record>.buffer_pool[<pool_in>].index from <src> */

/* M_GET_BUFFER(dst, pc_record, pool_in, tmp) */
/*	Get <pc_record>.buffer_pool[<pool_in>].b_buffer to <dst> */

#if PC_BUFFDESCR_SIZE == 8

#define M_GET_NUM_ENT(dst, pc_record, pool_in, tmp) \
	/* tmp =  pool_in * sizeof(pc_record.buffer_pool[0]))*/\
	sll	tmp, pool_in,	3 ; /*x8*/	\
	addu	tmp, pc_record, tmp;		\
	lw	dst, PC_RECORD_P0NUMENT(tmp)

#define M_SET_NUM_ENT(src, pc_record, pool_in, tmp) \
	/* tmp =  pool_in * sizeof(pc_record.buffer_pool[0]))*/\
	sll	tmp, pool_in,	3 ; /*x8*/	\
	addu	tmp, pc_record, tmp;		\
	sw	src, PC_RECORD_P0NUMENT(tmp)

#define M_GET_BUFFER(dst, pc_record, pool_in, tmp) \
	/* tmp =  pool_in * sizeof(pc_record.buffer_pool[0]))*/\
	sll	tmp, pool_in,	3 ; /*x8*/	\
	addu	tmp, pc_record, tmp;		\
	lw	dst, PC_RECORD_P0BUFFER(tmp)

#else
#error "sizeof(pc_record.buffer_pool[0])) != 8"
#endif


/* M_INC_POOL_INDEX(dst, pc_record, x, tmp) */ 
/*	Get ((x+1) >= (pc_record.pool_num) ? 0 : (x+1)) */

#define M_INC_POOL_INDEX(dst, pc_record, src, tmp )	\
	/*	tmp = pc.record.pool_num; */		\
	/*	dst = src + 1; */			\
	/*	tmp = (dst >= tmp); */			\
	/*	if (tmp) dst = 0; */			\
							\
	lw	tmp, PC_RECORD_POOL_NUM(pc_record);	\
	addu	dst, src, 1;				\
	sge	tmp, dst, tmp;				\
	movn	dst, zero, tmp




LEAF(r5900_perf_sample_pc)
	.set push
	.set noat

	.extern pc_record, PC_RECORD_SIZE
	/* save a0 and set pc_record addr */
	la	k1, pc_record
	sq	a0, PC_RECORD_REG_A0(k1)
	sq	a1, PC_RECORD_REG_A1(k1)
	sq	a2, PC_RECORD_REG_A2(k1)
	move	a0, k1


	/*
	// get current buffer pool index
        u32 pool_in = pc_record.pool_in;
	// get current buffer index
        u32 num_entry = pc_record.buffer_pool[pc_record.pool_in].num_entry;

	// check current buffer full
        if (num_entry >=  pc_record.max_entry) {

                // try get next buffer
                u32 pool_out = pc_record.pool_out;
                u32 next_pool_in =  INC_POOL_INDEX(pool_in);
                if ( next_pool_in == pool_out) {
			// remove last record
                        pc_record.lost ++;
			num_entry  --;
			pc_record.buffer_pool[pool_in].num_entry = num_entry;
			goto L_rec_start:
                }
                pool_in = next_pool_in;
                pc_record.pool_in = pool_in;
                num_entry = 0;
                pc_record.buffer_pool[pool_in].num_entry = num_entry;
        }

	L_rec_start:
        PTR = &(* pc_record.buffer_pool[pool_in].p_buffer )[ num_entry ];
		:
	*PTR = somthing ...;
		:
		:
        pc_record.buffer_pool[pool_in].num_entry ++ ;

	*/



	/* check current buffer full */
        lw	a1, PC_RECORD_POOL_IN(a0)		// a1: pool_in
	M_GET_NUM_ENT(a2, a0, a1, k0)			// a2: num_entry
	lw	k0, PC_RECORD_MAX_ENTRY(a0) 		// k0: max_entry
	sgt	k0, k0, a2				// k0 = k0>a2
	bnez	k0, L_rec_start

	    /* try get next buffer */
	    M_INC_POOL_INDEX(k0, a0, a1, k1)		// k0: next_pool_in
	    lw		k1, PC_RECORD_POOL_OUT(a0)	// k1: pool_out
	    bne		k0, k1, L_next_buffer

		/* count up lost statistic */
		lw	k0, PC_RECORD_LOST(a0)
		addu	k0, 1
		sw	k0, PC_RECORD_LOST(a0)

		/*  remove last entry (dec. num_entry) */
		subu	a2, 1
		M_SET_NUM_ENT(a2, a0, a1, k0)		// a2: num_entry

		b	L_rec_start
L_next_buffer:
		/* update pool_in */
		move	a1, k0				// k0: next_pool_in
		sw	k0, PC_RECORD_POOL_IN(a0)	// a1: pool_in

		/* clear num_entry */
		move	a2, zero
		M_SET_NUM_ENT(zero, a0, a1, k0)		// a2: num_entry

L_rec_start:
	// At this point, A1 and A2 hold followings;
	// 	a1: pool_in
	// 	a2: num_entry

	/* get buffer pointer to A2 */
	M_GET_BUFFER(k1, a0, a1, k0)
	sw	k1, PC_RECORD_DEBUG_BASE(a0) // for debug
	M_GET_PC_SAMPLE_ENTRY_ADDR(k1, a2, k0, a1)
	move	a2, k1
	sw	k1, PC_RECORD_DEBUG_PTR(a0) // for debug



	/* record pc */
	mfc0	k1, CP0_ERROREPC
	sw	k1, PERF_ENTRY_PC(a2)

	/* record jiffies */
	.extern jiffies, SIZEOF_JIFFIES
	lui	k0, %hi(jiffies)
#if SIZEOF_JIFFIES == 4
	lw	k0, %lo(jiffies)(k0)
	sw	k0, PERF_ENTRY_JIFFIES(a2)
#elif SIZEOF_JIFFIES == 8
	ld	k0, %lo(jiffies)(k0)
	sd	k0, PERF_ENTRY_JIFFIES(a2)
#else
#error "sizeof(jiffies)"
#endif

	/* record  event type */
	mfc0    k0, CP0_STATUS
	sll     k0, 3	/* extract CU0 bit, this is linux's way 
				to detect kernel mode */ 
	li	k1, PERF_SAMPLE_EVENT_KERNEL
	bltz    k0, 1f
		/* Called from user mode. */
		li	k1, PERF_SAMPLE_EVENT_USER
    1:
	sb	k1, PERF_ENTRY_EVENT(a2)

	/* record pid and set to A1*/
	_GET_CURRENT(k0)
#if SIZEOF_PID == 4
	lw	a1, TASK_PID(k0)
	sw	a1, PERF_ENTRY_PID(a2)
#elif SIZEOF_PID == 8
	ld	a1, TASK_PID(k0)
	sd	a1, PERF_ENTRY_PID(a2)
#else
#error "SIZEOF_PID is not 4 or 8"
#endif

	/*  check IN_INTERRUPT, see <asm/hardirq.h> */
	.extern local_irq_count, 4
	.extern local_bh_count, 4
	lui	k0, %hi(local_irq_count)
	lw	k0, %lo(local_irq_count)(k0)
	bnez	k0, 1f
	    lui		k1, %hi(local_bh_count)
	    lw		k1, %lo(local_bh_count)(k1)
	    beqz	k1, L_process_ignore_pid
	    1:
		/* record event type */
		/* in INTERRUPT, store PERF_SAMPLE_EVENT_KERNEL */
		li	k0, PERF_SAMPLE_EVENT_KERNEL
		sb	k0, PERF_ENTRY_EVENT(a2)
		/* record pid */
		/* in INTERRUPT, store zero as PID */
#if SIZEOF_PID == 4
		sw	zero, PERF_ENTRY_PID(a2)
		move	a1, zero
#elif SIZEOF_PID == 8
		sd	zero, PERF_ENTRY_PID(a2)
		move	a1, zero
#else
#error "sizeof(pid_t)"
#endif


L_process_ignore_pid:
	/* A1: PERF_ENTRY_PID */
	// clear PC_RECORD_IGNORE
	sb	zero, PC_RECORD_IGNORE(a0)

	// offset = PC_RECORD_NUM_IPID * SIZEOF_PID
	lw	k0, PC_RECORD_NUM_IPID(a0)
#if SIZEOF_PID == 4
	sll	k0, k0, 2
#elif SIZEOF_PID == 8
	sll	k0, k0, 3
#else
#error "SIZEOF_PID is not 4 or 8"
#endif
	// while (1) {
   L_ipid_loop:
		// if (offset == 0) break
		beqz	k0, L_reload_ctr
		// offset -= SIZEOF_PID
		subu	k0, SIZEOF_PID	
		// k1 = *(PC_RECORD_IPID + offset)
#if SIZEOF_PID == 4
		addu	k1, a0, k0
		lw	k1, PC_RECORD_IPID(k1)
#elif SIZEOF_PID == 8
		addu	k1, a0, k0
		ld	k1, PC_RECORD_IPID(k1)
#else
#error "SIZEOF_PID is not 4 or 8"
#endif
		// if (k1 == a1) { set PC_RECORD_IGNORE; beak }
		bne	k1, a1, L_ipid_loop

			li	k1, 1
			sb	k1, PC_RECORD_IGNORE(a0)
	// }
	
    L_reload_ctr:
	/* reload counters and record id part of CID */

	move	a1, zero
	mfpc	k1, 0
	srl	k1, OVF_SHIFT
	beqz	k1, 1f
		lw	k0, PC_RECORD_RELOAD0(a0)
		mtpc	k0, 0
		sync.p
		li	a1, PERF_SAMPLE_CTR0_BIT
    1:

	mfpc	k0, 1
	srl	k0, OVF_SHIFT
	beqz	k0, 2f
		lw	k0, PC_RECORD_RELOAD1(a0)
		mtpc	k0, 1
		sync.p
		/* a1: PERF_ENTRY_CID(a2) */
		or	a1, PERF_SAMPLE_CTR1_BIT
    2:
	/* a1: PERF_ENTRY_CID */
	/* record target part of CID */
	mfps	k0, 0
	move	k1, k0

	/*   event0 part */
	srl	k0, CCR_EVENT0_SHIFT
	and	k0, PERF_SAMPLE_CTR_EVENT_MASK
	or	a1, k0

	/*   event1 part */
	srl	k1, CCR_EVENT1_SHIFT
	and	k1, PERF_SAMPLE_CTR_EVENT_MASK
	sll	k1, PERF_SAMPLE_CTR1_SHIFT
	or	a1, k1

	/* a1: PERF_ENTRY_CID */
	sh	a1, PERF_ENTRY_CID(a2)

	/* check rotate_fast */
	lw	k1, PC_RECORD_MODE(a0)
	subu	k1, PERF_SAMPLE_ROTATE_FAST
	bnez	k1, L_update_index


	    /* check cpu cycle0 fired or cpu cycle1 fired */

#define CID_HALF_MASK (PERF_SAMPLE_CTR_EVENT_MASK | PERF_SAMPLE_CTR0_BIT)
#define CYCLE0_FIRED (PERF_SAMPLE_CTR0_BIT | CCR_CTR0_CPU_CYCLE)
#define CYCLE1_FIRED (PERF_SAMPLE_CTR0_BIT | CCR_CTR1_CPU_CYCLE)

	    move	k0, a1			/* a1: PERF_ENTRY_CID */
	    and		k0, CID_HALF_MASK
	    subu	k0, CYCLE0_FIRED	/* k0 := ! CYCLE0_FIRED */

	    srl		a1, PERF_SAMPLE_CTR1_SHIFT
	    and		a1, CID_HALF_MASK
	    subu	a1, CYCLE1_FIRED	/* k1 := ! CYCLE1_FIRED */

	    /* if !CYCLE0_FIRED and !CYCLE1_FIRED then  L_update_index */
	    and 	k0, a1
	    bnez	k0, L_update_index


		/*  get CUR_PAIR pointer to A1 */
		lw	a1, PC_RECORD_CUR_PAIR(a0)

		/* save counter context */
		mfpc	k0, 0
		sw	k0, CTRPAIR_CUR0(a1)
		mfpc	k1, 1
		sw	k1, CTRPAIR_CUR1(a1)
		
		/* update current ctrpair index (A1:PC_RECORD_CUR_NUM) */
		/* 	and current ctrpair pointer (PC_RECORD_CUR_PAIR) */
		lw	k1, PC_RECORD_CUR_NUM(a0)
		addu	k1, 1
		addu	a1, CTRPAIR_SIZE

		lw	k0, PC_RECORD_NUM_PAIR(a0)
		subu	k0, k0, k1  
		bgtz	k0, 1f
			lw	a1, PC_RECORD_INIT_PAIR(a0)
    			move	k1, zero
	    1:
		sw	a1, PC_RECORD_CUR_PAIR(a0)
		sw	k1, PC_RECORD_CUR_NUM(a0)
	    	
		/* load counter context and restore conuting state */

		/* restore reload vals */
		lw	k0, CTRPAIR_VAL0(a1)
		lw	k1, CTRPAIR_VAL1(a1)
		sw	k0, PC_RECORD_RELOAD0(a0)
		sw	k1, PC_RECORD_RELOAD1(a0)

		/* restore ccr */
		lb	k0, CTRPAIR_EV0(a1)
		lb	k1, CTRPAIR_EV1(a1)
		sll	k0, CCR_EVENT0_SHIFT
		sll	k1, CCR_EVENT1_SHIFT
		or	k0, k1
		li	k1, CCR_TARGET_ALL
		sll	k1, CCR_TARGET0_SHIFT
		or	k0, k1
		li	k1, CCR_TARGET_ALL
		sll	k1, CCR_TARGET1_SHIFT
		or	k0, k1
		mtps	k0, 0
		sync.p

		/* resotre ctr varl context */
		lw	k0, CTRPAIR_CUR0(a1)
		lw	k1, CTRPAIR_CUR1(a1)
		mtpc	k0, 0
		sync.p
		mtpc	k1, 1
		sync.p

		/* restart ccr */
		mfps	k0, 0
		li	k1, 1
		sll	k1, CCR_CTE_SHIFT
		or	k0, k1
		mtps	k0, 0
		sync.p



    L_update_index:
	/* ignore this record ? */
	lb	k1, PC_RECORD_IGNORE(a0)
	bnez	k1, 1f

	/* inc in_index */
        //pc_record.buffer_pool[pool_in].num_entry ++ ;
        lw	a1, PC_RECORD_POOL_IN(a0)	// a1: pool_in
	M_GET_NUM_ENT(a2, a0, a1, k0)
	addu	a2, 1
	M_SET_NUM_ENT(a2, a0, a1, k0)

    1:
	/* restore a0 */
	lq	a2, PC_RECORD_REG_A2(a0)
	lq	a1, PC_RECORD_REG_A1(a0)
	lq	a0, PC_RECORD_REG_A0(a0);

	.local  ret_from_pc_exception
	b ret_from_pc_exception

	.set pop
END(r5900_perf_sample_pc)


#endif /* defined(CONFIG_PERF_DEV) 
	|| defined(CONFIG_PERF_DEV_MODULE) */

       /*
        * simple count-up service for sys_r5900
        *
	*/
LEAF(r5900_perf_count_up)
	.set push
	.set noat

	.extern r5900_upper_ctrs, UPPER_CTRS_SIZE
	/*
	 * offset_xxx values must be sync with sys_r5900.c
	 */

	la	k1, r5900_upper_ctrs
	/*
	 * if counter#0 is overflow then clear  counter#0 
	 */
	mfpc	k0, 0
	srl	k0, OVF_SHIFT
	.set	noreorder
	beqz	k0, 1f
	lw	k0,UPPER_CTRS_CTR0(k1)
	.set	reorder
	addu	k0,1
	sw	k0,UPPER_CTRS_CTR0(k1)
	mtpc	zero, 0
	sync.p
1:	
	/*
	 * if counter#1 is overflow then clear  counter#1 
	 */
	mfpc	k0, 1
	srl	k0, OVF_SHIFT
	.set	noreorder
	beqz	k0, 2f
	lw	k0,UPPER_CTRS_CTR1(k1)
	.set	reorder
	addu	k0,1
	sw	k0,UPPER_CTRS_CTR1(k1)
	mtpc	zero, 1
	sync.p
2:	

ret_from_pc_exception:
	/*
	 * resoter k0, k1
	 */

	/* restore  MS64B of k0 */
	la	k0, save_kregs;
	lq	k0, SAVE_K0_OFFSET(k0);	

	/* restore LS32B of k0 */
	mfc0	k0, CP0_TAGLO
	sync.p
	li	k1, 0xffffffff
	and	k0, k1

	/* restore next LS32B of k0 */
	mfc0	k1, CP0_TAGHI
	.set push
	.set mips3
	dsll32	k1, k1, 0
	.set pop
	or	k0, k1

	/* restore k1 */
	la	k1, save_kregs;
	lq	k1, SAVE_K1_OFFSET(k1);
	sync.p
	eret
	.set pop

END(r5900_perf_count_up)

