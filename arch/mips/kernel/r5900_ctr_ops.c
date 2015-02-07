/*
 * r5900_ctr_ops.c - r5900 spcific perf counter recording  serivce 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#include <linux/sched.h>	/* jiffies */
#include <linux/param.h>	/* HZ */
#include <linux/errno.h>	/* error codes */
#include <linux/perf_dev.h>

#include <linux/delay.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/perf_counter.h>

#include "r5900_perf.h"
extern u8 r5900_perf_mode;
extern struct perf_sample_operations *perf_sample_ops;

static int counter_refcnt=0;
static void * counter_writer=NULL;

extern unsigned int r5900_get_cpu_clock(void);

static void *get_info(char *buff, int size)
{
#define STR_MARGIN	80
	int	out;
	void	*ret;
	u32	cs;
	u32	ccr;
	u32	ctr0_event,  ctr1_event;
	u32	ctr0_target, ctr1_target;
	u64	ctr0_value,  ctr1_value;

	buff[0]='\0';

	ccr = get_CCR();
	cs = enter_cs();

#define DEBGU_MSG_UPDATE \
	size -= out; buff += out; if (size <= STR_MARGIN) { ret = 0; goto out; }


	out = sprintf(buff, "*CPU clock:\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "  clock:%4d MHz\n", 
					r5900_get_cpu_clock());
	DEBGU_MSG_UPDATE
#if 0
	if (r5900_perf_mode!=PERF_MODE_COUNTER) {
		ret = buff;
		goto out;
	}
#endif
	ccr = get_CCR();
	ctr0_value = get_CTR0_64();
	ctr1_value = get_CTR1_64();
	ctr0_event = CCR_GET_CTR0_EVENT(ccr);
	ctr1_event = CCR_GET_CTR1_EVENT(ccr);
	ctr0_target = CCR_GET_CTR0_TARGET(ccr);
	ctr1_target = CCR_GET_CTR1_TARGET(ccr);

	out = sprintf(buff, "\n*CCR:\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "    target0:0x%4.4x    ", ctr0_target);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "    target1:0x%4.4x\n",   ctr1_target);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "     event0:0x%4.4x    ", ctr0_event);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "     event1:0x%4.4x\n",   ctr1_event);
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "\n*CTR:\n");
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "       val0:0x%8.8x:%8.8x\n", 
			(u32) (ctr0_value>>32),  (u32) (ctr0_value&0xffffffff));
	DEBGU_MSG_UPDATE
	out = sprintf(buff, "       val1:0x%8.8x:%8.8x\n", 
			(u32) (ctr1_value>>32), (u32) (ctr1_value&0xffffffff));
	DEBGU_MSG_UPDATE

	ret = buff;
out:
	leave_cs(cs);

	return ret;
}


/* return 1 if ctr was running, otherwise 0 */
int stop_ctr(void)
{
	int r;
	u32 ccr;

	ccr = get_CCR();
	r = CCR_GET_CTE(ccr);
	ccr = CCR_SET_CTE (ccr,0);
	set_CCR(ccr);
	return r;
}

void start_ctr(void)
{
	u32 ccr;

	ccr = get_CCR();
	ccr = CCR_SET_CTE (ccr,1);
	set_CCR(ccr);
}

void r5900_perf_iocgetinfo(struct perf_dev_info *info)
{
	info->num_perf_ctr = PERF_DEV_NUM_PERF_CTR;
	info->num_event_type = PERF_DEV_NUM_CTR_EVENT;
	asm  ( "mfc0	%0, $15" : "=r" (info->arch_info.cpu_prid) /*output*/);
	info->arch_info.cpu_clock=r5900_get_cpu_clock();
}

static long control_counter(int cpuid, unsigned int cmd, void *arg)
{
	int r, cte;
	u32 ccr;
	struct perf_dev_internal_cmd *cmd_pkt;
	struct perf_dev_internal_cmd my_cmd_pkt;
	struct perf_dev_set_counters ctrs_spec;
	struct perf_dev_counters new,old;
	struct perf_dev_info devinfo;
	int sub_cmd;

	switch(cmd) {
	  case PERF_IOCGETINFO:
		r5900_perf_iocgetinfo(&devinfo);
		r = copy_to_user(arg, &devinfo, sizeof(devinfo));
		return r;
	  	break;

	  case PERF_IOCSETCOUNTERS:
		r = copy_from_user(&ctrs_spec,arg,sizeof(ctrs_spec));
		if (r<0) return r;

		if (!ctrs_spec.old_counters && !ctrs_spec.new_counters) {
			return -EINVAL;
		}

		if ((counter_writer!=current) && ctrs_spec.new_counters) {
			return -EPERM;
		}

		/* save counter running state */
		cte = stop_ctr();
		/* retrieve current counters */
		if (ctrs_spec.old_counters) {
			ccr = get_CCR();
			old.ctr0_value = get_CTR0_64();
			old.ctr1_value = get_CTR1_64();
			old.ctr0_event = CCR_GET_CTR0_EVENT(ccr);
			old.ctr1_event = CCR_GET_CTR1_EVENT(ccr);
			old.ctr0_target = CCR_GET_CTR0_TARGET(ccr);
			old.ctr1_target = CCR_GET_CTR1_TARGET(ccr);
			r = copy_to_user(ctrs_spec.old_counters, 
				&old, sizeof(old));
			if (r<0) goto out;
		}

		/* set new counters */
		if (ctrs_spec.new_counters) {
			r = copy_from_user(&new, ctrs_spec.new_counters,
						sizeof(new));
			if (r<0) goto out;

			set_CTR0_64(new.ctr0_value);
			set_CTR1_64(new.ctr1_value);
			ccr = 0;
			ccr = CCR_SET_CTR0_EVENT(ccr, new.ctr0_event);
			ccr = CCR_SET_CTR1_EVENT(ccr, new.ctr1_event);
			ccr = CCR_SET_CTR0_TARGET(ccr, new.ctr0_target);
			ccr = CCR_SET_CTR1_TARGET(ccr, new.ctr1_target);
			set_CCR(ccr);
		}
		r = 0;
	    out:
		/* restore counter running state */
		if (cte) start_ctr();
		return r;
	  	break;

	  case PERF_IOCSTART:
		if (counter_writer!=current) return -EPERM;
		start_ctr();
		return 0;
	  	break;

	  case PERF_IOCSTOP:
		if (counter_writer!=current) return -EPERM;
		(void)stop_ctr();
		return 0;
		break;

	  case PERF_IOCINTERNAL:
	  	cmd_pkt = arg;
		sub_cmd = cmd_pkt -> cmd;
		if ( sub_cmd == PERF_ACQUIRE_WR 
		      || sub_cmd == PERF_ACQUIRE) {
		        my_cmd_pkt.cmd = PERF_REFCNT;
			if (perf_sample_ops->control(cpuid,
				PERF_IOCINTERNAL, &my_cmd_pkt)) {
				return -EBUSY;
			}
			if (sub_cmd == PERF_ACQUIRE_WR)  {
				if (counter_writer)  {
					return  -EBUSY;
				}
				counter_writer=current;
			} 
			r5900_perf_mode=PERF_MODE_COUNTER;
			counter_refcnt ++;

			if (counter_writer==current) {
				// init counters.
				set_CTR0_64(0);
				set_CTR1_64(0);
				set_CCR(0);
			}
			return 0;
		} else if (sub_cmd == PERF_RELEASE
				|| sub_cmd == PERF_RELEASE_WR ) {
			counter_refcnt --;
			if (!counter_refcnt) {
				control_counter(cpuid, PERF_IOCSTOP, NULL);
				r5900_perf_mode=PERF_MODE_UNKOWN;
			}
			if (sub_cmd == PERF_RELEASE_WR)  {
				counter_writer=0;
			}
			return 0;
		} else if (sub_cmd == PERF_REFCNT) {
			return  counter_refcnt;
		} else if (sub_cmd == PERF_WRITER) {
			return  (long) counter_writer;
		}
		return  -EINVAL;
		break;
	  default:
		break;
	}
	return -EINVAL;
}

static struct perf_counter_operations counter_ops ={
	control:  control_counter,
	get_info: get_info,
};


/*
 * extern vars.
 */

struct perf_counter_operations *perf_counter_ops=&counter_ops;


//==============

static long control_ctrl(int cpuid, unsigned int cmd, void *arg)
{
	int r;
	struct perf_dev_info devinfo;

	switch(cmd) {
	  case PERF_IOCGETINFO:
		r5900_perf_iocgetinfo(&devinfo);
		r = copy_to_user(arg, &devinfo, sizeof(devinfo));
		return r;
	  	break;
	  default:
		break;
	}
	return -EINVAL;
}

static struct perf_ctrl_operations ctrl_ops ={
	control:  control_ctrl,
};

/*
 * extern vars.
 */

struct perf_ctrl_operations *perf_ctrl_ops=&ctrl_ops;
