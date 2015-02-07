/*
 *  PlayStation 2 CD/DVD driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: cdvd.c,v 1.27.2.9 2001/09/19 10:23:18 takemura Exp $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/cdrom.h>
#include <linux/iso_fs.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/siflock.h>
#include "libcdvd.h"
#include "cdvdcall.h"
#include "cdvd.h"


/*
 * macro defines
 */
#define MAJOR_NR	ps2cdvd_major
#define DEVICE_NAME	"PS2 CD/DVD-ROM"
/* #define DEVICE_INTR	do_ps2cdvd */
#define DEVICE_REQUEST	do_ps2cdvd_request
#define DEVICE_NR(dev)	(MINOR(device))
#define DEVICE_ON(dev)
#define DEVICE_OFF(dev)

#define INIT_BLKDEV	0x0001
#define INIT_CDROM	0x0002
#define INIT_IOPSIDE	0x0004
#define INIT_LABELBUF	0x0008
#define INIT_DATABUF	0x0010

#define ENTER	1
#define LEAVE	0
#define INVALID_DISCTYPE	-1

#define SET_TIMER(n) do { \
		io_timer.expires = jiffies + (n); \
		add_timer(&io_timer); \
	} while (0)
#define RESET_TIMER() del_timer(&io_timer)

#define DATA_SECT_SIZE	2048
#define AUDIO_SECT_SIZE	2352
#define BUFFER_ALIGNMENT	64
#define MIN(a, b)	((a) < (b) ? (a) : (b))

/*
 * data types
 */
enum {
	EV_START,
	EV_INTR,
	EV_TIMEOUT,
	EV_TICK,
};

#define	STAT_WAIT_DISC			(1	)
#define	STAT_INIT_TRAYSTAT		(2	)
#define	STAT_CHECK_DISCTYPE		(3	)
#define	STAT_INIT_CHECK_READY		(4	)
#define	STAT_TOC_READ			(5	)
#define	STAT_LABEL_READ			(6	)
#define	STAT_LABEL_READ_ERROR_CHECK	(7	)
#define	STAT_READY			(8	)
#define	STAT_CHECK_TRAY			(9	)
#define	STAT_READ			(10	)
#define	STAT_READ_EOM_RETRY		(11	)
#define	STAT_READ_ERROR_CHECK		(12	)
#define	STAT_INVALID_DISC		(13	)
#define	STAT_ERROR			(14	)
#define	STAT_SET_MMODE			(15	)
#define	STAT_SPINDOWN			(16	)
#define	STAT_IDLE			(17	)

struct ps2cdvd_event {
	int type;
	void *arg;
};

#define DVD_DATA_SECT_SIZE 2064
#define DVD_DATA_OFFSET 12

/*
 * function prototypes
 */
static int ps2cdvd_open(struct cdrom_device_info *, int);
static void ps2cdvd_release(struct cdrom_device_info *);
static int ps2cdvd_media_changed(struct cdrom_device_info *, int);
static int ps2cdvd_tray_move(struct cdrom_device_info *, int);
static int ps2cdvd_drive_status(struct cdrom_device_info *, int);
static int ps2cdvd_lock_door(struct cdrom_device_info *, int);
static int ps2cdvd_select_speed(struct cdrom_device_info *, int);
static int ps2cdvd_reset(struct cdrom_device_info *);
static int ps2cdvd_audio_ioctl(struct cdrom_device_info *, unsigned int, void*);
static int ps2cdvd_dev_ioctl(struct cdrom_device_info *, unsigned int, unsigned long);

static void do_ps2cdvd_request(void);

static void ps2cdvd_state_machine(struct ps2cdvd_event* ev);
static void ps2cdvd_timer(unsigned long);
static void ps2cdvd_cleanup(void);
static int ps2cdvd_enter_leave(int, int);
static int ps2cdvd_ready(void);

static inline int ps2cdvd_enter(int state) {
	return ps2cdvd_enter_leave(state, 1);
}

static inline void ps2cdvd_leave(int state) {
	(void)ps2cdvd_enter_leave(state, 0);
}

static inline int decode_bcd(int bcd) {
	return ((bcd >> 4) & 0x0f) * 10 + (bcd & 0x0f);
}

static inline long msftolba(int m, int s, int f)
{
	return (m) * 4500 + (s) * 75 + (f) - 150;
}

static inline void lbatomsf(long lba, int *m, int *s, int *f)
{
	lba -= 150;
	*m = (lba / 4500);
	*s = (lba % 4500) / 75;
	*f = (lba % 75);
}

/*
 * variables
 */
int ps2cdvd_wrong_disc_retry = 1;
int ps2cdvd_check_interval = 2;
int ps2cdvd_spindown = 10;
int ps2cdvd_read_ahead = 32;
#if 1
unsigned long ps2cdvd_debug = DBG_DIAG;
#else
unsigned long ps2cdvd_debug = (DBG_DIAG | DBG_READ | DBG_INFO | DBG_STATE);
#endif
static int ps2cdvd_major = PS2CDVD_MAJOR;
static int ps2cdvd_blocksizes[1] = { DATA_SECT_SIZE, };
static int ps2cdvd_hardsectsizes[1] = { DATA_SECT_SIZE, };
static int initialized = 0;

static int disc_locked = 0;
static int disc_lock_key_valid = 0;
static unsigned long disc_lock_key;
static int label_valid = 0;
static u_char *labelbuf = NULL;
static int disc_changed = 1;
static int disc_type = INVALID_DISCTYPE;
static int media_mode = SCECdCD;
static int prev_tray_check = 0;
static int prev_read = 0;
static int toc_valid = 0;
static long leadout_start;
static unsigned char tocbuf[1024];

static unsigned char *ps2cdvd_databuf = NULL;
static unsigned char *ps2cdvd_databufx = NULL;
static int ps2cdvd_databuf_size = 16;
static int ps2cdvd_databuf_addr = -1;

ps2sif_lock_t *ps2cdvd_lock;
static ps2sif_lock_queue_t ps2cdvd_lock_qi;
static struct wait_queue *statq = NULL;

static struct timer_list io_timer;
static int ps2cdvd_state;

#ifdef MODULE
EXPORT_NO_SYMBOLS;
MODULE_PARM(ps2cdvd_major, "0-255i");
MODULE_PARM(ps2cdvd_debug, "i");
MODULE_PARM(ps2cdvd_check_interval, "1-30i");
MODULE_PARM(ps2cdvd_spindown, "1-3600i");
MODULE_PARM(ps2cdvd_wrong_disc_retry, "0-1i");
MODULE_PARM(ps2cdvd_read_ahead, "1-256i");
MODULE_PARM(ps2cdvd_databuf_size, "1-256i");
#endif

static struct sceCdRMode DataMode = {
	100,		/* try count			*/
	SCECdSpinNom,	/* try with maximum speed	*/
	SCECdSecS2048,	/* data size = 2048		*/
	0xff		/* padding data			*/
};

static struct sceCdRMode CDDAMode = {
	50,		/* try count			*/
	SCECdSpinNom,	/* try with maximum speed	*/
	SCECdSecS2352|0x80,	/* data size = 2352, CD-DA		*/
	0x0f		/* padding data			*/
};

#include <linux/blk.h>

static struct cdrom_device_ops ps2cdvd_dops = {
	ps2cdvd_open,			/* open */
	ps2cdvd_release,	       	/* release */
	ps2cdvd_drive_status,		/* drive status */
	ps2cdvd_media_changed,		/* media changed */
	ps2cdvd_tray_move,	       	/* tray move */
	ps2cdvd_lock_door,		/* lock door */
	ps2cdvd_select_speed,		/* select speed */
	NULL,				/* select disc */
	NULL,				/* get last session */
	NULL,				/* get universal product code */
	ps2cdvd_reset,			/* hard reset */
	ps2cdvd_audio_ioctl,		/* audio ioctl */
	ps2cdvd_dev_ioctl,		/* device-specific ioctl */
	CDC_OPEN_TRAY | CDC_LOCK | CDC_MEDIA_CHANGED |
	CDC_RESET | CDC_PLAY_AUDIO | CDC_IOCTLS | CDC_DRIVE_STATUS,
					/* capability */
	0,				/* number of minor devices */
};

static struct cdrom_device_info ps2cdvd_info = {
	&ps2cdvd_dops,			/* device operations */
	NULL,				/* link */
	NULL,				/* handle */
	0,				/* dev */
	0,				/* mask */
	2,				/* maximum speed */
	1,				/* number of discs */
	0,				/* options, not owned */
	0,				/* mc_flags, not owned */
	0,				/* use count, not owned */
	"ps2cdvd",		       	/* name of the device type */
};

/* error strings */
char *ps2cdvd_errors[] = {
	[SCECdErNO]	= "no error",
	[SCECdErEOM]	= "end of media",
	[SCECdErTRMOPN]	= "terminated by user",
	[SCECdErREAD]	= "read error",
	[SCECdErPRM]	= "parameter error",
	[SCECdErILI]	= "invalid lendth",
	[SCECdErIPI]	= "invalid address",
	[SCECdErCUD]	= "inappropriate disc",
	[SCECdErNORDY]	= "not ready",
	[SCECdErNODISC]	= "no disc",
	[SCECdErOPENS]	= "open tray",
	[SCECdErCMD]	= "command not supported",
	[SCECdErABRT]	= "aborted",
};

/* disc type names */
char *ps2cdvd_disctypes[] = {
	[SCECdIllgalMedia]	= "illegal media",
	[SCECdDVDV]		= "DVD video",
	[SCECdCDDA]		= "CD DA",
	[SCECdPS2DVD]		= "PS2 DVD",
	[SCECdPS2CDDA]		= "PS2 CD DA",
	[SCECdPS2CD]		= "PS2 CD",
	[SCECdPSCDDA]		= "PS CD DA",
	[SCECdPSCD]		= "PS CD",
	[SCECdUNKNOWN]		= "unknown",
	[SCECdDETCTDVDD]	= "DVD-dual detecting",
	[SCECdDETCTDVDS]	= "DVD-single detecting",
	[SCECdDETCTCD]		= "CD detecting",
	[SCECdDETCT]		= "detecting",
	[SCECdNODISC]		= "no disc",
};

/* event names */
char *ps2cdvd_events[] = {
	[EV_START]		= "START",
	[EV_INTR]		= "INTR",
	[EV_TIMEOUT]      	= "TIMEOUT",
	[EV_TICK]		= "TICK",
};

/* state names */
char *ps2cdvd_states[] = {
	[STAT_WAIT_DISC			& 0xff]	= "WAIT_DISC",
	[STAT_INIT_TRAYSTAT		& 0xff]	= "INIT_TRAYSTAT",
	[STAT_CHECK_DISCTYPE		& 0xff]	= "CHECK_DISCTYPE",
	[STAT_INIT_CHECK_READY		& 0xff]	= "INIT_CHECK_READY",
	[STAT_SET_MMODE			& 0xff] = "SET_MMODE",
	[STAT_TOC_READ			& 0xff]	= "TOC_READ",
	[STAT_LABEL_READ		& 0xff]	= "LABEL_READ",
	[STAT_LABEL_READ_ERROR_CHECK	& 0xff]	= "LABEL_READ_ERROR_CHECK",
	[STAT_READY			& 0xff]	= "READY",
	[STAT_CHECK_TRAY		& 0xff]	= "CHECK_TRAY",
	[STAT_READ			& 0xff]	= "READ",
	[STAT_READ_EOM_RETRY		& 0xff] = "READ_EOM_RETRY",
	[STAT_READ_ERROR_CHECK		& 0xff]	= "READ_ERROR_CHECK",
	[STAT_INVALID_DISC		& 0xff]	= "INVALID_DISC",
	[STAT_ERROR			& 0xff]	= "ERROR",
	[STAT_SPINDOWN			& 0xff] = "SPINDOWN",
	[STAT_IDLE			& 0xff] = "IDLE",
};

/*
 * function bodies
 */
static void
invalidate_discinfo(void)
{
	disc_type = INVALID_DISCTYPE;
	if (label_valid) DPRINT(DBG_DLOCK, "label gets invalid\n");
	label_valid = 0;
	if (toc_valid) DPRINT(DBG_VERBOSE, "toc gets invalid\n");
	toc_valid = 0;
	ps2cdvd_databuf_addr = -1;
}

static unsigned long
checksum(unsigned long *data, int len)
{
	unsigned long sum = 0;
	while (len--)
		sum ^= *data++;
	return sum;
}

static void
print_isofsstr(char *str, int len)
{
	int i;
	int space = 0;
	for (i = 0;i < len; i++) {
	  if (*str == ' ') {
	    if (!space) {
	      space = 1;
	      printk("%c", *str++);
	    }
	  } else {
	    space = 0;
	    printk("%c", *str++);
	  }
	}
}

char*
ps2cdvd_geterrorstr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_errors) &&
	    ps2cdvd_errors[no]) {
		return ps2cdvd_errors[no];
	} else {
		sprintf(buf, "unknown error(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_getdisctypestr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_disctypes) &&
	    ps2cdvd_disctypes[no]) {
		return ps2cdvd_disctypes[no];
	} else {
		sprintf(buf, "unknown type(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_geteventstr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_events) &&
	    ps2cdvd_events[no]) {
		return ps2cdvd_events[no];
	} else {
		sprintf(buf, "unknown event(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_getstatestr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_states) &&
	    ps2cdvd_states[no]) {
		return ps2cdvd_states[no];
	} else {
		sprintf(buf, "unknown state(0x%02x)", no);
		return buf;
	}
}

int
ps2cdvd_lowlevel_lock(void)
{
	DPRINT(DBG_LOCK, "lowlevel_lock\n");
	return (ps2sif_lowlevel_lock(ps2cdvd_lock, &ps2cdvd_lock_qi, 0));
}

void
ps2cdvd_lowlevel_unlock(void)
{
	DPRINT(DBG_LOCK, "lowlevel_unlock\n");
	ps2sif_lowlevel_unlock(ps2cdvd_lock, &ps2cdvd_lock_qi);
}

static void
ps2cdvd_start(void *arg)
{
	struct ps2cdvd_event ev;

	ev.type = EV_START;
	ev.arg = (void*)NULL;
	ps2cdvd_state_machine(&ev);
}

static struct tq_struct ps2cdvd_start_task;

static void
do_ps2cdvd_request(void)
{
	INIT_REQUEST;
	if (!ps2cdvd_wrong_disc_retry && ps2cdvd_state == STAT_INVALID_DISC) {
		while (CURRENT)
			end_request(0);
	}
	if (in_interrupt()) {
		ps2cdvd_start(NULL);
	} else {
		/*
		 * ensure that we are in intterrupt to get low level lock.
		 * If you get low level lock in process context and
		 * call some iop access routine, you will sleep forever.
		 */
		ps2cdvd_start_task.routine = ps2cdvd_start;
		queue_task(&ps2cdvd_start_task, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

static int
ps2cdvd_ready()
{
	unsigned long flags;

	while (1) {
	  save_flags(flags);
	  cli();
	  switch (ps2cdvd_state) {
	  case STAT_WAIT_DISC:
	    if (disc_type == SCECdNODISC ||
		disc_type == SCECdIllgalMedia ||
		disc_type == INVALID_DISCTYPE ||
		disc_type == SCECdUNKNOWN) {
	      restore_flags(flags);
	      return -ENOMEDIUM;
	    }
	    break;
	  case STAT_READY:
	    restore_flags(flags);
	    prev_read = jiffies;
	    return (0);

	  case STAT_IDLE:
	    restore_flags(flags);
	    ps2cdvd_start(NULL);
	    continue;

	  case STAT_INVALID_DISC:
	    if (!ps2cdvd_wrong_disc_retry) {
	      restore_flags(flags);
	      return (-ENOMEDIUM);
	    }
	    break;

	  default:
	    break;
	  }
	  interruptible_sleep_on(&statq);
	  restore_flags(flags);
	  if(signal_pending(current))
	    return -ERESTARTSYS;
	}

	/* not reached */
}

static void
ps2cdvd_timer(unsigned long arg)
{
	struct ps2cdvd_event ev;

	ev.type = EV_TIMEOUT;
	ev.arg = (void*)arg;
	ps2cdvd_state_machine(&ev);
}

void
ps2cdvd_intr(void* arg, int result)
{
	struct ps2cdvd_event ev;

	ev.type = EV_INTR;
	ev.arg = (void*)arg;
	ps2cdvd_state_machine(&ev);
}

static int
ps2cdvd_enter_leave(int state, int enter)
{
	int res_state = state;

	switch (state) {
	case STAT_WAIT_DISC:
	  if (enter) {
	    if (ps2cdvd_state != STAT_CHECK_DISCTYPE)
	      invalidate_discinfo();
	    ps2cdvd_lowlevel_unlock();
	    if (CURRENT)
	      DPRINT(DBG_DIAG, "abort all pending request\n");
	    while (CURRENT)
	      end_request(0);
	    SET_TIMER(HZ * ps2cdvd_check_interval);
	  } else {
	    RESET_TIMER();
	  }
	  break;
	case STAT_INIT_TRAYSTAT:
	  if (enter) {
	    if (ps2cdvd_send_trayreq(SCECdTrayCheck) < 0) {
	      DPRINT(DBG_DIAG, "send_trayreq() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_CHECK_DISCTYPE:
	  if (enter) {
	    disc_type = INVALID_DISCTYPE;
	    if (ps2cdvd_send_gettype() < 0) {
	      DPRINT(DBG_DIAG, "send_gettype() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_INIT_CHECK_READY:
	  if (enter) {
	    //if (ps2cdvd_send_ready(1 /* non block */) < 0) {
	    if (ps2cdvd_send_ready(0 /* block */) < 0) {
	      DPRINT(DBG_DIAG, "send_ready() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_TOC_READ:
	  if (enter) {
	    if (ps2cdvd_send_gettoc(tocbuf, sizeof(tocbuf)) < 0) {
	      DPRINT(DBG_DIAG, "send_gettoc() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_SET_MMODE:
	  if (enter) {
	    DPRINT(DBG_INFO, "media mode %s\n",
		   media_mode == SCECdCD ? "CD" : "DVD");
	    if (ps2cdvd_send_mmode(media_mode) < 0) {
	      DPRINT(DBG_DIAG, "send_mmode() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_LABEL_READ:
	  if (enter) {
            if (media_mode == SCECdDVDV) {
	      if (ps2cdvd_send_read_dvd(16, 1, labelbuf, &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read_dvd() failed\n");
                res_state = ps2cdvd_enter(STAT_ERROR);
              } else {
                memcpy(labelbuf, labelbuf + DVD_DATA_OFFSET, DATA_SECT_SIZE);
              }
            } else {
	      if (ps2cdvd_send_read(16, 1, labelbuf, &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read() failed\n");
                res_state = ps2cdvd_enter(STAT_ERROR);
              }
            }
	  } else {
	  }
	  break;
	case STAT_LABEL_READ_ERROR_CHECK:
	  if (enter) {
	    if (ps2cdvd_send_geterror() < 0) {
	      DPRINT(DBG_DIAG, "send_geterror() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_READY:
	  if (enter) {
	    ps2cdvd_lowlevel_unlock();
	    SET_TIMER(HZ * ps2cdvd_check_interval);
	  } else {
	    RESET_TIMER();
	  }
	  break;
	case STAT_CHECK_TRAY:
	  if (enter) {
	    if (ps2cdvd_send_trayreq(2) < 0) {
	      DPRINT(DBG_DIAG, "send_trayreq() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_READ:
	  if (enter) {
	    DPRINT(DBG_READ, "REQ %p: sec=%ld  n=%ld  buf=%p\n",
		   CURRENT, CURRENT->sector,
		   CURRENT->current_nr_sectors, CURRENT->buffer);
            if (media_mode == SCECdDVDV) {
	      if (ps2cdvd_send_read_dvd(CURRENT->sector/4,
				    ps2cdvd_databuf_size,
				    ps2cdvd_databuf,
				    &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read_dvd() failed\n");
	        res_state = ps2cdvd_enter(STAT_ERROR);
	      } else {
	        ps2cdvd_databuf_addr = CURRENT->sector/4;
	      }
            } else {
	      if (ps2cdvd_send_read(CURRENT->sector/4,
				    ps2cdvd_databuf_size,
				    ps2cdvd_databuf,
				    &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read() failed\n");
	        res_state = ps2cdvd_enter(STAT_ERROR);
	      } else {
	        ps2cdvd_databuf_addr = CURRENT->sector/4;
	      }
            }
	  } else {
	  }
	  break;
	case STAT_READ_EOM_RETRY:
	  if (enter) {
	    DPRINT(DBG_READ, "REQ %p: sec=%ld  n=%ld  buf=%p (EOM retry)\n",
		   CURRENT, CURRENT->sector,
		   CURRENT->current_nr_sectors, CURRENT->buffer);
            if (media_mode == SCECdDVDV) {
	      if (ps2cdvd_send_read_dvd(CURRENT->sector/4 - ps2cdvd_databuf_size + 1,
				    ps2cdvd_databuf_size,
				    ps2cdvd_databuf,
				    &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read_dvd() failed\n");
	        res_state = ps2cdvd_enter(STAT_ERROR);
	      } else {
	        ps2cdvd_databuf_addr =
		  CURRENT->sector/4 - ps2cdvd_databuf_size + 1;
	      }
            } else {
	      if (ps2cdvd_send_read(CURRENT->sector/4 - ps2cdvd_databuf_size + 1,
				    ps2cdvd_databuf_size,
				    ps2cdvd_databuf,
				    &DataMode) < 0) {
	        DPRINT(DBG_DIAG, "ps2cdvd_send_read() failed\n");
	        res_state = ps2cdvd_enter(STAT_ERROR);
	      } else {
	        ps2cdvd_databuf_addr =
		  CURRENT->sector/4 - ps2cdvd_databuf_size + 1;
	      }
            }
	  } else {
	  }
	  break;
	case STAT_READ_ERROR_CHECK:
	  if (enter) {
	    if (ps2cdvd_send_geterror() < 0) {
	      DPRINT(DBG_DIAG, "send_geterror() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_INVALID_DISC:
	  if (enter) {
	    if (!disc_locked) {
	      DPRINT(DBG_DIAG, "attempt to enter INVALID_DISC state while !disc_locked\n");
	      res_state = ps2cdvd_enter(STAT_WAIT_DISC);
	    } else {
	      invalidate_discinfo();
	      ps2cdvd_lowlevel_unlock();
	      SET_TIMER(HZ * ps2cdvd_check_interval);
	    }
	  } else {
	    RESET_TIMER();
	  }
	  break;
	case STAT_SPINDOWN:
	  if (enter) {
	    if (ps2cdvd_send_stop() < 0) {
	      DPRINT(DBG_DIAG, "send_stop() failed\n");
	      res_state = ps2cdvd_enter(STAT_ERROR);
	    }
	  } else {
	  }
	  break;
	case STAT_IDLE:
	  if (enter) {
	    invalidate_discinfo();
	    ps2cdvd_lowlevel_unlock();
	  } else {
	    RESET_TIMER();
	  }
	  break;
	case STAT_ERROR:
	  if (enter) {
	    invalidate_discinfo();
	    while (CURRENT)
	      end_request(0);
	    ps2cdvd_lowlevel_unlock();
	  } else {
	  }
	  break;
	default:
	  printk(KERN_CRIT "ps2cdvd: INVALID STATE\n");
	  break;
	}

	return res_state;
}

static void
ps2cdvd_state_machine(struct ps2cdvd_event* ev)
{
	unsigned long flags;
	int old_state = ps2cdvd_state;
	int new_state = ps2cdvd_state;
	struct sbr_common_arg *carg = ev->arg;

	DPRINT(DBG_STATE, "state: %s event: %s\n",
	       ps2cdvd_getstatestr(old_state),
	       ps2cdvd_geteventstr(ev->type));

	save_flags(flags);
	cli();

	switch (ps2cdvd_state) {
	case STAT_WAIT_DISC:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    if (ps2cdvd_lowlevel_lock() < 0) {
	      /* waiting for unlock... */
	      RESET_TIMER();
	      SET_TIMER(HZ * ps2cdvd_check_interval);
	    } else {
	      new_state = STAT_INIT_TRAYSTAT;
	    }
	    break;
	  case EV_INTR:
	  }
	  break;
	case STAT_INIT_TRAYSTAT:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    {
	      if (carg->result != 0) {
		new_state = STAT_WAIT_DISC;
	      } else {
		new_state = STAT_CHECK_DISCTYPE;
	      }
	    }
	    break;
	  }
	  break;
	case STAT_CHECK_DISCTYPE:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    disc_type = carg->result;
	    DPRINT(DBG_INFO, "ps2cdvd_getdisctype()='%s', %d\n",
		   ps2cdvd_getdisctypestr(disc_type), disc_type);
	    switch (disc_type) {
	    case SCECdPS2CDDA:		/* PS2 CD DA */
	    case SCECdPS2CD:		/* PS2 CD */
	    case SCECdPSCDDA:		/* PS CD DA */
	    case SCECdPSCD:		/* PS CD */
	    case SCECdCDDA:		/* CD DA */
	    case SCECdPS2DVD:		/* PS2 DVD */
	    case SCECdDVDV:		/* DVD video */
	      new_state = STAT_INIT_CHECK_READY;
	      break;
	    case SCECdDETCTDVDD:	/* DVD-dual detecting */
	    case SCECdDETCTDVDS:	/* DVD-single detecting */
	    case SCECdDETCTCD:		/* CD detecting */
	    case SCECdDETCT:		/* detecting */
	    case SCECdNODISC:		/* no disc */
	      new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
	      break;
	    case SCECdIllgalMedia:	/* illegal media */
	    case SCECdUNKNOWN:		/* unknown */
	      printk(KERN_CRIT "ps2cdvd: illegal media\n");
	      new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
	      break;
	    }
	    break;
	  }
	  break;
	case STAT_INIT_CHECK_READY:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    prev_read = jiffies;
	    if (carg->result == SCECdComplete) {
	      switch (disc_type) {
	      case SCECdPS2CDDA:	/* PS2 CD DA */
	      case SCECdPS2CD:		/* PS2 CD */
	      case SCECdPSCDDA:		/* PS CD DA */
	      case SCECdPSCD:		/* PS CD */
	      case SCECdCDDA:		/* CD DA */
		new_state = STAT_TOC_READ;
		media_mode = SCECdCD;
		break;
	      case SCECdPS2DVD:		/* PS2 DVD */
		new_state = STAT_SET_MMODE;
		media_mode = SCECdDVD;
		break;
	      case SCECdDVDV:		/* DVD video */
		new_state = STAT_SET_MMODE;
		media_mode = SCECdDVDV;
		break;
	      default:
		printk(KERN_CRIT "ps2cdvd: internal error at %s(%d)\n",
		       __FILE__, __LINE__);
		new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
		break;
	      }
	    } else {
	      new_state = STAT_WAIT_DISC;
	    }
	    break;
	  }
	  break;
	case STAT_TOC_READ:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (carg->result < 0) {
	      DPRINT(DBG_DIAG, "gettoc() failed\n");
	      new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
	    } else {
	      struct ps2cdvd_tocentry *toc;

	      toc_valid = 1;
	      toc = (struct ps2cdvd_tocentry *)tocbuf;
	      leadout_start = msftolba(decode_bcd(toc[2].abs_msf[0]),
				       decode_bcd(toc[2].abs_msf[1]),
				       decode_bcd(toc[2].abs_msf[2]));
#ifdef PS2CDVD_DEBUG
	      if (ps2cdvd_debug & DBG_INFO) {
		struct sbr_cdvd_gettoc_arg *arg = carg->arg;
		if (arg->media == 0) {
		  ps2cdvd_tocdump(DBG_LOG_LEVEL "ps2cdvd: ",
				  (struct ps2cdvd_tocentry *)tocbuf);
		} else {
		  /*
		   * we have no interrest in DVD Physical format information
		   ps2cdvd_hexdump(tocbuf, len);
		  */
		}
	      }
#endif
	      new_state = STAT_SET_MMODE;
	    }
	    break;
	  }
	  break;
	case STAT_SET_MMODE:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (carg->result == 0) {
	      DPRINT(DBG_DIAG, "set_mmode() failed\n");
	      new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
	    } else {
	      switch (disc_type) {
	      case SCECdPS2DVD:		/* PS2 DVD */
	      case SCECdPS2CDDA:		/* PS2 CD DA */
	      case SCECdPS2CD:		/* PS2 CD */
	      case SCECdPSCDDA:		/* PS CD DA */
	      case SCECdPSCD:		/* PS CD */
		new_state = STAT_LABEL_READ;
		break;
	      case SCECdDVDV:		/* DVD video */
	      case SCECdCDDA:		/* CD DA */
		if (disc_locked && disc_lock_key_valid) {
		  new_state = STAT_LABEL_READ;
		} else {
		  disc_changed++;
		  new_state = STAT_READY;
		}
		break;
	      default:
		printk(KERN_CRIT "ps2cdvd: internal error at %s(%d)\n",
		       __FILE__, __LINE__);
		new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
		break;
	      }
	      break;
	    }
	  }
	  break;
	case STAT_LABEL_READ:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    new_state = STAT_LABEL_READ_ERROR_CHECK;
	    break;
	  }
	  break;
	case STAT_LABEL_READ_ERROR_CHECK:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (carg->result != SCECdErNO) {
	      DPRINT(DBG_DIAG, "error: %s, code=0x%02x\n",
		     ps2cdvd_geterrorstr(carg->result),
		     carg->result);
	      if (disc_locked && disc_lock_key_valid) {
		printk(KERN_CRIT "ps2cdvd: =============================\n");
		printk(KERN_CRIT "ps2cdvd:          wrong disc.         \n");
		printk(KERN_CRIT "ps2cdvd: =============================\n");
		if (!ps2cdvd_wrong_disc_retry) {
		  INIT_REQUEST;
		  while (CURRENT)
		    end_request(0);
		  disc_changed++;
		}
	      }
	      new_state = disc_locked ? STAT_INVALID_DISC : STAT_WAIT_DISC;
	    } else {
	      unsigned long sum;
#ifdef PS2CDVD_DEBUG
	      struct iso_primary_descriptor *label;
	      label = (struct iso_primary_descriptor*)labelbuf;

	      if (ps2cdvd_debug & DBG_INFO) {
		printk(DBG_LOG_LEVEL "ps2cdvd: ");
		print_isofsstr(label->system_id, sizeof(label->system_id));
		print_isofsstr(label->volume_id, sizeof(label->volume_id));
		print_isofsstr(label->volume_set_id,
			       sizeof(label->volume_set_id));
		print_isofsstr(label->publisher_id,
			       sizeof(label->publisher_id));
		print_isofsstr(label->application_id,
			       sizeof(label->application_id));
		printk("\n");

		/* ps2cdvd_hexdump(DBG_LOG_LEVEL "ps2cdvd: ", labelbuf, 2048);
		 */
	      }
#endif
	      label_valid = 1;
	      DPRINT(DBG_DLOCK, "label is valid\n");
	      sum = checksum((u_long*)labelbuf, 2048/sizeof(u_long));
	      if (disc_lock_key_valid &&
		  disc_locked &&
		  disc_lock_key != sum) {
		printk(KERN_CRIT "ps2cdvd: =============================\n");
		printk(KERN_CRIT "ps2cdvd:          wrong disc.         \n");
		printk(KERN_CRIT "ps2cdvd: =============================\n");
		if (!ps2cdvd_wrong_disc_retry) {
		  INIT_REQUEST;
		  while (CURRENT)
		    end_request(0);
		  disc_changed++;
		}
		new_state = STAT_INVALID_DISC;
	      } else {
		disc_lock_key = sum;
		if (!disc_lock_key_valid && disc_locked) {
		  DPRINT(DBG_DLOCK, "disc lock key=%lX\n", sum);
		}
		disc_lock_key_valid = 1;
		new_state = STAT_READY;
	      }
	    }
	    break;
	  }
	  break;
	case STAT_READY:
	  switch (ev->type) {
	  case EV_TICK:
	    if (CURRENT == NULL || ps2sif_iswaiting(ps2cdvd_lock)) {
	      break;
	    }
	    /* fall through */
	  case EV_START:
	  case EV_TIMEOUT:
	    if (ps2cdvd_lowlevel_lock() < 0) {
	      /* waiting for unlock... */
	      RESET_TIMER();
	      SET_TIMER(HZ * ps2cdvd_check_interval);
	      break;
	    }
	    if (CURRENT == NULL) {
	      if (ps2cdvd_spindown * HZ < jiffies - prev_read) {
		new_state = STAT_SPINDOWN;
	      } else {
		/* nothing to do */
		ps2cdvd_lowlevel_unlock();
		RESET_TIMER();
		SET_TIMER(HZ * ps2cdvd_check_interval);
	      }
	    } else {
	      prev_read = jiffies;
	      if (jiffies - prev_tray_check < HZ/2) {
		new_state = STAT_READ;
	      } else {
		prev_tray_check = jiffies;
		new_state = STAT_CHECK_TRAY;
	      }
	    }
	    break;
	  case EV_INTR:
	    break;
	  }
	  break;
	case STAT_CHECK_TRAY:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (carg->result < 0) {
	      new_state = STAT_ERROR;
	    } else {
	      struct sbr_cdvd_trayreq_arg *arg = carg->arg;
	      if (arg->traycount != 0) {
		if (disc_locked) {
		  printk(KERN_CRIT"ps2cdvd: =============================\n");
		  printk(KERN_CRIT"ps2cdvd: the disc is currently locked.\n");
		  printk(KERN_CRIT"ps2cdvd: please don't take it away!\n");
		  printk(KERN_CRIT"ps2cdvd: =============================\n");
		}

		invalidate_discinfo();

		new_state = STAT_CHECK_DISCTYPE;
	      } else {
		if (CURRENT) {
		  new_state = STAT_READ;
		} else {
		  new_state = STAT_READY;
		}
	      }
	    }
	    break;
	  }
	  break;
	case STAT_READ:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    new_state = STAT_READ_ERROR_CHECK;
	    break;
	  }
	  break;
	case STAT_READ_EOM_RETRY:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    new_state = STAT_READ_ERROR_CHECK;
	    break;
	  }
	  break;
	case STAT_READ_ERROR_CHECK:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (carg->result == SCECdErNO) {
	      /*
	       * succeeded
	       */
	      while (CURRENT != NULL &&
		     ps2cdvd_databuf_addr <= CURRENT->sector/4 &&
		     CURRENT->sector/4 < ps2cdvd_databuf_addr + ps2cdvd_databuf_size) {
                if (media_mode == SCECdDVDV) {
                  memcpy(CURRENT->buffer,
                         ps2cdvd_databuf + DVD_DATA_OFFSET + DVD_DATA_SECT_SIZE * (CURRENT->sector/4 - ps2cdvd_databuf_addr),
                         DATA_SECT_SIZE);
	        } else {
                  memcpy(CURRENT->buffer,
		         ps2cdvd_databuf + DATA_SECT_SIZE * (CURRENT->sector/4 - ps2cdvd_databuf_addr),
		         DATA_SECT_SIZE);
	        }
		end_request(1);
	      }
	      if (!ps2sif_iswaiting(ps2cdvd_lock) && CURRENT != NULL) {
		/* tiny acceleration */
		new_state = STAT_READ;
	      } else {
		new_state = STAT_READY;
	      }
	    } else
	    if (carg->result == 0x38) {
	      /*
	       * sector format error
	       */
	      DPRINT(DBG_DIAG,
		     "error: sector format error, code=0x38 (ignored)\n");
	      memset(CURRENT->buffer, 0, DATA_SECT_SIZE);
	      end_request(1);
	      if (!ps2sif_iswaiting(ps2cdvd_lock) && CURRENT != NULL) {
		/* tiny acceleration */
		new_state = STAT_READ;
	      } else {
		new_state = STAT_READY;
	      }
	    } else
	    if (carg->result == SCECdErEOM &&
		ps2cdvd_databuf_addr != 
			CURRENT->sector/4 - ps2cdvd_databuf_size + 1 &&
		ps2cdvd_databuf_size <= CURRENT->sector/4) {
	      /* you got End Of Media and you have not retried */
	      DPRINT(DBG_DIAG, "error: %s, code=0x%02x (retry...)\n",
		     ps2cdvd_geterrorstr(carg->result),
		     carg->result);
	      new_state = STAT_READ_EOM_RETRY;
	    } else {
	      DPRINT(DBG_DIAG, "error: %s, code=0x%02x\n",
		     ps2cdvd_geterrorstr(carg->result),
		     carg->result);
	      ps2cdvd_databuf_addr = -1;
	      end_request(0);		/* I/O error */
	      new_state = STAT_READY;
	    }
	    break;
	  }
	  break;
	case STAT_INVALID_DISC:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    if (ps2cdvd_lowlevel_lock() < 0) {
	      /* waiting for unlock... */
	      RESET_TIMER();
	      SET_TIMER(HZ * ps2cdvd_check_interval);
	    } else {
	      new_state = STAT_CHECK_DISCTYPE;
	    }
	    break;
	  case EV_INTR:
	    break;
	  }
	  break;
	case STAT_SPINDOWN:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    break;
	  case EV_INTR:
	    if (CURRENT == NULL) {
	      new_state = STAT_IDLE;
	    } else {
	      DPRINT(DBG_VERBOSE, "re-spinup...\n");
	      new_state = STAT_CHECK_DISCTYPE;
	    }
	    break;
	  }
	  break;
	case STAT_IDLE:
	  switch (ev->type) {
	  case EV_START:
	  case EV_TIMEOUT:
	    if (ps2cdvd_lowlevel_lock() < 0) {
	      /* waiting for unlock... */
	      RESET_TIMER();
	      SET_TIMER(HZ * ps2cdvd_check_interval);
	    } else {
	      new_state = STAT_CHECK_DISCTYPE;
	    }
	    break;
	  case EV_INTR:
	    break;
	  }
	  break;
	case STAT_ERROR:
	  break;
	default:
	  printk(KERN_ERR "ps2cdvd: invalid state");
	  ps2cdvd_state = STAT_WAIT_DISC;
	  break;
	}

	if (new_state != old_state) {
		struct ps2cdvd_event tick;
		tick.type = EV_TICK;
		ps2cdvd_leave(old_state);
		ps2cdvd_state = ps2cdvd_enter(new_state);
		DPRINT(DBG_STATE, "  -> new state: %s\n",
		       ps2cdvd_getstatestr(ps2cdvd_state));
		if (old_state != ps2cdvd_state &&
		    ps2cdvd_state == STAT_READY) {
			ps2cdvd_state_machine(&tick);
		}
		wake_up_interruptible(&statq);
	}

	restore_flags(flags);
}

__initfunc(int ps2cdvd_init(void))
{
	int res;
	unsigned long flags;

	DPRINT(DBG_VERBOSE, "init: get lock\n");
	if ((ps2cdvd_lock = ps2sif_getlock(PS2LOCK_CDVD)) == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't get lock\n");
		ps2cdvd_cleanup();
		return -EINVAL;
	}
	ps2sif_lockqueueinit(&ps2cdvd_lock_qi);
	ps2cdvd_lock_qi.name = "ps2cdvd";

	DPRINT(DBG_VERBOSE, "init: initialize timer\n");
	init_timer(&io_timer);
	io_timer.function = (void(*)(u_long))ps2cdvd_timer;
	ps2cdvd_state = STAT_WAIT_DISC;

	DPRINT(DBG_VERBOSE, "init: allocate diaklabel buffer\n");
	labelbuf = kmalloc(DVD_DATA_SECT_SIZE, GFP_KERNEL);
	if (labelbuf == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't allocate buffer\n");
		ps2cdvd_cleanup();
		return -ENOMEM;
	}
	initialized |= INIT_LABELBUF;

	DPRINT(DBG_VERBOSE, "allocate buffer\n");
	ps2cdvd_databufx = kmalloc(ps2cdvd_databuf_size * AUDIO_SECT_SIZE +
				   BUFFER_ALIGNMENT, GFP_KERNEL);
	if (ps2cdvd_databufx == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't allocate buffer\n");
		ps2cdvd_cleanup();
		return -ENOMEM;
	}
	ps2cdvd_databuf = ALIGN(ps2cdvd_databufx, BUFFER_ALIGNMENT);
	initialized |= INIT_DATABUF;

	DPRINT(DBG_VERBOSE, "init: call sbios\n");
	if (ps2cdvdcall_init()) {
		printk(KERN_ERR "ps2cdvd: Can't initialize CD/DVD-ROM subsystem\n");
		ps2cdvd_cleanup();
		return -EIO;
	}
#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (0x0201 <= sbios(SB_GETVER, NULL))
		ps2cdvdcall_reset();
#else
	ps2cdvdcall_reset();
#endif
	initialized |= INIT_IOPSIDE;

	DPRINT(DBG_VERBOSE, "init: register block device\n");
	if ((res = register_blkdev(MAJOR_NR, "ps2cdvd", &cdrom_fops)) < 0) {
		printk(KERN_ERR "ps2cdvd: Unable to get major %d for PS2 CD/DVD-ROM\n",
		       MAJOR_NR);
		ps2cdvd_cleanup();
                return -EIO;
	}
	if (MAJOR_NR == 0) MAJOR_NR = res;
	initialized |= INIT_BLKDEV;

	blksize_size[MAJOR_NR] = ps2cdvd_blocksizes;
	hardsect_size[MAJOR_NR] = ps2cdvd_hardsectsizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = ps2cdvd_read_ahead;

	DPRINT(DBG_VERBOSE, "init: register cdrom\n");
	ps2cdvd_info.dev = MKDEV(MAJOR_NR, 0);
        if (register_cdrom(&ps2cdvd_info) != 0) {
		printk(KERN_ERR "ps2cdvd: Cannot register PS2 CD/DVD-ROM\n");
		ps2cdvd_cleanup();
		return -EIO;
        }
	initialized |= INIT_CDROM;

	printk(KERN_INFO "PS2 CD/DVD-ROM driver\n");

	save_flags(flags);
	cli();
	if (ps2cdvd_lowlevel_lock() == 0)
	  ps2cdvd_state = ps2cdvd_enter(STAT_INIT_TRAYSTAT);
	else
	  ps2cdvd_state = ps2cdvd_enter(STAT_WAIT_DISC);
	restore_flags(flags);

	return 0;
}

static int
ps2cdvd_open(struct cdrom_device_info * cdi, int purpose)
{
        MOD_INC_USE_COUNT;

	DPRINT(DBG_INFO, "open\n");
	return 0;
}

void
ps2cdvd_hexdump(char *header, unsigned char *data, int len)
{
	int i;
	char *hex = "0123456789abcdef";
	char line[70];

	for (i = 0; i < len; i++) {
		int o = i % 16;
		if (o == 0) {
			memset(line, ' ', sizeof(line));
			line[sizeof(line) - 1] = '\0';
		}
		line[o * 3 + 0] = hex[(data[i] & 0xf0) >> 4];
		line[o * 3 + 1] = hex[(data[i] & 0x0f) >> 0];
		if (0x20 <= data[i] && data[i] < 0x7f) {
			line[o + 50] = data[i];
		} else {
			line[o + 50] = '.';
		}
		if (o == 15) {
			printk("%s%s\n", header, line);
		}
	}
}

void
ps2cdvd_tocdump(char *header, struct ps2cdvd_tocentry *tocents)
{
  int i, startno, endno;

  startno = decode_bcd(tocents[0].abs_msf[0]);
  endno = decode_bcd(tocents[1].abs_msf[0]);
  printk("%strack: %d-%d  lead out: %02d:%02d:%02d\n",
	 header, startno, endno, 
	 decode_bcd(tocents[2].abs_msf[0]),
	 decode_bcd(tocents[2].abs_msf[1]),
	 decode_bcd(tocents[2].abs_msf[2]));
  tocents += 2;
  printk("%saddr/ctrl track index min/sec/frame\n", header);
  for (i = startno; i <= endno; i++) {
    printk("%s   %x/%x    %02d    %02d    %02d:%02d:%02d\n",
	   header,
	   tocents[i].addr,
	   tocents[i].ctrl,
	   decode_bcd(tocents[i].trackno),
	   decode_bcd(tocents[i].indexno),
	   decode_bcd(tocents[i].abs_msf[0]),
	   decode_bcd(tocents[i].abs_msf[1]),
	   decode_bcd(tocents[i].abs_msf[2]));
  }
}

static void
ps2cdvd_release(struct cdrom_device_info * cdi)
{
	DPRINT(DBG_INFO, "release\n");
	MOD_DEC_USE_COUNT;
}

static int
ps2cdvd_media_changed(struct cdrom_device_info * cdi, int disc_nr)
{
	int res;
	res = disc_changed ? 1 : 0;
	disc_changed = 0;
	return res;
}

static int 
ps2cdvd_tray_move(struct cdrom_device_info * cdi, int position)
{
	if (position) {
		/* open */
		DPRINT(DBG_INFO, "tray open request\n");
		return ps2cdvdcall_trayreq(SCECdTrayOpen, NULL);
	} else {
		/* close */
		DPRINT(DBG_INFO, "tray close request\n");
		return ps2cdvdcall_trayreq(SCECdTrayClose, NULL);
	}
}

static int
ps2cdvd_drive_status(struct cdrom_device_info *cdi, int arg)
{
	int res;

	/* spinup and re-check disc if the state is in IDLE */
	ps2cdvd_ready();

	if (ps2sif_lock(ps2cdvd_lock, "cdvd_drive_status") != 0)
		return CDS_NO_INFO;

	switch (ps2cdvd_state) {
	case STAT_WAIT_DISC:
	case STAT_INVALID_DISC:
		DPRINT(DBG_INFO, "drive_status=NO_DISC\n");
		res = CDS_NO_DISC;
		break;

	case STAT_READY:
	case STAT_READ:
	case STAT_READ_ERROR_CHECK:
		DPRINT(DBG_INFO, "drive_status=DISC_OK\n");
		res = CDS_DISC_OK;
		break;

	case STAT_ERROR:
	default:
		DPRINT(DBG_INFO, "drive_status=NO_INFO\n");
		res = CDS_NO_INFO;
		break;
	}
	ps2sif_unlock(ps2cdvd_lock);

	return res;
}

static int
ps2cdvd_lock_door(struct cdrom_device_info *cdi, int lock)
{
	if (lock) {
		disc_locked = 1;
		DPRINT(DBG_DLOCK, "disc is locked\n");
		if (label_valid) {
		  disc_lock_key = checksum((u_long*)labelbuf,
					   2048/sizeof(u_long));
		  disc_lock_key_valid = 1;
		  DPRINT(DBG_DLOCK, "disc lock key=%lX\n", disc_lock_key);
		} else {
		  disc_lock_key_valid = 0;
		  DPRINT(DBG_DLOCK, "disc lock key=****\n");
		}
	} else {
		/* unlock */
		disc_locked = 0;
	}
	return 0;
}

static int
ps2cdvd_select_speed(struct cdrom_device_info *cdi, int speed)
{
	int ps2_speed;

	if (speed == 0) {
	  DPRINT(DBG_INFO, "select speed, Normal\n");
	  ps2_speed = SCECdSpinNom;
	} else if (speed == 1) {
	  DPRINT(DBG_INFO, "select speed, x1\n");
	  ps2_speed = SCECdSpinX1;
	} else if (2 <= speed && speed < 4) {
	  DPRINT(DBG_INFO, "select speed, x2\n");
	  ps2_speed = SCECdSpinX2;
	} else if (4 <= speed && speed < 8) {
	  DPRINT(DBG_INFO, "select speed, x4\n");
	  ps2_speed = SCECdSpinX4;
	} else if (8 <= speed && speed <= 12) {
	  DPRINT(DBG_INFO, "select speed, x12\n");
	  ps2_speed = SCECdSpinX12;
	} else if (12 < speed && speed) {
	  DPRINT(DBG_INFO, "select speed, Max\n");
	  ps2_speed = SCECdSpinMx;
	} else {
	  ps2_speed = SCECdSpinNom;
	}

	DataMode.spindlctrl = ps2_speed;
	CDDAMode.spindlctrl = ps2_speed;

	return 0;
}

static int
ps2cdvd_reset(struct cdrom_device_info *cdi)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	ps2sif_lock(ps2cdvd_lock, "cdvd_reset");
	RESET_TIMER();
#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (0x0201 <= sbios(SB_GETVER, NULL))
		ps2cdvdcall_reset();
#else
	ps2cdvdcall_reset();
#endif
	invalidate_discinfo();
	ps2cdvd_state = ps2cdvd_enter(STAT_WAIT_DISC);
	ps2sif_unlock(ps2cdvd_lock);
	restore_flags(flags);
	return 0;
}

static int
ps2cdvd_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void *arg)
{
   int res;

   switch (cmd) {
   case CDROMSTART:     /* Spin up the drive */
   case CDROMSTOP:      /* Spin down the drive */
   case CDROMPAUSE:     /* Pause the drive */
     return 0;	/* just ignore it */
     break;

   case CDROMRESUME:    /* Start the drive after being paused */
   case CDROMPLAYMSF:   /* Play starting at the given MSF address. */
     return -EINVAL;
     break;

   case CDROMREADTOCHDR:        /* Read the table of contents header */
      {
         struct cdrom_tochdr *hdr;
	 struct ps2cdvd_tocentry *toc = (struct ps2cdvd_tocentry *)tocbuf;
         
	 if ((res = ps2cdvd_ready()) != 0)
	   return (res);

	 if (!toc_valid) {
	   DPRINT(DBG_VERBOSE, "TOC is not valid\n");
	   return -EIO;
	 }
         
         hdr = (struct cdrom_tochdr *) arg;
         hdr->cdth_trk0 = decode_bcd(toc[0].abs_msf[0]);
         hdr->cdth_trk1 = decode_bcd(toc[1].abs_msf[0]);
      }
      return 0;

   case CDROMREADTOCENTRY:      /* Read a given table of contents entry */
      {
	 struct ps2cdvd_tocentry *toc = (struct ps2cdvd_tocentry *)tocbuf;
         struct cdrom_tocentry *entry;
         int idx;
         
	 if ((res = ps2cdvd_ready()) != 0)
	   return (res);

	 if (!toc_valid) {
	   DPRINT(DBG_VERBOSE, "TOC is not valid\n");
	   return -EIO;
	 }
         
         entry = (struct cdrom_tocentry *) arg;
         
	 if (entry->cdte_track == CDROM_LEADOUT)
	   entry->cdte_track = 102;
	 for (idx = 0; idx <= 102; idx++) {
	   if (decode_bcd(toc[idx].indexno) == entry->cdte_track) break;
	 }
	 if (102 < idx) {
	   DPRINT(DBG_DIAG, "Can't find track %d(0x%02x)\n",
		  entry->cdte_track, entry->cdte_track);
	   return -EINVAL;
	 }

         entry->cdte_adr = toc[idx].addr;
         entry->cdte_ctrl = toc[idx].ctrl;
         
         /* Logical buffer address or MSF format requested? */
         if (entry->cdte_format == CDROM_LBA) {
            entry->cdte_addr.lba = msftolba(toc[idx].abs_msf[0],
					    toc[idx].abs_msf[1],
					    toc[idx].abs_msf[2]);
         } else
	 if (entry->cdte_format == CDROM_MSF) {
	   entry->cdte_addr.msf.minute = decode_bcd(toc[idx].abs_msf[0]);
	   entry->cdte_addr.msf.second = decode_bcd(toc[idx].abs_msf[1]);
	   entry->cdte_addr.msf.frame = decode_bcd(toc[idx].abs_msf[2]);
         } else
	   return -EINVAL;
      }
      return 0;
      break;

   case CDROMPLAYTRKIND:     /* Play a track.  This currently ignores index. */
   case CDROMVOLCTRL:   /* Volume control.  What volume does this change, anyway? */
   case CDROMSUBCHNL:   /* Get subchannel info */
   default:
      return -EINVAL;
   }
}

static int
ps2cdvd_dev_ioctl(struct cdrom_device_info *cdi,
		  unsigned int  cmd,
		  unsigned long arg)
{

	switch (cmd) {
	}

	return -EINVAL;
}

static void
ps2cdvd_cleanup()
{

	DPRINT(DBG_VERBOSE, "cleanup\n");

	ps2sif_lock(ps2cdvd_lock, "cdvd_cleanup");
	RESET_TIMER();

	if (initialized & INIT_LABELBUF) {
		DPRINT(DBG_VERBOSE, "free labelbuf %p\n", labelbuf);
		kfree(labelbuf);
	}

	if (initialized & INIT_DATABUF) {
		DPRINT(DBG_VERBOSE, "free databuf %p\n", ps2cdvd_databufx);
		kfree(ps2cdvd_databufx);
	}

#if 0
	if (initialized & INIT_IOPSIDE) 
		ps2cdvd_cleanupiop();
#endif

	if (initialized & INIT_BLKDEV) {
		DPRINT(DBG_VERBOSE, "unregister block device\n");
		unregister_blkdev(MAJOR_NR, "ps2cdvd");
	}

	if (initialized & INIT_CDROM) {
		DPRINT(DBG_VERBOSE, "unregister cdrom\n");
		unregister_cdrom(&ps2cdvd_info);
	}

	blksize_size[MAJOR_NR] = NULL;

	initialized = 0;
	ps2sif_unlock(ps2cdvd_lock);
}

#ifdef MODULE
int
init_module(void)
{
	DPRINT(DBG_INFO, "load\n");
	return ps2cdvd_init();
}

void
cleanup_module(void)
{
	DPRINT(DBG_INFO, "unload\n");
	ps2cdvdcall_stop();
	ps2cdvd_cleanup();
}
#endif /* MODULE */
