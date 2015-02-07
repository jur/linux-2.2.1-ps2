/*
 * console.c: SGI arcs console code.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 *
 * $Id: console.c,v 1.1 1998/10/18 13:32:08 tsbogend Exp $
 */
#include <linux/init.h>
#include <asm/sgialib.h>

__initfunc(void prom_putchar(char c))
{
	long cnt;
	char it = c;

	romvec->write(1, &it, 1, &cnt);
}

__initfunc(char prom_getchar(void))
{
	long cnt;
	char c;

	romvec->read(0, &c, 1, &cnt);
	return c;
}
