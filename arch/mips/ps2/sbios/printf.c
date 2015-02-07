/*
 * printf.c: low level print character facility.
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: printf.c,v 1.1 2001/03/28 07:47:23 nakamura Exp $
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/ps2/sbcall.h>

int prom_getchar(void)
{
	int c;
	while ((c = sbios(SB_GETCHAR, NULL)) == 0)
		;
	return c;
}

int prom_putchar(char c)
{
	struct sb_putchar_arg arg;
	arg.c = c;
	return sbios(SB_PUTCHAR, &arg);
}

#ifdef CONFIG_T10000_DEBUG_HOOK
void (*ps2_debug_hook[0x80])(int c);
void ps2sio_debug_check(void)
{
	int c;
	if ((c = sbios(SB_GETCHAR, NULL)) == 0)
		return;
	if (0 <= c && c < 0x80 && ps2_debug_hook[c])
		(*ps2_debug_hook[c])(c);
}
#endif

void prom_write(char *msg, int len)
{
        while (0 < len) {
                prom_putchar(*msg++);
                len--;
        }
}

void prom_printf(char *fmt, ...)
{
	va_list args;
	char *p, buf[1024];

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	for (p = buf; *p; p++)
		prom_putchar(*p);

	return;
}
