/*
 * sifsyms.c
 *
 * Export PS2 functions needed for loadable modules.
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sifsyms.c,v 1.5 2001/03/28 07:47:23 nakamura Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/spinlock.h>
#include <asm/ps2/sifdefs.h>
#include <asm/ps2/siflock.h>
#include <asm/ps2/sbcall.h>

int (*sbios)(int, void *) = NULL;

EXPORT_SYMBOL(sbios);
EXPORT_SYMBOL(sbios_rpc);

/*
 * SIF functions
 */
EXPORT_SYMBOL(ps2sif_setdma);
EXPORT_SYMBOL(ps2sif_dmastat);
EXPORT_SYMBOL(ps2sif_writebackdcache);

/*
 * SIF remote procedure call functions
 */
EXPORT_SYMBOL(ps2sif_bindrpc);
EXPORT_SYMBOL(ps2sif_callrpc);
EXPORT_SYMBOL(ps2sif_checkstatrpc);
EXPORT_SYMBOL(ps2sif_setrpcqueue);
EXPORT_SYMBOL(ps2sif_getnextrequest);
EXPORT_SYMBOL(ps2sif_execrequest);
EXPORT_SYMBOL(ps2sif_registerrpc);
EXPORT_SYMBOL(ps2sif_getotherdata);
EXPORT_SYMBOL(ps2sif_removerpc);
EXPORT_SYMBOL(ps2sif_removerpcqueue);

/*
 * IOP heap functions
 */
EXPORT_SYMBOL(ps2sif_allociopheap);
EXPORT_SYMBOL(ps2sif_freeiopheap);
EXPORT_SYMBOL(ps2sif_virttobus);
EXPORT_SYMBOL(ps2sif_bustovirt);
