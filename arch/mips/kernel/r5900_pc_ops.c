/*
 * r5900_pc_ops.c - r5900 spcific pc sampling serivce 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <linux/malloc.h>	/* kmalloc */
#include <linux/sched.h>	/* jiffies */
#include <linux/param.h>	/* HZ */
#include <linux/errno.h>	/* error codes */
#include <linux/perf_dev.h>

#include <asm/uaccess.h>
#include <asm/perf_counter.h>

#include "r5900_pc_ops.h"

/*========================================================================*/
/* variables uesd as interface to other modules */
/*========================================================================*/

struct wait_queue *perf_sample_queue = NULL;


#define INC_POOL_INDEX(x)  ((x+1) >= (pc_record.pool_num) ? 0 : (x+1))

struct pc_record pc_record = { 
	pool_in:0,pool_out:0,
	lost:0,
	num_of_ctrpair:0,
	cur_ctrpair_num:0,
	sample_mode:PERF_SAMPLE_ROTATE_FAST,
};

extern u8 r5900_perf_mode;
extern struct perf_counter_operations *perf_counter_ops;


/*========================================================================*/
/*========================================================================*/

static u8  initialized=0;

/*========================================================================*/
/* counter set table */
/*========================================================================*/

extern unsigned int r5900_get_cpu_clock(void);
#define CNT_1MSEC	(r5900_get_cpu_clock()*1000)

#define LD_VAL(x)		PERF_SAMPLE_COUNT_TO_LOADVAL(x)
#define COUNT_VAL(x)		PERF_SAMPLE_LOADVAL_TO_COUNT(x)

#define CNT_CPU_CYCLE_FAST	 100	/* usec */
#define CNT_CPU_CYCLE_MID	 500 	/* usec */
#define CNT_CPU_CYCLE_SLOW	1000	/* usec */

/* for SLOW (POLLING) ROTATION MODE only */
#define DECL_CTRSET6(PARAM_NAME, PARAM_CYCLE_VAL) \
static struct  ctrpair_struct PARAM_NAME [] = { \
  { event0:CCR_CTR0_CPU_CYCLE,	val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_INST_COMP,	val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_ICACHE_MISS,	val0:LD_VAL(1000), \
    event1:CCR_CTR1_DCACHE_MISS,	val1:LD_VAL(1000), } \
  , \
  { event0:CCR_CTR0_ADDR_BUS_BUSY,val0:LD_VAL(10000), \
    event1:CCR_CTR1_DATA_BUS_BUSY,val1:LD_VAL(10000), } \
}


/* for FAST (CPU_CYCLE EVENT) ROTATION MODE */

#define DECL_CTRSET_CYCLE4(PARAM_NAME, PARAM_CYCLE_VAL) \
static struct  ctrpair_struct PARAM_NAME [] = { \
  { event0:CCR_CTR0_ICACHE_MISS,	val0:LD_VAL(1000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_DCACHE_MISS,	val1:LD_VAL(1000), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_DATA_BUS_BUSY,	val1:LD_VAL(10000), } \
  , \
  { event0:CCR_CTR0_ADDR_BUS_BUSY,	val0:LD_VAL(10000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
}

#define DECL_CTRSET_CYCLE12(PARAM_NAME, PARAM_CYCLE_VAL) \
static struct  ctrpair_struct PARAM_NAME [] = { \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
   event1:CCR_CTR1_DTLB_MISS,		val1:LD_VAL(10000), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_ITLB_MISS,		val1:LD_VAL(1000), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_DCACHE_MISS,		val1:LD_VAL(1000), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_DATA_BUS_BUSY,	val1:LD_VAL(10000), } \
  , \
  { event0:CCR_CTR0_TLB_MISS,		val0:LD_VAL(1000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_ICACHE_MISS,		val0:LD_VAL(1000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_INST_COMP,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_BTAC_MISS,		val0:LD_VAL(10000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_NONBLOCK_LOAD,	val0:LD_VAL(10000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_ADDR_BUS_BUSY,	val0:LD_VAL(10000), \
    event1:CCR_CTR1_CPU_CYCLE,		val1:(PARAM_CYCLE_VAL), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_WBB_BURST_UNAVAIL,	val1:LD_VAL(1000), } \
  , \
  { event0:CCR_CTR0_CPU_CYCLE,		val0:(PARAM_CYCLE_VAL), \
    event1:CCR_CTR1_WBB_BURST_FULL,	val1:LD_VAL(1000), } \
}

DECL_CTRSET6(ctrset6_slow, CNT_CPU_CYCLE_SLOW);
DECL_CTRSET6(ctrset6_med, CNT_CPU_CYCLE_MID);

DECL_CTRSET_CYCLE4(ctrset4_cycle_slow, CNT_CPU_CYCLE_SLOW);
DECL_CTRSET_CYCLE4(ctrset4_cycle_med, CNT_CPU_CYCLE_MID);
DECL_CTRSET_CYCLE4(ctrset4_cycle_fast, CNT_CPU_CYCLE_FAST);

DECL_CTRSET_CYCLE12(ctrset12_cycle_slow, CNT_CPU_CYCLE_SLOW);
DECL_CTRSET_CYCLE12(ctrset12_cycle_med, CNT_CPU_CYCLE_MID);
DECL_CTRSET_CYCLE12(ctrset12_cycle_fast, CNT_CPU_CYCLE_FAST);


struct ctrset_catalog {
	int	num_pair;
	struct ctrpair_struct *ctrset_ptr;
	char short_descr[ PERF_DEV_DESCR_STR_LEN];
};

struct ctrset_catalog pc_sample_slow_catalog[] = {
    {
	sizeof(ctrset6_slow)/sizeof(ctrset6_slow[0])
		, ctrset6_slow
		, "Apply 6 measures with low sampling rate."
    }
    ,
    {
	sizeof(ctrset6_med)/sizeof(ctrset6_med[0])
		, ctrset6_med
		, "Apply 6 measures with medium sampling rate."
    }
};

struct ctrset_catalog pc_sample_fast_catalog[] = {
    {
	sizeof(ctrset4_cycle_slow)/sizeof(ctrset4_cycle_slow[0])
		, ctrset4_cycle_slow
		, "Apply 4+1 measures with low sampling rate."
    }
    ,
    {
	sizeof(ctrset4_cycle_med)/sizeof(ctrset4_cycle_med[0])
		, ctrset4_cycle_med
		, "Apply 4+1 measures with medium sampling rate."
    }
    ,
    {
	sizeof(ctrset4_cycle_fast)/sizeof(ctrset4_cycle_fast[0])
		, ctrset4_cycle_fast
		, "Apply 4+1 measures with high sampling rate."
    }
    ,
    {
	sizeof(ctrset12_cycle_slow)/sizeof(ctrset12_cycle_slow[0])
		, ctrset12_cycle_slow
		, "Apply 12+1 measures with low sampling rate."
    }
    ,
    {
	sizeof(ctrset12_cycle_med)/sizeof(ctrset12_cycle_med[0])
		, ctrset12_cycle_med
		, "Apply 12+1 measures with medium sampling rate."
    }
    ,
    {
	sizeof(ctrset12_cycle_fast)/sizeof(ctrset12_cycle_fast[0])
		, ctrset12_cycle_fast
		, "Apply 12+1 measures with high sampling rate."
    }
};


struct fixed_catalog_struct { 
	struct ctrpair_struct ctrpair;
	char short_descr[PERF_DEV_DESCR_STR_LEN];
} pc_sample_fixed_catalog[] = {
  {
    { event0:CCR_CTR0_CPU_CYCLE,		val0:1000/*usec*/,
      event1:CCR_CTR1_DCACHE_MISS,	val1:LD_VAL(500),}
	, "Apply 2 measures with slow sampling rate."
	  " (CPU_CYCLE and DCACHE_MISS)"
  }
  ,
  {
    {  event0:CCR_CTR0_ICACHE_MISS,	val0:LD_VAL(500),
       event1:CCR_CTR1_CPU_CYCLE,		val1:1000/*usec*/,}
	, "Apply 2 measures with slow sampling rate."
	  " (CPU_CYCLE and ICACHE_MISS)"
  }
  ,
  {
    { event0:CCR_CTR0_CPU_CYCLE,		val0:1000/*usec*/,
      event1:CCR_CTR1_DATA_BUS_BUSY,	val1:LD_VAL(10000), } 
	, "Apply 2 measures with slow sampling rate."
	  " (CPU_CYCLE and DATA_BUS_BUSY)"
  }
  ,
  {
    { event0:CCR_CTR0_ADDR_BUS_BUSY,	val0:LD_VAL(10000), 
       event1:CCR_CTR1_CPU_CYCLE,		val1:1000/*usec*/,}
	, "Apply 2 measures with slow sampling rate."
	  " (CPU_CYCLE and ADDR_BUS_BUSY)"
  }
  ,
  {
    {  event0:CCR_CTR0_ICACHE_MISS,	val0:LD_VAL(500),
       event1:CCR_CTR1_DCACHE_MISS,	val1:LD_VAL(500),}
	, "Apply 2 measures with slow sampling rate."
	  " (ICACHE_MISS and DCACHE_MISS)"
  }
  ,
  {
    { event0:CCR_CTR0_ADDR_BUS_BUSY,	val0:LD_VAL(10000), 
      event1:CCR_CTR1_DATA_BUS_BUSY,	val1:LD_VAL(10000), } 
	, "Apply 2 measures with slow sampling rate."
	  " (ADDR_BUS_BUSY and DATA_BUS_BUSY)"
  }
};

static 
void init_ctrpair_val (struct ctrpair_struct *ctrpair)
{
	if ((ctrpair->event0 == CCR_CTR0_CPU_CYCLE)  
		||(ctrpair->event0 == CCR_CTR0_INST_COMP))
		ctrpair->val0 = LD_VAL(ctrpair->val0*(CNT_1MSEC/1000));
	if ((ctrpair->event1 == CCR_CTR1_CPU_CYCLE) 
		||(ctrpair->event1 == CCR_CTR1_INST_COMP))
		ctrpair->val1 = LD_VAL(ctrpair->val1*(CNT_1MSEC/1000));
}

static
void init_table(void)
{
	int n_fixed;
	int n_fast;
	int n_slow;
	int k;
	int i;
	int j;

	struct {
		int items_in_catalog;
		struct ctrset_catalog *catalog;
	}  catalogs[] =  {
		{ (sizeof(pc_sample_fast_catalog)/
				sizeof(pc_sample_fast_catalog[0])),
		  pc_sample_fast_catalog },
		{ (sizeof(pc_sample_slow_catalog)/
				sizeof(pc_sample_slow_catalog[0])),
		  pc_sample_slow_catalog }
	};

	n_fixed = sizeof(pc_sample_fixed_catalog)/
				sizeof(pc_sample_fixed_catalog[0]);
	for(i=0;i<n_fixed; i++) {
		init_ctrpair_val(&(pc_sample_fixed_catalog[i].ctrpair)); 
	}

	for (k=0; k < (sizeof(catalogs)/sizeof(catalogs[0])); k++)
		for(i=0;i<catalogs[k].items_in_catalog; i++) {
			struct ctrset_catalog *catalog;
			catalog = &(catalogs[k].catalog[i]);
			for (j=0;j<catalog->num_pair; j++) {
				init_ctrpair_val(&(catalog->ctrset_ptr[j]));
			}
		}

}

/*========================================================================*/
/* helpers for rotatetion  */
/*========================================================================*/

static void set_sample_dev (u32 ctr0_ev, u32 ctr0_val, 
				u32 ctr1_ev, u32 ctr1_val ); /* forward */

static
void init_ctrset(struct ctrset_catalog *catalog) 
{
	int i;
	
	for (i=0; i<catalog->num_pair; i++) {
		catalog->ctrset_ptr[i].current0 = 
				catalog->ctrset_ptr[i].val0 ;
		catalog->ctrset_ptr[i].current1 = 
				catalog->ctrset_ptr[i].val1 ;
	}

	pc_record.num_of_ctrpair=catalog->num_pair;
	pc_record.cur_ctrpair_num=0;
	pc_record.init_ctrpair=catalog->ctrset_ptr;
	pc_record.cur_ctrpair=pc_record.init_ctrpair;


	set_sample_dev(
		pc_record.cur_ctrpair->event0,
		pc_record.cur_ctrpair->val0,
		pc_record.cur_ctrpair->event1,
		pc_record.cur_ctrpair->val1
	);

}

static
void switch_ctrpair(void) 
{
	u32 ccr;
	int cte;

	/* get counter enable flag */
	cte = CCR_GET_CTE( get_CCR() );
	/* stop counter */
	set_CCR(0);

	/* save counter context */
	pc_record.cur_ctrpair->current0 = get_CTR0();
	pc_record.cur_ctrpair->current1 = get_CTR1();
	
	/* get next index */
	pc_record.cur_ctrpair_num++; 
	pc_record.cur_ctrpair ++;
	if (pc_record.cur_ctrpair_num >= pc_record.num_of_ctrpair) {
		pc_record.cur_ctrpair_num=0;
		pc_record.cur_ctrpair = pc_record.init_ctrpair;
	}

	/* load counter context and restore conuting state */

	pc_record.reload0=pc_record.cur_ctrpair->val0;
	pc_record.reload1=pc_record.cur_ctrpair->val1;

	set_CTR0(pc_record.cur_ctrpair->current0);
	set_CTR1(pc_record.cur_ctrpair->current1);

	ccr = CCR_SET_CTE(0,cte);
	ccr = CCR_SET_CTR0_EVENT (ccr, pc_record.cur_ctrpair->event0);
	ccr = CCR_SET_CTR1_EVENT (ccr, pc_record.cur_ctrpair->event1);

	ccr = CCR_SET_CTR0_TARGET (ccr, CCR_TARGET_ALL);
	ccr = CCR_SET_CTR1_TARGET (ccr, CCR_TARGET_ALL);

	set_CCR(ccr);

}

static int init_sample_dev(int mode, int selector); /* forward */
static int realloc_buffer(int num); /* forward */
static int realloc_buffer_for_mode(int mode); /* forward */ 

static
int set_sample_mode(struct perf_dev_sample_mode *mode_spec)
{
	int i;
	int current_buff;

	current_buff=realloc_buffer_for_mode(mode_spec->sample_mode);
	if (current_buff<0)
		return current_buff;
	i = init_sample_dev(mode_spec->sample_mode, mode_spec->selector);
	if ( i<0 ) {
		realloc_buffer(current_buff);
		return i;
	}

	pc_record.sample_mode = mode_spec->sample_mode;
	pc_record.selector = mode_spec->selector;
	return 0;
}

/*========================================================================*/
/* Polling functions */
/*========================================================================*/

static struct timer_list  polling_timer;

static u32 poll_cnt;
static u8 poll_started = 0;

static struct fasync_struct **async_queue_p=NULL;


static void do_poll(unsigned long tick); /* forward */
static void __start_polling(unsigned long tick)
{

	init_timer(&polling_timer);
	polling_timer.function=do_poll;
	polling_timer.data=tick;
	polling_timer.expires=jiffies+tick;

	add_timer(&polling_timer);
}

static int is_empty(int cpuid); /* forward */

static void do_poll(unsigned long tick)
{

	if ( pc_record.sample_mode == PERF_SAMPLE_ROTATE_SLOW)
		switch_ctrpair();

	if ( poll_started && tick > 0) {
		__start_polling(tick);
	}
	
	poll_cnt++;
	if (!is_empty(0/*dummy*/)) {
		wake_up_interruptible(&perf_sample_queue);
		if (async_queue_p && *async_queue_p) {
			kill_fasync(*async_queue_p , SIGIO);
		}
	}
}

static void start_polling(unsigned long tick)
{
	u32	flags;
	save_flags(flags);
	cli();
	if (!poll_started) {
		poll_started=1;
		poll_cnt=0;
		__start_polling(tick);
	}
	restore_flags(flags);
}

static void stop_polling(void)
{
	u32	flags;
	save_flags(flags);
	cli();
	if (poll_started) {
		poll_started = 0;
		del_timer(&polling_timer);
	}
	restore_flags(flags);
}


/*========================================================================*/
/* sample_buffer access methods */
/*========================================================================*/

inline
static int __is_empty(void) 
{
	return pc_record.pool_in == pc_record.pool_out &&
		!pc_record.buffer_pool[pc_record.pool_in].num_entry;
}

inline static u32 enter_cs(void)
{
	/* pause perf counter */
	u32 ccr, ret;
	ccr = get_CCR();
	ret = ccr;
	ccr = CCR_SET_CTE (ccr,0);
	set_CCR(ccr);
	return ret;
}

inline static void leave_cs(u32 val)
{
	/* continue perf counter */
	set_CCR(val);
}


/* Recored sample data, return zero if overflow (for internal) */
inline static 
int __record_sample (struct perf_sample_entry *p_sample)
{
	u32 pool_in = pc_record.pool_in;
	u32 num_entry = pc_record.buffer_pool[pool_in].num_entry;

	if (num_entry ==  pc_record.max_entry) {
		u32 pool_out = pc_record.pool_out;
		u32 next_pool_in = INC_POOL_INDEX(pool_in);
		// try get next buffer
		if ( next_pool_in == pool_out) {
			// failed, no buffer
			pc_record.lost ++;
			return 0;
		}
		pool_in =  next_pool_in;
		pc_record.pool_in = pool_in; 
		num_entry = 0;
		pc_record.buffer_pool[pool_in].num_entry = num_entry;

	}
	(* pc_record.buffer_pool[pool_in].p_buffer )[ num_entry ] = *p_sample;
	num_entry ++;
	pc_record.buffer_pool[pool_in].num_entry = num_entry ;

	return 1;
}

/* Recored sample data, return zero if overflow */
static 
int record_sample (int cpuid, struct perf_sample_entry *p_sample)
{
	int r;
	u32 cs;
	cs = enter_cs();
	r=__record_sample(p_sample);
	leave_cs(cs);
	return r;
}

/* Retrieve sample data buffer and num of data, return zero if empty */
static int
retrieve_sample_buffer (int cpuid, 
	struct perf_sample_entry (** p_entry)[], void **key)
{
	int num;
	u32 cs;


	cs = enter_cs();
	if ( __is_empty() ) {
		num = 0;
		*key = (void *)-1;
		goto out;
	}
	num=pc_record.buffer_pool[pc_record.pool_out].num_entry ;
	*p_entry = pc_record.buffer_pool[pc_record.pool_out].p_buffer;

	if  (pc_record.pool_out == pc_record.pool_in ) {
		pc_record.pool_in = INC_POOL_INDEX(pc_record.pool_in);
		pc_record.buffer_pool[pc_record.pool_in].num_entry = 0;
	}
	*key = (void *)pc_record.pool_out;

    out:
	leave_cs(cs);
	return num;
}

static int
release_sample_buffer (int cpuid, void *key)
{
	if (key != (void *)pc_record.pool_out) {
		printk(KERN_ERR "r5900_pc_ops.c: release_sample_buffer mismatch\n");
		return 0;
	}
	pc_record.pool_out = INC_POOL_INDEX(pc_record.pool_out);
	return 1;
}

static void reset_sample(int cpuid)
{
	pc_record.pool_in = 0;
	pc_record.pool_out =0;
	pc_record.lost = 0;
	pc_record.buffer_pool[pc_record.pool_in].num_entry =0;
}

static int is_empty(int cpuid)
{
	int r;
	u32 cs;

	cs = enter_cs();
	r = __is_empty();
	leave_cs(cs);
	return r;
}

/*========================================================================*/
/* /dev service  */
/*========================================================================*/

static int sample_refcnt = 0;
static int polling_msec;

static void * get_info(char *buff, int size)
{
#define STR_MARGIN	80
	int	out;
	int	i;
	void	*ret;
	u32	cs;
	u32	ccr;

	buff[0]='\0';

	ccr = get_CCR();
	cs = enter_cs();

#define DEBGU_MSG_UPDATE \
	size -= out; buff += out; if (size <= STR_MARGIN) { ret = 0; goto out; }

	// Statistics
	out = sprintf(buff, "*Statistics\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  lost    :%8d", pc_record.lost);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  poll_cnt:%8d\n", poll_cnt);
	DEBGU_MSG_UPDATE

	// hardware regs.
	out = sprintf(buff, "*Perf Counter Regs.\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  ccr   :%8.8x (ev0:0x%2.2x    ev1:0x%2.2x)\n",
		ccr,
		CCR_GET_CTR0_EVENT(ccr) , CCR_GET_CTR1_EVENT(ccr));
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  ctr0  :%8.8x  ",get_CTR0());
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  ctr1  :%8.8x  ",get_CTR1());
	DEBGU_MSG_UPDATE
	if (!initialized) {
		out = sprintf(buff, "\n");
		DEBGU_MSG_UPDATE
		ret = buff;
		goto out;
	}
	out = sprintf(buff, "  reload0 :%8.8x  ", pc_record.reload0);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  reload1 :%8.8x\n", pc_record.reload1);
	DEBGU_MSG_UPDATE

	// ctrpair info.
	out = sprintf(buff, "*Counter pair info.\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  cur_num :%d  ", pc_record.cur_ctrpair_num);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  init_ptr:%8.8lx  ", 
		(unsigned long) pc_record.init_ctrpair);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  cur_ptr :%8.8lx\n",
		(unsigned long)  pc_record.cur_ctrpair);
	DEBGU_MSG_UPDATE

	out = sprintf(buff, "  ev0:0x%2.2x      ev1:0x%2.2x\n", 
		(pc_record.cur_ctrpair)->event0,
		(pc_record.cur_ctrpair)->event1);
	DEBGU_MSG_UPDATE

	out = sprintf(buff, "  val0:  %8.8x  ", 
		(pc_record.cur_ctrpair)->val0);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  val1:  %8.8x  ", 
		(pc_record.cur_ctrpair)->val1);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  cur0:  %8.8x  ", 
			(pc_record.cur_ctrpair)->current0);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  cur1:  %8.8x\n", 
			(pc_record.cur_ctrpair)->current1);
	DEBGU_MSG_UPDATE

	for (i=0;i<pc_record.num_of_ctrpair;i++) {
		out = sprintf(buff, 
			"  [%2d] (ev#0:%2x)cur/val:%8.8x/%8.8x"
			"  (ev#1:%2x)cur/val:%8.8x/%8.8x\n",
			i,
			(pc_record.init_ctrpair)[i].event0,
			(pc_record.init_ctrpair)[i].current0,
			(pc_record.init_ctrpair)[i].val0,
			(pc_record.init_ctrpair)[i].event1,
			(pc_record.init_ctrpair)[i].current1,
			(pc_record.init_ctrpair)[i].val1);
		DEBGU_MSG_UPDATE
	}

	// current buffer info.
	out = sprintf(buff, "*Current buffer info.\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  max_ent :%4d    ", pc_record.max_entry);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  base    :%8.8x  ", pc_record.debug_base);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  ptr     :%8.8x\n", pc_record.debug_ptr );
	DEBGU_MSG_UPDATE

	// buffer pool info.
	out = sprintf(buff, "*Buffer pool info.\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  pool_num:%4d    ", pc_record.pool_num);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  pool_in :%4d    ", pc_record.pool_in);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  pool_out:%4d\n", pc_record.pool_out);
	DEBGU_MSG_UPDATE

	for (i = 0; i < pc_record.pool_num; i ++) {
		out = sprintf(buff, "  [%3d]:%3d %8.8lx", 
			i, pc_record.buffer_pool[i].num_entry,
			(unsigned long)pc_record.buffer_pool[i].p_buffer);
		DEBGU_MSG_UPDATE
		if ((i&1) == 0x1) {
			out = sprintf(buff, "\n");
		} else {
			out = sprintf(buff, "    ");
		}
		DEBGU_MSG_UPDATE
	}

	ret = buff;

out:
	leave_cs(cs);

	return ret;
}

void show_counter_pairs(void)
{
	int i;
	for (i=0;i<pc_record.num_of_ctrpair;i++) {
		printk(KERN_ERR "%d\t  ev0   :%2x\t", i,
			(pc_record.init_ctrpair)[i].event0);
		printk("  ev1   :%2x\n", 
			(pc_record.init_ctrpair)[i].event1);
		printk(KERN_ERR "\t  val0  :%8.8x\t", 
			(pc_record.init_ctrpair)[i].val0);
		printk("  val1  :%8.8x\n", 
			(pc_record.init_ctrpair)[i].val1);
		printk(KERN_ERR "\t  cur0  :%8.8x\t", 
			(pc_record.init_ctrpair)[i].current0);
		printk("  cur1  :%8.8x\n", 
			(pc_record.init_ctrpair)[i].current1);
	}
}

static void
set_sample_dev( u32 ctr0_ev, u32 ctr0_val, u32 ctr1_ev, u32 ctr1_val )
{
	u32 ccr = 0;

	set_CCR(0);

	ccr = CCR_SET_CTR0_EVENT (ccr,ctr0_ev);
	ccr = CCR_SET_CTR1_EVENT (ccr,ctr1_ev);


	pc_record.reload0=ctr0_val;
	pc_record.reload1=ctr1_val;
	set_CTR0(ctr0_val);
	set_CTR1(ctr1_val);

	ccr = CCR_SET_CTR0_TARGET (ccr,CCR_TARGET_ALL);
	ccr = CCR_SET_CTR1_TARGET (ccr,CCR_TARGET_ALL);
	set_CCR(ccr);

}

static int init_sample_dev(int sample_mode, int selector) 
{

	if (sample_mode == PERF_SAMPLE_FIXED ) {
		if ( selector >= (sizeof(pc_sample_fixed_catalog)/
					sizeof(pc_sample_fixed_catalog[0])))
			return -EINVAL;

		set_sample_dev 
		    ( pc_sample_fixed_catalog[selector].ctrpair.event0,
			pc_sample_fixed_catalog[selector].ctrpair.val0,
			pc_sample_fixed_catalog[selector].ctrpair.event1,
			pc_sample_fixed_catalog[selector].ctrpair.val1 );

	} else if (sample_mode == PERF_SAMPLE_ROTATE_FAST ) {
		if ( selector >= (sizeof(pc_sample_fast_catalog)/
					sizeof(pc_sample_fast_catalog[0])))
			return -EINVAL;

		init_ctrset(&pc_sample_fast_catalog[selector]);

	} else if (sample_mode == PERF_SAMPLE_ROTATE_SLOW ) {
		if ( selector >= (sizeof(pc_sample_slow_catalog)/
					sizeof(pc_sample_slow_catalog[0])))
			return -EINVAL;

		init_ctrset(&pc_sample_slow_catalog[selector]);

	} else {
		/* can't be occuered */
		return -EINVAL;
	}
	polling_msec = 10; /* msec */
	return 0;

}

/* return unified event number, if event has alias,
        otherwise return -1 */
static
int event_is_alias(int i)
{
	if (i>=PERF_DEV_NUM_CTR_EVENT_PER_CTR) {
		i -= PERF_DEV_NUM_CTR_EVENT_PER_CTR;
		switch (i) {
			case CCR_CTR1_CPU_CYCLE:
				return CCR_CTR0_CPU_CYCLE;
			case CCR_CTR1_INST_COMP:
				return CCR_CTR0_INST_COMP;
		}
		return -1;
	}

	switch (i) {
		case CCR_CTR0_CPU_CYCLE:
			return CCR_CTR0_CPU_CYCLE;
		case CCR_CTR0_INST_COMP:
			return CCR_CTR0_INST_COMP;
	}
	return -1;
}

inline
static long  msec_to_tick(int msec)	
{
	long  tick = (msec*HZ/1000) ;
	if (tick == 0) tick = 1;
	return tick;
}

static void set_sample_counter_info ( 
	struct perf_sample_counter_info *dst_info, 
	struct ctrpair_struct *ctrpair)
{
	int evindex;
	int same_as;
		
	evindex = PERF_SAMPLE_CTR0_TO_INX(ctrpair->event0);
	if ( ctrpair->event0 !=  CCR_CTR0_NO_EVENT ) {
		dst_info[evindex].same_as = evindex;
		same_as = event_is_alias(evindex);
		if (same_as != -1){
			dst_info[evindex].same_as = same_as;
			evindex = same_as;
			dst_info[evindex].same_as = evindex;
		}
		if ( dst_info[evindex].avail >0 ) {
			if (dst_info[evindex].count != 
				COUNT_VAL(ctrpair->val0))
			printk(KERN_ERR 
				"r5900_pc_ops.c:invaild counter %d val0\n", 
					evindex);
		} else {
			dst_info[evindex].count = COUNT_VAL(ctrpair->val0);
		}
		dst_info[evindex].avail ++;
	}

	evindex = PERF_SAMPLE_CTR1_TO_INX(ctrpair->event1);
	if ( ctrpair->event1 !=  CCR_CTR1_NO_EVENT ) {
		dst_info[evindex].same_as = evindex;
		same_as = event_is_alias(evindex);
		if (same_as != -1) {
			dst_info[evindex].same_as = same_as;
			evindex = same_as;
			dst_info[evindex].same_as = evindex;
		}
		if ( dst_info[evindex].avail >0 ) {
			if (dst_info[evindex].count != 
				COUNT_VAL(ctrpair->val1))
			printk(KERN_ERR 
				"r5900_pc_ops.c:invaild counter %d val1\n", 
					evindex);
		} else {
			dst_info[evindex].count = COUNT_VAL(ctrpair->val1);
		}
		dst_info[evindex].avail ++;
	}
}

static int get_sample_mode_info(struct  perf_dev_sample_mode_info *info) 
{
	struct  ctrpair_struct *ctrpair;
	int n,i;
	char *msg;

	memset(info, 0, sizeof(info[0]));

	info->sample_mode = pc_record.sample_mode;
	info->selector = pc_record.selector;

	if (info->sample_mode == PERF_SAMPLE_FIXED ) {
		 info->num_selection = sizeof(pc_sample_fixed_catalog) 
		 			/ sizeof(pc_sample_fixed_catalog[0]);
		n = 1;
		ctrpair = &pc_sample_fixed_catalog[info->selector].ctrpair;
		msg	= pc_sample_fixed_catalog[info->selector].short_descr;

	} else if (info->sample_mode == PERF_SAMPLE_ROTATE_FAST) {
		info->num_selection = sizeof(pc_sample_fast_catalog) 
		 			/ sizeof(pc_sample_fast_catalog[0]);
		n = pc_sample_fast_catalog[info->selector].num_pair;
		ctrpair = pc_sample_fast_catalog[info->selector].ctrset_ptr;
		msg	= pc_sample_fast_catalog[info->selector].short_descr;

	} else if (info->sample_mode == PERF_SAMPLE_ROTATE_SLOW ) {
		info->num_selection = sizeof(pc_sample_slow_catalog) 
		 			/ sizeof(pc_sample_slow_catalog[0]);
		n = pc_sample_slow_catalog[info->selector].num_pair;
		ctrpair = pc_sample_slow_catalog[info->selector].ctrset_ptr;
		msg	= pc_sample_slow_catalog[info->selector].short_descr;
	} else {
		/* can't be occuered */
		return -EINVAL;
	}
	info->polling_period = msec_to_tick(polling_msec);

	strncpy(info->short_descr, msg, PERF_DEV_DESCR_STR_LEN-1);
	info->num_period = n;
	for (i=0; i<n; i++)
		set_sample_counter_info (info->sample_counter, &ctrpair[i]);

	/* sanity check for PERF_SAMPLE_ROTATE_FAST */
	if (info->sample_mode == PERF_SAMPLE_ROTATE_FAST) {
		int evindex;
		int same_as;
		evindex = PERF_SAMPLE_CTR0_TO_INX(CCR_CTR0_CPU_CYCLE);
		same_as = event_is_alias(evindex);
		if (same_as != -1 ) {
			evindex = same_as;
		}
		if ( info->sample_counter[evindex].avail != info->num_period )
			printk(KERN_ERR 
				"r5900_pc_ops.c:invaild cpu cycle counter.\n");
	}
	

	return 0;
}


static int get_sample_stats(struct  perf_dev_sample_stats *stats) 
{
	stats->num_losts = pc_record.lost;
	stats->num_polls = poll_cnt;
	return 0;
}


static
int realloc_buffer(int num) 
{
	static volatile int refcnt=0;
	int fail = 0;
	int i;
	int cur_num = pc_record.pool_num;

	if (num == cur_num)
		return 0;

	if (num < PC_SAMPLE_DEFAULT_NUM_POOL ||
		num > PC_SAMPLE_NUM_POOL )
		return -EINVAL;

	if (refcnt) return -EBUSY;
	refcnt ++;

	if (cur_num > num)  {
		for (i=num; i<cur_num; i++) {
			free_page((unsigned long)
				pc_record.buffer_pool[i].p_buffer);
			pc_record.buffer_pool[i].p_buffer=0;
		}
		pc_record.pool_num = num;
		return 0;
	} 

	/* (cur_num < num)  */

	for (i=cur_num; i<num; i++) {
		pc_record.buffer_pool[i].p_buffer = 
			(struct perf_sample_entry (*)[])
				__get_free_page (GFP_KERNEL);
		pc_record.buffer_pool[i].num_entry = 0;
		if (!pc_record.buffer_pool[i].p_buffer) {
			fail = 1;
			break;
		}
	}
	if (fail)  {
		for (i=cur_num; i<num; i++) {
			if (!pc_record.buffer_pool[i].p_buffer) 
				break;
			free_page((unsigned long)
				pc_record.buffer_pool[i].p_buffer);
			pc_record.buffer_pool[i].p_buffer=0;
		}
	} else {
		pc_record.pool_num = num;
	}
	refcnt --;
	if (fail)
		return -ENOMEM;
	return 0;
}

static
int realloc_buffer_for_mode(int mode) 
{
	int i;
	int cur_num = pc_record.pool_num;

	if (mode == PERF_SAMPLE_ROTATE_FAST)
		i=realloc_buffer(PC_SAMPLE_NUM_POOL);
	else
		i=realloc_buffer(PC_SAMPLE_DEFAULT_NUM_POOL);
	if (i<0) return i;
	return cur_num;
}

static
int alloc_buffer(int num) 
{
	int fail = 0;
	int i;

	for (i=0; i<PC_SAMPLE_NUM_POOL; i++) {
		pc_record.buffer_pool[i].p_buffer =0;
	}
	for (i=0; i<num; i++) {
		pc_record.buffer_pool[i].p_buffer = 
			(struct perf_sample_entry (*)[])
				__get_free_page (GFP_KERNEL);
		pc_record.buffer_pool[i].num_entry = 0;
		if (!pc_record.buffer_pool[i].p_buffer) {
			fail = 1;
			break;
		}
	}
	if (fail)  {
		for (i=0; i<num; i++)  {
			if (!pc_record.buffer_pool[i].p_buffer) 
				break;
			free_page((unsigned long)
				pc_record.buffer_pool[i].p_buffer);
			pc_record.buffer_pool[i].p_buffer=0;
		}

		printk(KERN_ERR 
			"r5900_pc_ops.c:"
			"error on allocating buffer:\n");
		return -ENOMEM;
	}

	pc_record.pool_in = 0;
	pc_record.pool_out =  0;
	pc_record.pool_num = num;
	pc_record.max_entry = PAGE_SIZE/sizeof(struct perf_sample_entry) ;

	return 0;
}

static
void free_buffer(void) 
{
	int i;
	for (i=0; i<PC_SAMPLE_NUM_POOL; i++) 
		if (pc_record.buffer_pool[i].p_buffer) {
			free_page((unsigned long)
				pc_record.buffer_pool[i].p_buffer);
			pc_record.buffer_pool[i].p_buffer=0;
		}

}

static long control_sample(int cpuid, unsigned int cmd, void *arg)
{
	extern void r5900_perf_iocgetinfo(struct perf_dev_info *);
	struct perf_dev_sample_ignore_pids ipids;
	struct perf_dev_internal_cmd *cmd_pkt;
	struct perf_dev_internal_cmd my_cmd_pkt;
	u32 ccr = 0;
	struct  perf_dev_sample_mode_info info;
	struct  perf_dev_sample_stats stats;
	struct perf_dev_info devinfo;
	int i;
	int sub_cmd;

	switch(cmd) {
	  case PERF_IOCGETINFO:
		r5900_perf_iocgetinfo(&devinfo);
		i = copy_to_user(arg, &devinfo, sizeof(devinfo));
		return i;
	  	break;

	  case PERF_IOCSTART:
		/* sanity check */
		if (!sample_refcnt)
			return -EINVAL;

		reset_sample(cpuid);
		start_polling(msec_to_tick(polling_msec));

		/* start perf counter */
		ccr = get_CCR();
		ccr = CCR_SET_CTE (ccr,1);
		set_CCR(ccr);
		return 0;
		break;

	  case PERF_IOCSTOP:
		ccr = get_CCR();
		ccr = CCR_SET_CTE (ccr,0);
		set_CCR(ccr);

		stop_polling();
		return 0;
		break;

	  case PERF_IOCGETSAMPLESTATS:
		i = get_sample_stats(&stats);
		if (i<0)
			return i;
		i = copy_to_user(arg, &stats, sizeof(stats));
		return i;
	  	break;

	  case PERF_IOCGETSAMPLEINFO:
		i = get_sample_mode_info(&info);
		if (i<0)
			return i;
		i = copy_to_user(arg, &info, sizeof(info));
		return i;
	  	break;

	  case PERF_IOCIGNORESAMPLEPIDS:
		i = copy_from_user(&ipids,arg,sizeof(ipids));
		if (i<0) 
			return -EINVAL;

		/* Get operations */
		if (ipids.cmd == PERF_SAMPLE_IPID_GET_NUM ) {
			ipids.spec.list.current = pc_record.num_ipid;
			ipids.spec.list.max = PC_SAMPLE_NUM_IPIDS;
			i = copy_to_user(arg,&ipids,sizeof(ipids));
			if (i<0) 
				return -EINVAL;
			return 0;
	
		} else if  (ipids.cmd == PERF_SAMPLE_IPID_GET_PID ) {
			if (ipids.spec.entry.index >= pc_record.num_ipid)
				return -EINVAL;
			ipids.spec.entry.pid = 
				pc_record.ipid[ipids.spec.entry.index];
			i = copy_to_user(arg,&ipids,sizeof(ipids));
			if (i<0) 
				return -EINVAL;
			return 0;
		}


		/* Set operations */
		ccr = get_CCR();
		if (CCR_GET_CTE(ccr))
			return -EBUSY;

		if (ipids.cmd == PERF_SAMPLE_IPID_ADD ) {
			for (i = 0; i <pc_record.num_ipid; i++) {
				if (pc_record.ipid[i] == ipids.spec.entry.pid)
					return 0;
			}
			pc_record.ipid[i] = ipids.spec.entry.pid;
			pc_record.num_ipid ++;
			return 0;

		} else if (ipids.cmd == PERF_SAMPLE_IPID_DEL ) {
			for (i = 0; i <pc_record.num_ipid; i++) {
				if (pc_record.ipid[i] == ipids.spec.entry.pid)
				break;
			}
			if ( i >= pc_record.num_ipid )
				return -EINVAL;

			for (i++; i <pc_record.num_ipid; i++) {
				pc_record.ipid[i-1] = pc_record.ipid[i];
			}
			pc_record.num_ipid --;
			return 0;

		}
		return -EINVAL;
		break;

	  case PERF_IOCSETSAMPLEMODE:
		ccr = get_CCR();
		if (!CCR_GET_CTE(ccr)) {
			struct perf_dev_sample_mode mode_spec;
			int i;

			i = copy_from_user(&mode_spec,arg,sizeof(mode_spec));
			if (i<0) 
				return -EINVAL;
			return set_sample_mode(&mode_spec);
		}
		return -EBUSY;
	  	break;

	  case PERF_IOCINTERNAL:
	  	cmd_pkt = arg;
		sub_cmd = cmd_pkt -> cmd;
		if (sub_cmd == PERF_SET_ASYNC ) {
			async_queue_p = cmd_pkt->arg0;
			return 0;
			break;
		} else if (sub_cmd == PERF_ACQUIRE_WR 
		      || sub_cmd == PERF_ACQUIRE) {
		        my_cmd_pkt.cmd = PERF_REFCNT;
			if (perf_counter_ops->control(cpuid,
				PERF_IOCINTERNAL, &my_cmd_pkt)){
				return  -EBUSY;
			}
			if (sample_refcnt) {
				return  -EBUSY;
			}

			sample_refcnt ++;
			r5900_perf_mode=PERF_MODE_SAMPLE;

			if (alloc_buffer(PC_SAMPLE_DEFAULT_NUM_POOL)<0)  {
				r5900_perf_mode=PERF_MODE_UNKOWN;
				sample_refcnt --;
				return -ENOMEM;
			}

			pc_record.sample_mode=PERF_SAMPLE_ROTATE_FAST;
			if (!initialized) {
				init_table();
				initialized = 1;
			}
			init_sample_dev(pc_record.sample_mode,0);
			pc_record.num_ipid=0;
			memset(&pc_record.ipid, 0, sizeof(pc_record.ipid));
			return  0;
		} else if (sub_cmd == PERF_RELEASE
				|| sub_cmd == PERF_RELEASE_WR ) {

			if (sample_refcnt>1) {
				sample_refcnt --;
				printk(KERN_ERR "error on sample_refcnt\n");
				return  0;
			}
			control_sample(cpuid, PERF_IOCSTOP, NULL);
			async_queue_p = NULL;
			free_buffer();
			r5900_perf_mode=PERF_MODE_UNKOWN;
			sample_refcnt --;
			return  0;
		} else if (sub_cmd == PERF_REFCNT) {
			return sample_refcnt;
		} else if (sub_cmd == PERF_WRITER) {
			return  0;
		}
		return -EINVAL;
		break;
	  default:
		break;
	}
	return -EINVAL;
}

/*========================================================================*/
/* /dev interface  */
/*========================================================================*/


static struct perf_sample_operations pc_rec_ops ={
	record: record_sample,
	retrieve_buffer: retrieve_sample_buffer,
	release_buffer: release_sample_buffer,
	is_empty: is_empty,
	reset:	reset_sample,
	control:  control_sample,
	get_info: get_info,
};


/*
 * extern vars.
 */

struct perf_sample_operations *perf_sample_ops = &pc_rec_ops;


