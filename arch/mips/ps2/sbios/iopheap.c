/*
 * iopheap.c
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: iopheap.c,v 1.3 2001/03/26 08:33:35 shin Exp $
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>

__initfunc(int ps2sif_initiopheap(void))
{
    int i;
    int result;

    while (1) {
	if (sbios_rpc(SBR_IOPH_INIT, NULL, &result) < 0)
	    return -1;
	if (result == 0)
	    break;
	i = 0x100000;
	while (i--)
	    ;
    }
    return 0;
}

void *ps2sif_allociopheap(int size)
{
    struct sbr_ioph_alloc_arg arg;
    int result;
    
    arg.size = size;
    if (sbios_rpc(SBR_IOPH_ALLOC, &arg, &result) < 0)
	return NULL;
    return (void *)result;
}

int ps2sif_freeiopheap(void *addr)
{
    struct sbr_ioph_free_arg arg;
    int result;
    
    arg.addr = addr;
    if (sbios_rpc(SBR_IOPH_FREE, &arg, &result) < 0)
	return -1;
    return result;
}

unsigned long ps2sif_virttobus(volatile void *a)
{
	return((unsigned long)a - 0xbc000000);
}

void *ps2sif_bustovirt(unsigned long a)
{
	return((void *)a + 0xbc000000);
}
