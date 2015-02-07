/*
 * setup.c: setup PlayStation 2
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: setup.c,v 1.46.6.2 2001/08/30 08:22:37 takemura Exp $
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mc146818rtc.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/console.h>

struct hd_driveid;
struct pt_regs;
#include <asm/ide.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/ps2/irq.h>
#include <asm/ps2/bootinfo.h>

extern void ps2_machine_restart(char *command);
extern void ps2_machine_halt(void);
extern void ps2_machine_power_off(void);
extern void ps2_aif_init(void);
extern int prom_putchar(char c);
extern void ps2_ide_port_found(ide_ioreg_t base);
extern int ps2sif_init(void);
extern int ps2lock_init(void);
extern int smaprpc_init_module(void);
extern int smap_init_module(void);

extern struct ide_ops ps2_ide_ops;
extern struct rtc_ops ps2_rtc_ops;

int ps2_pccard_present;
int ps2_pcic_type;
struct ps2_sysconf *ps2_sysconf;

extern asmlinkage void ps2_irq(void);

__initfunc(static void ps2_irq_setup(void))
{
	/* handlers are already prepared */
	enable_irq(IRQ_INTC_TIMER0);
	enable_irq(IRQ_INTC_GS);
	enable_irq(IRQ_INTC_SBUS);

	/* Now safe to set the exception vector. */
	set_except_vector(0, ps2_irq);
}

void ps2_dev_init(void)
{
	ps2sif_init();
	ps2_powerbutton_init();
#ifdef CONFIG_PS2_LOCK
	ps2lock_init();
#endif
#ifdef CONFIG_PS2_PAD
	ps2pad_init();
#endif
#ifdef CONFIG_PS2_ETHER_SMAP
	smaprpc_init_module();
	smap_init_module();
#endif
}

#ifdef CONFIG_T10000_DEBUG_HOOK
extern void traceback_me(void);
extern void (*ps2_debug_hook[0x80])(int c);
extern unsigned long jiffies;
#ifdef CONFIG_MAGIC_SYSRQ
struct kbd_struct;
extern void handle_sysrq(int key, struct pt_regs *pt_regs,
                  struct kbd_struct *kbd, struct tty_struct *tty);

static void ps2_debug_sysrq(int c)
{
	printk("\n%02ld:%02ld.%02ld: sysrq:%c ...\n",
		jiffies / (HZ *60) % 60,
		jiffies / HZ % 60,
		jiffies % HZ,
		c);
	handle_sysrq(c, 0, 0, 0);
	printk("%02ld:%02ld.%02ld: sysrq:%c done.\n",
		jiffies / (HZ *60) % 60,
		jiffies / HZ % 60,
		jiffies % HZ,
		c);
}
#endif /* CONFIG_MAGIC_SYSRQ */

static void ps2_debug_backtrace(int c)
{
	printk("\n%02ld:%02ld.%02ld: backtrace ...\n",
		jiffies / (HZ *60) % 60,
		jiffies / HZ % 60,
		jiffies % HZ);
	traceback_me();
	printk("%02ld:%02ld.%02ld: backtrace done.\n",
		jiffies / (HZ *60) % 60,
		jiffies / HZ % 60,
		jiffies % HZ);
}
#endif

void ps2_be_board_handler(struct pt_regs *regs)
{
	u_int sr, paddr;

	sr = read_32bit_cp0_register(CP0_STATUS);
	paddr = read_32bit_cp0_register($23);			/* BadPAddr */
	force_sig(SIGBUS, current);
	show_regs(regs);
	printk("paddr : %08x\n", paddr);
	write_32bit_cp0_register(CP0_STATUS, sr & ~(1 << 12));  /* clear BM */
}

__initfunc(void ps2_setup(void))
{
	ps2_pccard_present = ps2_bootinfo->pccard_type;
	ps2_pcic_type = ps2_bootinfo->pcic_type;
	ps2_sysconf = &ps2_bootinfo->sysconf;

	_machine_restart = ps2_machine_restart;
	_machine_halt = ps2_machine_halt;
	_machine_power_off = ps2_machine_power_off;

	irq_setup = ps2_irq_setup;
	rtc_ops = &ps2_rtc_ops;
#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &ps2_ide_ops;
#endif
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#else
#ifdef CONFIG_PS2_GSCON
	{
	    extern struct consw ps2_con;

	    conswitchp = &ps2_con;
	}
#endif
#endif /* !CONFIG_DUMMY_CONSOLE */
#ifdef CONFIG_T10000
	ps2_aif_init();
#endif
#ifdef CONFIG_T10000_AIFHDD
        ps2_ide_port_found(0xb8000060);
#endif
#ifdef CONFIG_PS2_PCCARDIDE
	if (ps2_pccard_present == 0x0200) /* CF found in the PC card slot */
		ps2_ide_port_found(0xb40001f0);
	if (ps2_pccard_present == 0x0201) /* ATA card found in the PC card slot */
		ps2_ide_port_found(0xb4000170);
	if (ps2_pccard_present == 0x0202) /* ATA card found in the PC card slot */
		ps2_ide_port_found(0xb4000180);
#endif
#ifdef CONFIG_PS2_HDD
	if (ps2_pccard_present == 0x0100)	/* PS2 HDD & Ether unit */
		ps2_ide_port_found(0xb4000040);
#endif
#ifdef CONFIG_T10000_DEBUG_HOOK
	ps2_debug_hook['T'] = ps2_debug_backtrace;
#ifdef CONFIG_MAGIC_SYSRQ
	ps2_debug_hook['b'] = ps2_debug_sysrq;	/* Resetting */
	ps2_debug_hook['u'] = ps2_debug_sysrq;	/* Emergency Remount R/O */
	ps2_debug_hook['s'] = ps2_debug_sysrq;	/* Emergency Sync */
	ps2_debug_hook['t'] = ps2_debug_sysrq;	/* Show State */
	ps2_debug_hook['m'] = ps2_debug_sysrq;	/* Show Memory */
	ps2_debug_hook['e'] = ps2_debug_sysrq;	/* Terminate All Tasks */
	ps2_debug_hook['i'] = ps2_debug_sysrq;	/* Kill All Tasks */
	ps2_debug_hook['l'] = ps2_debug_sysrq;	/* Kill All Tasks (even init) */

	{ 
	int i;
	for (i = '0' ; i<='9'; i++) 
		ps2_debug_hook[i] = ps2_debug_sysrq; /* console logging level */
	}
#endif /* CONFIG_MAGIC_SYSRQ */
#endif
}
