/*
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: debuglog.c,v 1.1 2000/09/25 12:22:33 takemura Exp $
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <asm/ps2/debuglog.h>

#ifdef MODULE
MODULE_PARM(debuglog_size, "i");
EXPORT_SYMBOL(debuglog_init);
EXPORT_SYMBOL(debuglog_free);
EXPORT_SYMBOL(debuglog);
EXPORT_SYMBOL(debuglog_flush);
#endif

struct debuglog default_log;
int debuglog_size = 10000;
static int debuglog_inited = 0;

static struct debuglog*
getdefaultlog(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dl->lock, flags);
	if (!debuglog_inited) {
		debuglog_init(&default_log, NULL, debuglog_size);
		debuglog_inited = 1;
	}
	spin_unlock_irqrestore(&dl->lock, flags);

	return (&default_log);
}

int
debuglog_init(struct debuglog *dl, char* buf, int size)
{
	dl->size = size;
	dl->count = 0;
	dl->head = 0;
	dl->tail = 0;
	dl->truncated = 0;
	dl->newline = 1;
	dl->buf = kmalloc(size, GFP_KERNEL);
	spin_lock_init(dl->lock);
	if (dl->buf == NULL) {
		dl->size = 0;
		return (-1);
	}

	return (0);
}

int
debuglog_free(struct debuglog *dl)
{
	if (dl != NULL) {
		dl->size = 0;
		if (dl->buf)
			kfree(dl->buf);
		dl->buf = NULL;
	}

	return (0);
}

/*
 * retrieve a value of counter register,
 * which is counting CPU clock.
 *
 * PS2 CPU clock is 294.912MHz.
 * 294.912MHz = 294912000
 *  = 2048 * 3 * 48000
 *  = 2^15 * 9 * 1000
 */
static inline __u32
get_COUNTER(void)
{
        __u32 r;
        asm volatile (
                "mfc0   %0,$9;"
                : "=r" (r) /* output */
                :
           );
        return r;
}

void
debuglog(struct debuglog *dl, const char * fmt, ...)
{
	va_list ap;
	char buf[200];
	int n, m;
	unsigned long flags;

	if (dl == NULL)
		dl = getdefaultlog();

	va_start(ap, fmt);
	if (dl->newline) {
		/* usec = counter * 1000000 / 294912000 */
		int usec = (get_COUNTER() % 294912000) / 128 * 1000 / 2304;
		n = 0;
		n += sprintf(&buf[n], "%02ld:%02ld.%02ld ",
			     jiffies / (HZ *60) % 60,
			     jiffies / HZ % 60,
			     jiffies % HZ);
		n += sprintf(&buf[n], "%04d ", (usec % 10000));
		n += vsprintf(&buf[n], fmt, ap);
		dl->newline = 0;
	} else {
		n = vsprintf(buf, fmt, ap);
	}
	va_end(ap);

	if (n != 0 && buf[n - 1] == '\n')
		dl->newline = 1;

	if (dl->buf == NULL) {
		printk(buf);
		return;
	}
	if (dl->size < n)
		n = dl->size;
	spin_lock_irqsave(&dl->lock, flags);
	m = (dl->count + n) - dl->size;
	if (0 < m) {
		dl->truncated += m;
		dl->count -= m;
		dl->head += m;
		dl->head %= dl->size;
	}
	if (n <= dl->size - dl->tail) {
		memcpy(&dl->buf[dl->tail], buf, n);
	} else {
		m = dl->size - dl->tail;
		memcpy(&dl->buf[dl->tail], buf, m);
		memcpy(dl->buf, &buf[m], n - m);
	}
	dl->count += n;
	dl->tail += n;
	dl->tail %= dl->size;
	spin_unlock_irqrestore(&dl->lock, flags);
}

int
debuglog_flush(struct debuglog *dl)
{
	int res;
	unsigned long flags;

	if (dl == NULL)
		dl = getdefaultlog();

	res = dl->count;
	spin_lock_irqsave(&dl->lock, flags);
	if (0 < dl->truncated) {
		printk("##### debug log message, %d bytes truncated #####\n",
		       dl->truncated);
		dl->truncated = 0;
	}
	while (0 < dl->count) {
		printk("%c", dl->buf[dl->head++]);
		dl->count--;
		dl->head %= dl->size;
	}
	spin_unlock_irqrestore(&dl->lock, flags);

	return (res);
}

#ifdef MODULE
int
init_module(void)
{
	debuglog_init(&default_log, NULL, debuglog_size);
	debuglog_inited = 1;
	return 0;
}

void
cleanup_module(void)
{
	debuglog_free(&default_log);
}
#endif /* MODULE */
