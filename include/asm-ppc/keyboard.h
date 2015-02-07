/*
 *  linux/include/asm-ppc/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *
 * $Id: keyboard.h,v 1.6 1998/08/20 14:41:03 ralf Exp $
 * Modified for Power Macintosh by Paul Mackerras
 */

/*
 * This file contains the ppc architecture specific keyboard definitions -
 * like the intel pc for prep systems, different for power macs.
 */

#ifndef __ASMPPC_KEYBOARD_H
#define __ASMPPC_KEYBOARD_H

#ifdef __KERNEL__

#include <asm/io.h>

#include <linux/config.h>
#include <asm/adb.h>

#ifdef CONFIG_APUS
#include <asm-m68k/keyboard.h>
#else

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0
#define INIT_KBD

extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_pretranslate(unsigned char scancode, char raw_mode);
extern int mackbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern int mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void mackbd_init_hw(void);

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);

static inline int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	if ( is_prep )
		return pckbd_setkeycode(scancode,keycode);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		return pckbd_setkeycode(scancode,keycode);
#else
		/* I'm not actually sure if it's legal to have a CHRP machine
		 * without an ADB controller. In any case, this should really
		 * be changed to be a test to see if an ADB _keyboard_ exists
		 * (not just a controller), but that's another story for
		 * another night.
		 */
		if ( adb_hardware == ADB_NONE )
			return pckbd_setkeycode(scancode,keycode);
		else
			return mackbd_setkeycode(scancode,keycode);
#endif
	else
		return mackbd_setkeycode(scancode,keycode);
}

static inline int kbd_getkeycode(unsigned int x)
{
	if ( is_prep )
		return pckbd_getkeycode(x);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		return pckbd_getkeycode(x);
#else
		if ( adb_hardware == ADB_NONE )
			return pckbd_getkeycode(x);
		else
			return mackbd_getkeycode(x);
#endif
	else
		return mackbd_getkeycode(x);
}

static inline int kbd_pretranslate(unsigned char x,char y)
{
	if ( is_prep )
		return pckbd_pretranslate(x,y);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		return pckbd_pretranslate(x,y);
#else
		if ( adb_hardware == ADB_NONE )
			return pckbd_pretranslate(x,y);
		else
			return mackbd_pretranslate(x,y);
#endif
	else
		return mackbd_pretranslate(x,y);
}

static inline int kbd_translate(unsigned char keycode, unsigned char *keycodep,
		     char raw_mode)
{
	if ( is_prep )
		return pckbd_translate(keycode,keycodep,raw_mode);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		return pckbd_translate(keycode,keycodep,raw_mode);
#else
		if ( adb_hardware == ADB_NONE )
			return pckbd_translate(keycode,keycodep,raw_mode);
		else
			return mackbd_translate(keycode,keycodep,raw_mode);
#endif
	else
		return mackbd_translate(keycode,keycodep,raw_mode);

}

static inline int kbd_unexpected_up(unsigned char keycode)
{
	if ( is_prep )
		return pckbd_unexpected_up(keycode);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		return pckbd_unexpected_up(keycode);
#else
		if ( adb_hardware == ADB_NONE )
			return pckbd_unexpected_up(keycode);
		else
			return mackbd_unexpected_up(keycode);
#endif
	else
		return mackbd_unexpected_up(keycode);
	
}

static inline void kbd_leds(unsigned char leds)
{
	if ( is_prep )
		pckbd_leds(leds);
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		pckbd_leds(leds);
#else
		if ( adb_hardware == ADB_NONE )
			pckbd_leds(leds);
		else
			mackbd_leds(leds);
#endif
	else
		mackbd_leds(leds);
}

static inline void kbd_init_hw(void)
{
	if ( is_prep )
		pckbd_init_hw();
	else if ( is_chrp )
#ifndef CONFIG_MAC_KEYBOARD
		pckbd_init_hw();
#else
		if ( adb_hardware == ADB_NONE )
			pckbd_init_hw();
		else
			mackbd_init_hw();
#endif
	else
		mackbd_init_hw();
}

#endif /* CONFIG_APUS */

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

#endif /* CONFIG_MAC_KEYBOARD */

#define keyboard_setup()						\
	request_region(0x60, 16, "keyboard")

/*
 * Machine specific bits for the PS/2 driver
 *
 * FIXME: does any PPC machine use the PS/2 driver at all?  If so,
 *        this should work, if not it's dead code ...
 */

#define AUX_IRQ 12

#define ps2_request_irq()						\
	request_irq(AUX_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL)

#define ps2_free_irq(inode) free_irq(AUX_IRQ, NULL)

#endif /* __KERNEL__ */

#endif /* __ASMPPC_KEYBOARD_H */
