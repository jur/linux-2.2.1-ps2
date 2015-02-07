/*
 *  linux/drivers/block/ps2ide.h
 *
 *	Copyright (C) 2001  Sony Computer Entertainment Inc.
 */

#ifndef _PS2IDE_H
#define _PS2IDE_H

#ifdef CONFIG_PS2_HDD

/* override I/O port read/write functions defined in <asm/io.h> */

#undef outb
#undef outb_p
#undef inb
#undef inb_p

#define PS2HDD_IDE_CMD	0xb400004e
#define PS2HDD_IDE_STAT	0xb400004e
#define PS2SPD_PIO_DIR	0xb400002c
#define PS2SPD_PIO_DATA	0xb400002e

static inline void ps2_ata_outb(unsigned int value, unsigned long port)
{
    __outb(value, port);
    if (port == PS2HDD_IDE_CMD) {	/* LED on */
	*(volatile unsigned char *)PS2SPD_PIO_DIR = 1;
	*(volatile unsigned char *)PS2SPD_PIO_DATA = 0;
    }
}

static inline unsigned char ps2_ata_inb(unsigned long port)
{
    unsigned char data;

    data = __inb(port);
    if (port == PS2HDD_IDE_STAT) {	/* LED off */
	*(volatile unsigned char *)PS2SPD_PIO_DIR = 1;
	*(volatile unsigned char *)PS2SPD_PIO_DATA = 1;
    }
    return data;
}

#define outb(b,p)	ps2_ata_outb((b),(p))
#define outb_p(b,p)	ps2_ata_outb((b),(p))
#define inb(p)		ps2_ata_inb((p))
#define inb_p(p)	ps2_ata_inb((p))

#endif /* CONFIG_PS2_HDD */

#endif	/* _PS2IDE_H */
