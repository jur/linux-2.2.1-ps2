/* $Id: time.c,v 1.37.4.1 2001/08/21 06:21:50 takemura Exp $
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Copyright (C) 1996, 1997, 1998  Ralf Baechle
 *  Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file contains the time handling details for PC-style clocks as
 * found in some MIPS systems.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ps2/irq.h>
#include <asm/ps2/bootinfo.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

extern volatile unsigned long lost_ticks;

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)

#define CPU_FREQ  294912000		/* CPU clock frequency (Hz) */
#define BUS_CLOCK (CPU_FREQ/2)		/* bus clock frequency (Hz) */
#define TM0_COMP  (BUS_CLOCK/256/HZ)	/* to generate 100Hz */

static volatile int *tm0_count = (volatile int *)0xb0000000;
static volatile int *tm0_mode  = (volatile int *)0xb0000010;
static volatile int *tm0_comp  = (volatile int *)0xb0000020;

static unsigned int last_cycle_count;
static int timer_intr_delay;

static unsigned long do_gettimeoffset(void)
{
    unsigned int count;
    int delay;

    count = read_32bit_cp0_register(CP0_COUNT);
    count -= last_cycle_count;
    count = (count * 1000 + (CPU_FREQ / 1000 / 2)) / (CPU_FREQ / 1000);
    delay = (timer_intr_delay * 10000 + (TM0_COMP / 2)) / TM0_COMP;
    return delay + count;
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_and_cli(flags);
	*tv = xtime;
	tv->tv_usec += do_gettimeoffset();

	/*
	 * xtime is atomically updated in timer_bh. lost_ticks is
	 * nonzero if the timer bottom half hasnt executed yet.
	 */
	if (lost_ticks)
		tv->tv_usec += USECS_PER_JIFFY;

	restore_flags(flags);

	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	/* This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	tv->tv_usec -= do_gettimeoffset();

	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;	/* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	sti();
}

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you won't notice until after reboot!
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		BCD_TO_BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		}
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
 		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	return retval;
}

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
asmlinkage void
timer_interrupt(struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	last_cycle_count = read_32bit_cp0_register(CP0_COUNT);
	timer_intr_delay = *tm0_count;
	*tm0_mode = *tm0_mode;	/* clear interrupt */

	hardirq_enter(cpu);
	kstat.irqs[0][IRQ_INTC_TIMER0]++;

#ifdef CONFIG_PROFILE
	if(!user_mode(regs)) {
		if (prof_buffer && current->pid) {
			extern int _stext;
			unsigned long pc = regs->cp0_epc;

			pc -= (unsigned long) &_stext;
			pc >>= prof_shift;
			/*
			 * Dont ignore out-of-bounds pc values silently,
			 * put them into the last histogram slot, so if
			 * present, they will show up as a sharp peak.
			 */
			if (pc > prof_len-1)
				pc = prof_len-1;
			atomic_inc((atomic_t *)&prof_buffer[pc]);
		}
	}
#endif
	do_timer(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 500000 - ((unsigned) tick) / 2 &&
	    xtime.tv_usec <= 500000 + ((unsigned) tick) / 2) {
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

	/* As we return to user mode fire off the other CPU schedulers.. this is 
	   basically because we don't yet share IRQ's around. This message is
	   rigged to be safe on the 386 - basically it's a hack, so don't look
	   closely for now.. */
	/*smp_message_pass(MSG_ALL_BUT_SELF, MSG_RESCHEDULE, 0L, 0); */


#ifdef CONFIG_T10000_DEBUG_HOOK
	{
		void ps2sio_debug_check(void);
		ps2sio_debug_check();
	}
#endif
	hardirq_exit(cpu);
}

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

/*
 * mkdate is the reverse function of the mktime.
 * XXX, fix me.
 */
#define LEAPYEARS(y) ((y)/4 - (y)/100 + (y)/400)
void
mkdate(unsigned long elapse, unsigned int *year, unsigned int *mon,
	unsigned int *day, unsigned int *hour,
	unsigned int *min, unsigned int *sec, unsigned int *dayofweek)
{
	long y, m;
	long leap;
	static long long days[2][13] = {
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 },
	};

	if (sec) *sec = (elapse % 60);
	elapse /= 60;
	if (min) *min = (elapse % 60);
	elapse /= 60;
	if (hour) *hour = (elapse % 24);
	elapse /= 24;

	elapse += 1969 * 365 + LEAPYEARS(1969);
	if (dayofweek) *dayofweek = (elapse + 6) % 7;

	y = elapse * 400 / (100 - 4 + 1 + 365*400) + 1;
	elapse -= ((y-1)*365 + LEAPYEARS(y-1));
	leap = ((y%4 == 0 && y%100 != 0) || y%400 == 0) ? 1 : 0;
	if (365 + leap <= elapse) {
		elapse -= 365 + leap;
		y++;
		leap = ((y%4 == 0 && y%100 != 0) || y%400 == 0) ? 1 : 0;
	}
	if (year) *year = y;
	m = elapse / 31 + 1;
	if (days[leap][m] <= elapse) m++;
	if (mon) *mon = m;
	elapse -= days[leap][m - 1];

	if (day) *day = elapse + 1;
}

#define ALLINTS (IE_IRQ0 | IE_IRQ1)

#ifdef CONFIG_T10000_AIFRTC
extern int ps2_aif_probe;
#endif

__initfunc(void time_init(void))
{
	unsigned int year, mon, day, hour, min, sec;

#ifdef CONFIG_T10000_AIFRTC
	if (ps2_aif_probe) {
	    unsigned int epoch, cent;
	    int i;
	    unsigned char save_freq_select;

	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	/* read RTC exactly on falling edge of update flag */
	for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);

		/* select bank 1 and read century */
		save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
		CMOS_WRITE((save_freq_select|0x10), RTC_FREQ_SELECT);
		cent = CMOS_READ(0x48);		/* RTC_CENTURY */
		CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	} while (sec != CMOS_READ(RTC_SECONDS));
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	  {
	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);
	    BCD_TO_BIN(cent);
	  }
	year = year + cent * 100;

	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
	/* !ps2_aif_probe */
	} else
#endif /* CONFIG_T10000_AIFRTC */
	{
	    struct ps2_rtc *rtcp = &ps2_bootinfo->boot_time;

	    year = 2000 + BCD_TO_BIN(rtcp->year);
	    mon = BCD_TO_BIN(rtcp->mon);
	    day = BCD_TO_BIN(rtcp->day);
	    hour = BCD_TO_BIN(rtcp->hour);
	    min = BCD_TO_BIN(rtcp->min);
	    sec = BCD_TO_BIN(rtcp->sec);

	    /* convert JST(UTC-9) to UTC */
	    xtime.tv_sec = mktime(year, mon, day, hour, min, sec) - 60*60*9;
	    xtime.tv_usec = 0;
	}

	/* setup 100Hz interval timer */
	*tm0_count = 0;
	*tm0_comp = TM0_COMP;
	/* busclk / 256, zret, cue, cmpe, equf */
	*tm0_mode = 2 | (1 << 6) | ( 1 << 7) | (1 << 8) | (1 << 10);

	set_cp0_status(ST0_IM, ALLINTS);
	sti();
}
