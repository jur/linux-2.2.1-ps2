/*
 * perf_dev.h - pc sampling device interface
 */


#ifndef _LINUX_PERF_DEV_H
#define _LINUX_PERF_DEV_H


#include <asm/perf_dev.h>


#ifndef PERF_DEV_NAME
#define PERF_DEV_NAME		"perf_dev"
#endif
#ifndef PERF_DEV_CTRL_NAME
#define PERF_DEV_CTRL_NAME      "perf_ctrl"
#endif
#ifndef PERF_DEV_COUNTER_NAME
#define PERF_DEV_COUNTER_NAME   "perf_counter"
#endif
#ifndef PERF_DEV_SAMPLE_NAME
#define PERF_DEV_SAMPLE_NAME    "perf_sample"
#endif

#ifndef _LANGUAGE_ASSEMBLY

struct perf_sample_entry {
	void		*pc;		/* sampled pc */
	unsigned long	pid;		/* pid */
	perf_sample_event_t	event;	/* event type */
	perf_sample_cid_t	cid;	/* perf. counter ID */
	unsigned long	jiffies; 	/* jiffies */
	unsigned long	aux; 		/* auxiliary info */
};

struct	perf_dev_info {
	int num_perf_ctr;	/* num of real perf. counters per one CPU */
	int num_event_type;	/* num of perf ctr event types per one CPU */
#ifdef PERF_DEV_ARCH_INFO_STRUCT
	struct PERF_DEV_ARCH_INFO_STRUCT	arch_info;
#endif
};


/* Ioctls */
/* 	type: 'p' */
/* 	nr:   0xe0 - 0xff (0xf0 - 0xff for machine depend)*/
#define PERF_IOCGETINFO	 	_IO('p',0xe0) 
#define PERF_IOCSTART	 	_IO('p',0xe1)
#define PERF_IOCSTOP		_IO('p',0xe2)

#ifdef __KERNEL__
#define PERF_IOCINTERNAL	 _IO('p',0xef)
#define		PERF_ACQUIRE	1
#define		PERF_RELEASE	2
#define		PERF_ACQUIRE_WR	3
#define		PERF_RELEASE_WR	4
#define		PERF_REFCNT	10
#define		PERF_WRITER	11
#define 	PERF_SET_ASYNC	20

struct perf_dev_internal_cmd {
	int	cmd;
	void	*arg0,*arg1,*arg2;
};
#endif /* _KERNEL_ */

#endif /* _LANGUAGE_ASSEMBLY */

/* Event type */
#define PERF_SAMPLE_EVENT_KERNEL		1
#define PERF_SAMPLE_EVENT_USER			2
#define PERF_SAMPLE_CONTROL_EVENT		3	// notyet
#define PERF_SAMPLE_MODULE_EVENT		4	// notyet
#define PERF_SAMPLE_TASK_EVENT			5	// notyet



#ifdef __KERNEL__

#ifndef _LANGUAGE_ASSEMBLY

/* sampling operation */
struct perf_sample_operations {
	long  (* control) (int cpuid, unsigned int cmd , void *arg);
	void (* reset) (int cpuid);
	void * (*get_info) (char *buff, int sz);
	int  (* record) (int cpuid, struct perf_sample_entry *entry);
	int  (* retrieve_buffer) (int cpuid, 
			struct perf_sample_entry (**entry)[], void **key);
	int  (* release_buffer) (int cpuid, void *key);
	int  (* is_empty) (int cpuid);
};

/* counting operation */
struct perf_counter_operations {
	long  (* control) (int cpuid, unsigned int cmd , void *arg);
	void (* reset) (int cpuid);
	void * (*get_info) (char *buff, int sz);
};

/* /dev/perf_ctrl */
struct perf_ctrl_operations {
	long  (* control) (int cpuid, unsigned int cmd , void *arg);
	void * (*get_debug) (char *buff, int sz);
};

#endif /*_LANGUAGE_ASSEMBLY*/


#endif  /* __KERNEL_ */

#endif /* _LINUX_PERF_DEV_H */

