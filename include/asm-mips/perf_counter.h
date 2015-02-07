/*
 * perf_counter.h - performance counter description
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */


#ifndef __MIPS_PERF_COUNTER_H
#define __MIPS_PERF_COUNTER_H

#include <linux/autoconf.h>

#ifdef CONFIG_CPU_R5900
#include <asm/r5900_perf_counter.h>
#else
#err "not yet"
#endif

#endif /* __MIPS_PERF_COUNTER_H */
