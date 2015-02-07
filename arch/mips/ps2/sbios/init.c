/*
 * init.c
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: init.c,v 1.1.6.2 2001/08/28 07:19:16 takemura Exp $
 */
#include <linux/init.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/ps2/sbcall.h>
#include <asm/ps2/bootinfo.h>

#define SBIOS_BASE	0x80001000
#define SBIOS_ENTRY	0
#define SBIOS_SIGNATURE	4

static struct ps2_bootinfo ps2_bootinfox;
struct ps2_bootinfo *ps2_bootinfo = &ps2_bootinfox;

char arcs_cmdline[CL_SIZE] = "root=/dev/hda1";

static void sbios_prints(const char *text)
{
	printk("SBIOS: %s", text);
}

int __init prom_init(int argc, char **argv, char **envp)
{
	struct ps2_bootinfo *bootinfo;
	int oldbootinfo = 0;
	int version;

	/* default bootinfo */
	memset(&ps2_bootinfox, 0, sizeof(struct ps2_bootinfo));
	ps2_bootinfox.sbios_base = SBIOS_BASE;
#ifdef CONFIG_T10000_MAXMEM
	ps2_bootinfox.maxmem = 128;
#else /* CONFIG_T10000_MAXMEM */
	ps2_bootinfox.maxmem = 32;
#endif /* !CONFIG_T10000_MAXMEM */
	ps2_bootinfox.maxmem = ps2_bootinfox.maxmem * 1024 * 1024 - 4096;

#ifdef CONFIG_PS2_COMPAT_OLDBOOTINFO
	if (*(unsigned long *)(SBIOS_BASE + SBIOS_SIGNATURE) == 0x62325350) {
	    bootinfo = (struct ps2_bootinfo *)KSEG0ADDR(PS2_BOOTINFO_OLDADDR);
	    memcpy(ps2_bootinfo, bootinfo, PS2_BOOTINFO_OLDSIZE);
	    oldbootinfo = 1;
	} else
#endif
	{
	    bootinfo = (struct ps2_bootinfo *)envp;
	    memcpy(ps2_bootinfo, bootinfo, bootinfo->size);
	}

	mips_machgroup = MACH_GROUP_EE;

	switch (ps2_bootinfo->mach_type) {
	case PS2_BOOTINFO_MACHTYPE_T10K:
	    mips_machtype = MACH_T10000;
	    break;
	case PS2_BOOTINFO_MACHTYPE_PS2:
	default:
	    mips_machtype = MACH_PS2;
	    break;
	}

	mips_memory_upper = KSEG0ADDR(ps2_bootinfo->maxmem);

	/* get command line parameters */
	if (ps2_bootinfo->opt_string != NULL) {
	    int i;
	    for (i = 0; i < CL_SIZE - 1 && ps2_bootinfo->opt_string[i]; i++)
		arcs_cmdline[i] = ps2_bootinfo->opt_string[i];
	    arcs_cmdline[i] = '\0';
	}

	if (*(unsigned long *)(ps2_bootinfo->sbios_base + SBIOS_SIGNATURE) != 0x62325350) {
		/* SBIOS not found */
		while (1)
			;
	}
	sbios = *(int (**)(int, void *))(ps2_bootinfo->sbios_base + SBIOS_ENTRY);
	printk("use boot information at 0x%x%s\n",
	       bootinfo, oldbootinfo ? "(old style)" : "");
	printk("boot option string at 0x%08x: %s\n",
	       ps2_bootinfo->opt_string, arcs_cmdline);

	version = sbios(SB_GETVER, 0);
	printk("PlayStation 2 SIF BIOS: %04x\n", version);

	/* Register Callback for debug output. */
	sbios(SB_SET_PRINTS_CALLBACK, sbios_prints);

	if (version == 0x200) {
		/* Beta kit */
		*(unsigned char *)0x80007c20 = 0;
	} else if (version == 0x250) {
		/* 1.0 kit */
		*(unsigned char *)0x800081b0 = 0;
	}

	return 0;
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
	/* nothing to do */
}

void prom_free_prom_memory (void)
{
	/* nothing to do */
}
