/*
 * perf_dev.h - mips spcific pc sampling device interface
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#ifndef _MIPS_PERF_DEV_H
#define _MIPS_PERF_DEV_H

#include <linux/autoconf.h>
#include <asm/perf_counter.h>

#ifdef CONFIG_CPU_R5900
#include <asm/r5900_perf_dev.h>
#else
#err "not yet"
#endif

#endif /*_MIPS_PERF_DEV_H */
