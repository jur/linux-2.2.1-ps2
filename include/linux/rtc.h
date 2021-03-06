/* $Id: rtc.h,v 1.2 1998/08/25 09:23:04 ralf Exp $
 *
 * Interface definitions for the /dev/rtc realtime clock interface.
 *
 * permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */
#ifndef _LINUX_RTC_H
#define _LINUX_RTC_H

/*
 * The struct used to pass data via the following ioctl. Similar to the
 * struct tm in <time.h>, but it needs to be here so that the kernel 
 * source is self contained, allowing cross-compiles, etc. etc.
 */

struct rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/*
 * ioctl calls that are permitted to the /dev/rtc interface, if 
 * CONFIG_RTC or CONFIG_SGI_DS1286 are enabled.  The interface definitions
 * in this file are a superset from the features provided by actual
 * RTC driver and chip implementations.
 */
#define RTC_AIE_ON	_IO('p', 0x01)	/* Alarm int. enable on		*/
#define RTC_AIE_OFF	_IO('p', 0x02)	/* ... off			*/
#define RTC_UIE_ON	_IO('p', 0x03)	/* Update int. enable on	*/
#define RTC_UIE_OFF	_IO('p', 0x04)	/* ... off			*/
#define RTC_PIE_ON	_IO('p', 0x05)	/* Periodic int. enable on	*/
#define RTC_PIE_OFF	_IO('p', 0x06)	/* ... off			*/
#define RTC_WIE_ON	_IO('p', 0x0f)	/* Watchdog int. enable on	*/
#define RTC_WIE_OFF	_IO('p', 0x10)	/* ... off			*/

#define RTC_ALM_SET	_IOW('p', 0x07, struct rtc_time) /* Set alarm time  */
#define RTC_ALM_READ	_IOR('p', 0x08, struct rtc_time) /* Read alarm time */
#define RTC_RD_TIME	_IOR('p', 0x09, struct rtc_time) /* Read RTC time   */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct rtc_time) /* Set RTC time    */
#define RTC_IRQP_READ	_IOR('p', 0x0b, unsigned long)	 /* Read IRQ rate   */
#define RTC_IRQP_SET	_IOW('p', 0x0c, unsigned long)	 /* Set IRQ rate    */
#define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch      */
#define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch       */

#endif /* _LINUX_RTC_H */
