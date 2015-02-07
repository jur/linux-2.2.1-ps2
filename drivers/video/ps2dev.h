#ifndef __PS2DEV_H
#define __PS2DEV_H

#include <linux/autoconf.h>
#include <asm/types.h>
#include <asm/io.h>

#define ALIGN16(x)	(((unsigned long)(x) + 15) & ~15)
#define PACK32(x, y)	((x) | ((y) << 16))
#define PACK64(x, y)	((u64)(x) | ((u64)(y) << 32))

#define GSFB_SIZE		(4 * 1024 * 1024)
#define SPR_SIZE		16384

/* register defines */

#define IPUREG_CMD		KSEG1ADDR(0x10002000)
#define IPUREG_CTRL		KSEG1ADDR(0x10002010)
#define IPUREG_BP		KSEG1ADDR(0x10002020)
#define IPUREG_TOP		KSEG1ADDR(0x10002030)

#define GIFREG_BASE		KSEG1ADDR(0x10003000)
#define GIFREG(x)		(*(volatile u32 *)(GIFREG_BASE + ((x) << 4)))
#define VIF0REG_BASE		KSEG1ADDR(0x10003800)
#define VIF0REG(x)		(*(volatile u32 *)(VIF0REG_BASE + ((x) << 4)))
#define VIF1REG_BASE		KSEG1ADDR(0x10003c00)
#define VIF1REG(x)		(*(volatile u32 *)(VIF1REG_BASE + ((x) << 4)))
#define VIFnREG(n, x)		\
	(*(volatile u32 *)(VIF0REG_BASE + ((n) * 0x0400) + ((x) << 4)))

#define VIF0_FIFO		KSEG1ADDR(0x10004000)
#define VIF1_FIFO		KSEG1ADDR(0x10005000)
#define GIF_FIFO		KSEG1ADDR(0x10006000)
#define IPU_O_FIFO		KSEG1ADDR(0x10007000)
#define IPU_I_FIFO		KSEG1ADDR(0x10007010)

#define GSSREG_BASE1		KSEG1ADDR(0x12000000)
#define GSSREG_BASE2		KSEG1ADDR(0x12001000)
#define GSSREG1(x)		(GSSREG_BASE1 + ((x) << 4))
#define GSSREG2(x)		(GSSREG_BASE2 + (((x) & 0x0f) << 4))

/* inline assembler functions */

union _dword {
        __u64 di;
        struct {
#ifdef CONFIG_CPU_LITTLE_ENDIAN
                __u32   lo, hi;
#else
                __u32   hi, lo;
#endif
        } si;
};

static inline void store_double(unsigned long addr, unsigned long long val)
{
    union _dword src;

    src.di=val;
    __asm__ __volatile__(
        ".set push\n"
        "	.set mips3\n"
        "	pextlw         $8,%1,%0\n"
        "	sd             $8,(%2)\n"
        "	.set   pop"
        : : "r"(src.si.lo), "r"(src.si.hi), "r" (addr) : "$8");
}

static inline unsigned long long load_double(unsigned long addr)
{
    union _dword val;

    __asm__ __volatile__(
	".set	push\n"
	"	.set	mips3\n"
	"	ld	$8,(%2)\n"
		/* 63-32th bits must be same as 31th bit */
	"	dsra	%1,$8,32\n" 
	"	dsll	%0,$8,32\n" 
	"	dsra	%0,%0,32\n"
	"	.set	pop"
	: "=r" (val.si.lo), "=r" (val.si.hi) : "r" (addr) : "$8");

    return val.di;
}

static inline void move_quad(unsigned long dest, unsigned long src)
{
    __asm__ __volatile__(
	"move	$8,%1\n"
	"	lq     $9,($8)\n"
	"	move	$8,%0\n"
	"	sq     $9,($8)"
	: : "r" (dest), "r" (src) : "$8", "$9" );
}

static inline void dummy_read_quad(unsigned long addr)
{
    __asm__ __volatile__(
	"lq	$9,(%0)"
	: : "r" (addr) : "$9" );
}

/* function prototypes */

/* ps2dev.c */
void (*ps2dev_screeninfo_hook)(struct ps2_screeninfo *info);
int ps2gs_screeninfo(struct ps2_screeninfo *info);
extern struct file_operations ps2gs_fops;
extern struct file_operations ps2vpu_fops;
extern struct file_operations ps2ipu_fops;
extern struct file_operations ps2spr_fops;
void ps2dev_init(void);

/* ps2image.c */
int ps2gs_loadimage(struct ps2_image *img, struct dma_device *dev, int async);
int ps2gs_storeimage(struct ps2_image *img, struct dma_device *dev);
int ps2gs_reset(int mode);

/* ps2mem.c */
extern struct file_operations ps2mem_fops;

/* ps2event.c */
extern struct file_operations ps2ev_fops;
void ps2ev_init(void);

/* ps2gs.c */
int ps2gs_setcrtmode(int mode, int res);
int ps2gs_setdisplay(int ch, int w, int h, int dx, int dy);
int ps2gs_setdispfb(int ch, int fbp, int fbw, int psm, int dbx, int dby);
int ps2gs_setpmode(int sw, int mmod, int amod, int slbg, int alp);
int ps2gs_setdpms(int mode);
int ps2gs_blank(int onoff);
int ps2gs_set_gssreg(int reg, u64 val);
int ps2gs_get_gssreg(int reg, u64 *val);
int ps2gs_set_gsreg(int reg, u64 val);

#endif /* __PS2DEV_H */
