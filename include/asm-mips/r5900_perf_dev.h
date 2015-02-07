/*
 * r5900_perf_dev.h - r5900 spcific pc sampling device interface
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#ifndef _LANGUAGE_ASSEMBLY
#include <linux/types.h>
#include <asm/ioctl.h>
#endif /* _LANGUAGE_ASSEMBLY */

/* Number of perf counter */
#define PERF_DEV_NUM_PERF_CTR 2 
/* Number of perf counter event types */
#define PERF_DEV_NUM_CTR_EVENT_PER_CTR 0x20 
#define PERF_DEV_NUM_CTR_EVENT \
		(PERF_DEV_NUM_CTR_EVENT_PER_CTR*PERF_DEV_NUM_PERF_CTR) 

#ifdef __KERNEL__
/* perf counter device mode */
#define PERF_MODE_UNKOWN	0
#define PERF_MODE_SAMPLE	1	/* pc sampling */
#define PERF_MODE_COUNTER	2	/* free running perf counter */
#endif

/*====================================================*/

/*
 *  definitions for sample mode "/dev/perf_ctrl"
 */

#ifndef _LANGUAGE_ASSEMBLY

/* Ioctls for "/dev/perf_ctrl" */
/* 	type: 'p' */
/* 	nr:   0xf0 - 0xf4 */

struct perf_dev_ctlr_info {
	int	cpu_clock;	// estimated CPU cycle speed.
	int	cpu_prid;	// Processor Revision Id.
};

#define PERF_DEV_ARCH_INFO_STRUCT perf_dev_ctlr_info
#endif /* _LANGUAGE_ASSEMBLY */

/*====================================================*/

/*
 *  definitions for sample mode "/dev/perf_counter"
 */

#ifndef _LANGUAGE_ASSEMBLY

/* Ioctls for "/dev/perf_counter" */
/* 	type: 'p' */
/* 	nr:   0xf5 - 0xf9 */
#define PERF_IOCSETCOUNTERS		 _IO('p',0xf5)		//NotYet

/* for ioctl  PERF_IOCSETCOUNTERS */

struct perf_dev_counters {
	__u8	ctr0_target;
	__u8	ctr1_target;
	__u8	ctr0_event;
	__u8	ctr1_event;
	__u64	ctr0_value;
	__u64	ctr1_value;
};

struct perf_dev_set_counters {
	struct perf_dev_counters *new_counters;
	struct perf_dev_counters *old_counters;
};

#endif /* _LANGUAGE_ASSEMBLY */

/*====================================================*/

/*
 *  definitions for sample mode "/dev/perf_sample"
 */

/* 
  layout of pc_sample_cid_t

	15bit		CTR1 is vaild
	8bit-12bit	CTR1 event
	7bit		CTR0 is vaild
	0bit-4bit	CTR0 event
*/

#define PERF_SAMPLE_CTR_EVENT_MASK (PERF_DEV_NUM_CTR_EVENT_PER_CTR-1)
#define PERF_SAMPLE_CTR1_SHIFT	8
#define PERF_SAMPLE_CTR0_BIT	0x80
#define PERF_SAMPLE_CTR1_BIT  (PERF_SAMPLE_CTR0_BIT << PERF_SAMPLE_CTR1_SHIFT)


/* sample mode value */
#define		PERF_SAMPLE_NUM_MODE         3
#define		PERF_SAMPLE_FIXED            0
#define		PERF_SAMPLE_ROTATE_SLOW      1
#define		PERF_SAMPLE_ROTATE_FAST      2

#ifndef _LANGUAGE_ASSEMBLY

/* Ioctls for "/dev/perf_sample" */
/* 	type: 'p' */
/* 	nr:   0xfa - 0xff */
#define PERF_IOCGETSAMPLEINFO	 _IO('p',0xfa)
#define PERF_IOCSETSAMPLEMODE	 _IO('p',0xfb)
#define PERF_IOCGETSAMPLESTATS	 _IO('p',0xfc)
#define PERF_IOCIGNORESAMPLEPIDS	 _IO('p',0xfd)


/* for ioctl  PERF_IOCSETSAMPLEINFO */

struct perf_sample_counter_info {
	__s8	avail; 	/* # of available periods, or no function if zero */
	__s8	same_as;/* no same measure, if same_as < 0 */
	__u32	count;  /* value to count (not use, if zero) */
};

#define PERF_DEV_DESCR_STR_LEN	256
struct	perf_dev_sample_mode_info {
	int	sample_mode;	/* current sample mode */
	int	selector;	/* current selection of the sample mode  */
	int	num_selection;	/* # of selections for the current mode */
	int	num_period;	/* # of rotation periods for 
					the current selection */
	unsigned long polling_period; /* polling period for read(jiffies) */
	/* short descrioption about the current selection */
	char	short_descr[ PERF_DEV_DESCR_STR_LEN];
	/* detail of current selected counter_set */
	struct perf_sample_counter_info
			sample_counter[PERF_DEV_NUM_CTR_EVENT];
};

/* for ioctl PERF_IOCSETSAMPLEMODE */
struct	perf_dev_sample_mode {
	int	sample_mode;	/* sample mode */
	int	selector;	/* selector */
};

/* for ioctl PERF_IOCGETSAMPLESTATS */
struct	perf_dev_sample_stats {
	unsigned long	num_losts;
	unsigned long	num_polls;

};

/* for PERF_IOCIGNORESAMPLEPIDS */
struct	perf_dev_sample_ignore_pids {
	int		cmd;
	union {
		struct {
			int		index;
			unsigned long	pid;
		} entry;
		struct {
			int 	current;
			int	max;
		} list;
	} spec ;
};

/* perf_dev_sample_ignore_pids.cmd values */
#define PERF_SAMPLE_IPID_ADD	1	/* add pid to IPID memeber */
#define PERF_SAMPLE_IPID_DEL	2	/* delete pid from IPID memeber */
#define PERF_SAMPLE_IPID_GET_NUM  3	/* get current and max number of 
								IPID memeber*/
#define PERF_SAMPLE_IPID_GET_PID  4	/* get pid secified by index */

#endif /* _LANGUAGE_ASSEMBLY */

/* helper to access cur_counter_set[inx] . */

#define PERF_SAMPLE_CTR0_TO_INX(x)	(x)
#define PERF_SAMPLE_INX_TO_CTR0(x)	(x)

#define PERF_SAMPLE_CTR1_TO_INX(x)	((x)+(PERF_DEV_NUM_CTR_EVENT>>1))
#define PERF_SAMPLE_INX_TO_CTR1(x)	((x)-(PERF_DEV_NUM_CTR_EVENT>>1))

/* helper to use measure[inx].count */

#define PERF_SAMPLE_COUNT_TO_LOADVAL(x)	(0x80000000 - (x))
#define PERF_SAMPLE_LOADVAL_TO_COUNT(x)	(0x80000000 - (x))


#ifndef _LANGUAGE_ASSEMBLY


typedef __u8 perf_sample_event_t;
typedef __u16 perf_sample_cid_t;

/* access methods for the pc_sample_cid_t */

inline
static int perf_sample_get_ctr_num(void) 
{
	return PERF_DEV_NUM_PERF_CTR;
}

inline
static int perf_sample_ctr_in_cid(perf_sample_cid_t cid, int ctr) 
{
	perf_sample_cid_t ctrbit;
	if (ctr<0 || ctr>1) return 0; /* sanity check */
	ctrbit = ctr ? PERF_SAMPLE_CTR1_BIT : PERF_SAMPLE_CTR0_BIT;
	return ((cid & ctrbit) != 0);
}

inline
static int perf_sample_get_ctr_event(perf_sample_cid_t cid, int ctr) 
{
	if (ctr<0 || ctr>1) return -1; /* sanity check */
	if (ctr) cid >>=  PERF_SAMPLE_CTR1_SHIFT;
	return (cid & PERF_SAMPLE_CTR_EVENT_MASK);
}

#endif /* _LANGUAGE_ASSEMBLY */
