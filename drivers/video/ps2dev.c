/*
 *  linux/drivers/video/ps2dev.c
 *  PlayStation 2 integrated device driver
 *
 *	Copyright (C) 2000,2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/console.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>

#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>
#include "ps2dma.h"
#include "ps2dev.h"

#define MINOR_FUNC(x)	(MINOR(x) >> 4)
#define MINOR_UNIT(x)	(MINOR(x) & 0x0f)

#define PS2MEM_FUNC	0
#define PS2EVENT_FUNC	1
#define PS2GS_FUNC	2
#define PS2VPU_FUNC	3
#define PS2IPU_FUNC	4
#define PS2SPR_FUNC	5

void *ps2spr_vaddr;		/* scratchpad RAM virtual address */

/*
 *  common DMA device functions
 */

static int ps2dev_send_ioctl(struct dma_device *dev,
			     unsigned int cmd, unsigned long arg)
{
    struct ps2_packet pkt;
    struct ps2_packet *pkts;
    struct ps2_plist plist;
    struct ps2_pstop pstop;
    int result;
    u32 ff, oldff;
    int val;

    switch (cmd) {
    case PS2IOC_SEND:
    case PS2IOC_SENDA:
	if (copy_from_user(&pkt, (void *)arg, sizeof(pkt)))
	    return -EFAULT;
	return ps2dma_send(dev, &pkt, cmd == PS2IOC_SENDA);
    case PS2IOC_SENDL:
	if (copy_from_user(&plist, (void *)arg, sizeof(plist)))
	    return -EFAULT;
	if (plist.num < 0)
	    return -EINVAL;
	if ((pkts = kmalloc(sizeof(struct ps2_packet) * plist.num, GFP_KERNEL) ) == NULL)
	    return -ENOMEM;
	if (copy_from_user(pkts, plist.packet, sizeof(struct ps2_packet) * plist.num)) {
	    kfree(pkts);
	    return -EFAULT;
	}
	result = ps2dma_send_list(dev, plist.num, pkts);
	kfree(pkts);
	return result;
    case PS2IOC_SENDQCT:
	return ps2dma_get_qct(dev, DMA_SENDCH, arg);
    case PS2IOC_SENDSTOP:
	ps2dma_stop(dev, DMA_SENDCH, &pstop);
	return copy_to_user((void *)arg, &pstop, sizeof(pstop)) ? -EFAULT : 0;
    case PS2IOC_SENDLIMIT:
	return ps2dma_set_qlimit(dev, DMA_SENDCH, arg);

    case PS2IOC_ENABLEEVENT:
	oldff = dev->intr_mask;
	if ((int)arg >= 0) {
	    cli();
	    ff = dev->intr_mask ^ arg;
	    dev->intr_flag &= ~ff;
	    dev->intr_mask = arg;
	    sti();
	}
	return oldff;
    case PS2IOC_GETEVENT:
	cli();
	oldff = dev->intr_flag;
	if ((int)arg > 0)
	    dev->intr_flag &= ~arg;
	sti();
	return oldff;
    case PS2IOC_WAITEVENT:
	cli();
	while (!(dev->intr_flag & arg)) {
	    interruptible_sleep_on(&dev->empty_wq);
	    if (signal_pending(current)) {
		sti();
		return -ERESTARTSYS;	/* signal arrived */
	    }
	}
	oldff = dev->intr_flag;
	dev->intr_flag &= ~arg;
	sti();
	return oldff;
    case PS2IOC_SETSIGNAL:
	val = dev->sig;
	if ((int)arg >= 0)
	    dev->sig = arg;
	return val;
    }
    return -ENOIOCTLCMD;
}

static int ps2dev_recv_ioctl(struct dma_device *dev,
			     unsigned int cmd, unsigned long arg)
{
    struct ps2_packet pkt;
    struct ps2_packet *pkts;
    struct ps2_plist plist;
    struct ps2_pstop pstop;
    int result;

    switch (cmd) {
    case PS2IOC_RECV:
    case PS2IOC_RECVA:
	if (copy_from_user(&pkt, (void *)arg, sizeof(pkt)))
	    return -EFAULT;
	return ps2dma_recv(dev, &pkt, cmd == PS2IOC_RECVA);
    case PS2IOC_RECVL:
	if (copy_from_user(&plist, (void *)arg, sizeof(plist)))
	    return -EFAULT;
	if (plist.num < 0)
	    return -EINVAL;
	if ((pkts = kmalloc(sizeof(struct ps2_packet) * plist.num, GFP_KERNEL) ) == NULL)
	    return -ENOMEM;
	if (copy_from_user(pkts, plist.packet, sizeof(struct ps2_packet) * plist.num)) {
	    kfree(pkts);
	    return -EFAULT;
	}
	result = ps2dma_recv_list(dev, plist.num, pkts);
	kfree(pkts);
	return result;
    case PS2IOC_RECVQCT:
	return ps2dma_get_qct(dev, DMA_RECVCH, arg);
    case PS2IOC_RECVSTOP:
	ps2dma_stop(dev, DMA_RECVCH, &pstop);
	return copy_to_user((void *)arg, &pstop, sizeof(pstop)) ? -EFAULT : 0;
    case PS2IOC_RECVLIMIT:
	return ps2dma_set_qlimit(dev, DMA_RECVCH, arg);

    }
    return -ENOIOCTLCMD;
}

static ssize_t ps2dev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_packet pkt;
    int result;

    pkt.ptr = buf;
    pkt.len = count;
    if ((result = ps2dma_recv(dev, &pkt, 0)) < 0)
	return result;

    return count;
}

static ssize_t ps2dev_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_packet pkt;

    pkt.ptr = (char *)buf;
    pkt.len = count;
    return ps2dma_write(dev, &pkt, file->f_flags & O_NONBLOCK);
}

static loff_t ps2dev_llseek(struct file *file, loff_t offset, int orig)
{
    return -ESPIPE;	/* cannot seek */
}

static int ps2dev_fsync(struct file *file, struct dentry *dentry)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    return ps2dma_get_qct(dev, DMA_SENDCH, 1);
}

static unsigned long ps2dev_nopage(struct vm_area_struct *vma, unsigned long addr, int write)
{
    return 0;	/* SIGBUS */
}

static struct vm_operations_struct ps2dev_vmops = {
    NULL, NULL, NULL, NULL, NULL, NULL, ps2dev_nopage,
};


/*
 *  Graphics Synthesizer (/dev/ps2gs) driver
 */

static struct ps2_crtmode current_crtmode;
static struct ps2_display current_display[2];
static struct ps2_dispfb current_dispfb[2];
static struct ps2_pmode current_pmode;
static struct ps2_screeninfo current_screeninfo;

void (*ps2dev_screeninfo_hook)(struct ps2_screeninfo *info) = NULL;

static int ps2gs_crtmode(struct ps2_crtmode *crtmode)
{
    if (ps2gs_setcrtmode(crtmode->mode, crtmode->res) < 0)
	return -1;
    current_crtmode = *crtmode;
    return 0;
}

static int ps2gs_display(struct ps2_display *display)
{
    if (ps2gs_setdisplay(display->ch, display->w, display->h, display->dx, display->dy) < 0)
	return -1;
    current_display[display->ch] = *display;
    return 0;
}

static int ps2gs_dispfb(struct ps2_dispfb *dispfb)
{
    if (ps2gs_setdispfb(dispfb->ch, dispfb->fbp, dispfb->fbw, dispfb->psm, dispfb->dbx, dispfb->dby) < 0)
	return -1;
    current_dispfb[dispfb->ch] = *dispfb;
    return 0;
}

static int ps2gs_pmode(struct ps2_pmode *pmode)
{
    if (ps2gs_setpmode(pmode->sw, pmode->mmod, pmode->amod, pmode->slbg, pmode->alp) < 0)
	return -1;
    current_pmode = *pmode;
    return 0;
}

int ps2gs_screeninfo(struct ps2_screeninfo *info)
{
    int ch = 0, ctx = 0;
    struct ps2_crtmode crtmode;
    struct ps2_display display;
    struct ps2_dispfb dispfb;
    struct ps2_pmode pmode;
    int result;

    crtmode.mode = info->mode;
    crtmode.res = info->res;
    display.ch = ch;
    display.w = info->w;
    display.h = info->h;
    display.dx = display.dy = 0;
    dispfb.ch = ch;
    dispfb.fbp = info->fbp;
    dispfb.fbw = (info->w + 63) / 64;
    dispfb.psm = info->psm;
    dispfb.dbx = dispfb.dby = 0;
    pmode.sw = 1 << ch;
    pmode.mmod = 1;
    pmode.amod = 0;
    pmode.slbg = 0;
    pmode.alp = 0xff;
    
    result = ps2gs_reset(PS2_GSRESET_GS);
    if (ps2gs_crtmode(&crtmode) < 0 ||
	ps2gs_display(&display) < 0 ||
	ps2gs_dispfb(&dispfb) < 0) {
	ps2gs_screeninfo(&current_screeninfo);
	return -1;
    }

    /* set GS registers */
    ps2gs_set_gsreg(PS2_GS_FRAME_1 + ctx,
		    PS2_GS_SETREG_FRAME(info->fbp & 0x1ff,
					((info->w + 63) / 64) & 0x3f,
					info->psm & 0x3f,
					0));
    ps2gs_set_gsreg(PS2_GS_ZBUF_1 + ctx,
		    PS2_GS_SETREG_ZBUF(info->fbp & 0x1ff, 0, 1));
    ps2gs_set_gsreg(PS2_GS_XYOFFSET_1 + ctx,
		    PS2_GS_SETREG_XYOFFSET(0, 0));
    ps2gs_set_gsreg(PS2_GS_SCISSOR_1 + ctx,
		    PS2_GS_SETREG_SCISSOR(0, info->w - 1,
					  0, info->h - 1));
    ps2gs_set_gsreg(PS2_GS_TEST_1 + ctx,
		    PS2_GS_SETREG_TEST(0, 0, 0, 0, 0, 0, 1, 1));
    ps2gs_set_gsreg(PS2_GS_FBA_1 + ctx,
		    PS2_GS_SETREG_FBA(0));

    ps2gs_set_gsreg(PS2_GS_SCANMSK,
		    PS2_GS_SETREG_SCANMSK(0));
    ps2gs_set_gsreg(PS2_GS_PRMODECONT,
		    PS2_GS_SETREG_PRMODECONT(1));
    ps2gs_set_gsreg(PS2_GS_COLCLAMP,
		    PS2_GS_SETREG_COLCLAMP(1));
    ps2gs_set_gsreg(PS2_GS_PABE,
		    PS2_GS_SETREG_PABE(0));
    ps2gs_set_gsreg(PS2_GS_DTHE,
		    PS2_GS_SETREG_DTHE(0));

    /* clear screen */
    ps2gs_set_gsreg(PS2_GS_PRIM,
		    PS2_GS_SETREG_PRIM(6, 0, 0, 0, 0, 0, 0, ctx, 0));
    ps2gs_set_gsreg(PS2_GS_RGBAQ,
		    PS2_GS_SETREG_RGBAQ(0, 0, 0, 0, 0));
    ps2gs_set_gsreg(PS2_GS_XYZ2,
		    PS2_GS_SETREG_XYZ(0, 0, 0));
    ps2gs_set_gsreg(PS2_GS_XYZ2,
		    PS2_GS_SETREG_XYZ(info->w << 4, info->h << 4, 0));

    /* turn on display */
    if (ps2gs_pmode(&pmode) < 0) {
	ps2gs_screeninfo(&current_screeninfo);
	return -1;
    }
    current_screeninfo = *info;

    if (ps2dev_screeninfo_hook)
	ps2dev_screeninfo_hook(info);
    if (result)
	printk("ps2gs: DMA timeout\n");
    return 0;
}

static int ps2gs_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_gsinfo gsinfo;
    struct ps2_image image;
    struct ps2_gssreg gssreg;
    struct ps2_gsreg gsreg;
    struct ps2_gifreg gifreg;
    struct ps2_screeninfo screeninfo;
    struct ps2_crtmode crtmode;
    struct ps2_display display;
    struct ps2_dispfb dispfb;
    struct ps2_pmode pmode;
    int val;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_GSINFO:
	gsinfo.size = GSFB_SIZE;
	return copy_to_user((void *)arg, &gsinfo, sizeof(gsinfo)) ? -EFAULT : 0;
    case PS2IOC_GSRESET:
	val = arg;
	if (ps2gs_reset(val) != 0)
	    printk("ps2gs: DMA timeout\n");
	return 0;

    case PS2IOC_LOADIMAGE:
    case PS2IOC_LOADIMAGEA:
	if (copy_from_user(&image, (void *)arg, sizeof(image)))
	    return -EFAULT;
	return ps2gs_loadimage(&image, dev, cmd == PS2IOC_LOADIMAGEA);
    case PS2IOC_STOREIMAGE:
	if (copy_from_user(&image, (void *)arg, sizeof(image)))
	    return -EFAULT;
	return ps2gs_storeimage(&image, dev);

    case PS2IOC_SGSSREG:
	if (copy_from_user(&gssreg, (void *)arg, sizeof(gssreg)))
	    return -EFAULT;
	if (ps2gs_set_gssreg(gssreg.reg, gssreg.val) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GGSSREG:
	if (copy_from_user(&gssreg, (void *)arg, sizeof(gssreg)))
	    return -EFAULT;
	if (ps2gs_get_gssreg(gssreg.reg, &gssreg.val) < 0)
	    return -EINVAL;
	return copy_to_user((void *)arg, &gssreg, sizeof(gssreg)) ? -EFAULT : 0;
    case PS2IOC_SGSREG:
	if (copy_from_user(&gsreg, (void *)arg, sizeof(gsreg)))
	    return -EFAULT;
	if (ps2gs_set_gsreg(gsreg.reg, gsreg.val) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_SGIFREG:
	if (copy_from_user(&gifreg, (void *)arg, sizeof(gifreg)))
	    return -EFAULT;
	if (gifreg.reg == PS2_GIFREG_CTRL ||
	    gifreg.reg == PS2_GIFREG_MODE)
	    GIFREG(gifreg.reg) = gifreg.val;
	else
	    return -EINVAL;
	return 0;
    case PS2IOC_GGIFREG:
	if (copy_from_user(&gifreg, (void *)arg, sizeof(gifreg)))
	    return -EFAULT;
	if (gifreg.reg == PS2_GIFREG_STAT ||
	    (gifreg.reg >= PS2_GIFREG_TAG0 && gifreg.reg <= PS2_GIFREG_P3TAG)) {
	    if (gifreg.reg != PS2_GIFREG_STAT)
		GIFREG(PS2_GIFREG_CTRL) = 1 << 3;
	    gifreg.val = GIFREG(gifreg.reg);
	    if (gifreg.reg != PS2_GIFREG_STAT)
		GIFREG(PS2_GIFREG_CTRL) = 0;
	} else {
	    return -EINVAL;
	}
	return copy_to_user((void *)arg, &gifreg, sizeof(gifreg)) ? -EFAULT : 0;

    case PS2IOC_SSCREENINFO:
	if (copy_from_user(&screeninfo, (void *)arg, sizeof(screeninfo)))
	    return -EFAULT;
	if (ps2gs_screeninfo(&screeninfo) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GSCREENINFO:
	return copy_to_user((void *)arg, &current_screeninfo, sizeof(current_screeninfo)) ? -EFAULT : 0;
    case PS2IOC_SCRTMODE:
	if (copy_from_user(&crtmode, (void *)arg, sizeof(crtmode)))
	    return -EFAULT;
	if (ps2gs_crtmode(&crtmode) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GCRTMODE:
	return copy_to_user((void *)arg, &current_crtmode, sizeof(current_crtmode)) ? -EFAULT : 0;
    case PS2IOC_SDISPLAY:
	if (copy_from_user(&display, (void *)arg, sizeof(display)))
	    return -EFAULT;
	if (ps2gs_display(&display) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GDISPLAY:
	if (copy_from_user(&display, (void *)arg, sizeof(display)))
	    return -EFAULT;
	if (display.ch < 0 || display.ch > 1)
	    return -EINVAL;
	return copy_to_user((void *)arg, &current_display[display.ch], sizeof(current_display[0])) ? -EFAULT : 0;
    case PS2IOC_SDISPFB:
	if (copy_from_user(&dispfb, (void *)arg, sizeof(dispfb)))
	    return -EFAULT;
	if (ps2gs_dispfb(&dispfb) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GDISPFB:
	if (copy_from_user(&dispfb, (void *)arg, sizeof(dispfb)))
	    return -EFAULT;
	if (dispfb.ch < 0 || dispfb.ch > 1)
	    return -EINVAL;
	return copy_to_user((void *)arg, &current_dispfb[dispfb.ch], sizeof(current_dispfb[0])) ? -EFAULT : 0;
    case PS2IOC_SPMODE:
	if (copy_from_user(&pmode, (void *)arg, sizeof(pmode)))
	    return -EFAULT;
	if (ps2gs_pmode(&pmode) < 0)
	    return -EINVAL;
	return 0;
    case PS2IOC_GPMODE:
	return copy_to_user((void *)arg, &current_pmode, sizeof(current_pmode)) ? -EFAULT : 0;

    case PS2IOC_DPMS:
	val = arg;
	if (ps2gs_setdpms(val) < 0)
	    return -EINVAL;
	return 0;
    }
    return -EINVAL;
}

static void ps2gif_reset(void)
{
    int apath;

    apath = (GIFREG(PS2_GIFREG_STAT) >> 10) & 3;
    GIFREG(PS2_GIFREG_CTRL) = 0x00000001;	/* reset GIF */
    if (apath == 3)
	store_double(GSSREG2(PS2_GSSREG_CSR), (u64)0x0100);	/* reset GS */
}

static int ps2gs_open_count = 0;	/* only one process can open */

static int ps2gs_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2gs_open_count)
	return -EBUSY;
    ps2gs_open_count++;

    if ((dev = ps2dma_dev_init(DMA_GIF, -1)) == NULL) {
	ps2gs_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    return 0;
}

static int ps2gs_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2gs: DMA timeout\n");
    kfree(dev);
    ps2gs_open_count--;
    return 0;
}

/*
 *  Vector Processing Unit (/dev/ps2vpu0, /dev/ps2vpu1) driver
 */

static const struct {
    unsigned long ubase, ulen;
    unsigned long vubase, vulen;
} vumap[2] = {
    { 0x11000000,  4096, 0x11004000,  4096 },
    { 0x11008000, 16384, 0x1100c000, 16384 },
};

static int ps2vpu_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;
    struct ps2_vpuinfo vpuinfo;
    struct ps2_vifreg vifreg;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_VPUINFO:
	vpuinfo.umemsize = vumap[vusw].ulen;
	vpuinfo.vumemsize = vumap[vusw].vulen;
	return copy_to_user((void *)arg, &vpuinfo, sizeof(vpuinfo)) ? -EFAULT : 0;
    case PS2IOC_SVIFREG:
	if (copy_from_user(&vifreg, (void *)arg, sizeof(vifreg)))
	    return -EFAULT;
	if (vifreg.reg == PS2_VIFREG_MARK ||
	    vifreg.reg == PS2_VIFREG_FBRST ||
	    vifreg.reg == PS2_VIFREG_ERR)
	    VIFnREG(vusw, vifreg.reg) = vifreg.val;
	else
	    return -EINVAL;
	return 0;
    case PS2IOC_GVIFREG:
	if (copy_from_user(&vifreg, (void *)arg, sizeof(vifreg)))
	    return -EFAULT;
	if (vifreg.reg == PS2_VIFREG_STAT ||
	    (vifreg.reg >= PS2_VIFREG_ERR && vifreg.reg <= PS2_VIFREG_ITOPS) ||
	    vifreg.reg == PS2_VIFREG_ITOP ||
	    (vifreg.reg >= PS2_VIFREG_R0 && vifreg.reg <= PS2_VIFREG_C3) ||
	    (vusw == 1 &&
	     (vifreg.reg >= PS2_VIFREG_BASE && vifreg.reg <= PS2_VIFREG_TOP)))
	    vifreg.val = VIFnREG(vusw, vifreg.reg);
	else
	    return -EINVAL;
	return copy_to_user((void *)arg, &vifreg, sizeof(vifreg)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static u32 init_vif0code[] __attribute__((aligned(DMA_TRUNIT))) = {
    PS2_VIF_SET_CODE(0x0404, 0, PS2_VIF_STCYCL, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMASK, 0),
    0,
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMOD, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_ITOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_NOP, 0),
};

static void ps2vpu0_reset(void)
{
    VIF0REG(PS2_VIFREG_MARK) = 0;
    VIF0REG(PS2_VIFREG_ERR) = 2;
    VIF0REG(PS2_VIFREG_FBRST) = 1;		/* reset VIF0 */
    set_cp0_status(ST0_CU2, ST0_CU2);
    __asm__ __volatile__(
	".set	push\n"
	"	.set	r5900\n"
	"	sync.l\n"
	"	cfc2	$8, $vi28\n"
	"	ori	$8, $8, 0x0002\n"
	"	ctc2	$8, $vi28\n"
	"	sync.p\n"
	"	.set	pop"
	::: "$8");				/* reset VU0 */
    set_cp0_status(ST0_CU2, 0);
    move_quad(VIF0_FIFO, (unsigned long)&init_vif0code[0]);
    move_quad(VIF0_FIFO, (unsigned long)&init_vif0code[4]);
}

static u32 init_vif1code[] __attribute__((aligned(DMA_TRUNIT))) = {
    PS2_VIF_SET_CODE(0x0404, 0, PS2_VIF_STCYCL, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMASK, 0),
    0,
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_STMOD, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_MSKPATH3, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_BASE, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_OFFSET, 0),
    PS2_VIF_SET_CODE(0,      0, PS2_VIF_ITOP, 0),
};

static void ps2vpu1_reset(void)
{
    int apath;

    VIF1REG(PS2_VIFREG_MARK) = 0;
    VIF1REG(PS2_VIFREG_ERR) = 2;
    VIF1REG(PS2_VIFREG_FBRST) = 1;		/* reset VIF1 */
    set_cp0_status(ST0_CU2, ST0_CU2);
    __asm__ __volatile__(
	".set	push\n"
	"	.set	r5900\n"
	"	sync.l\n"
	"	cfc2	$8, $vi28\n"
	"	ori	$8, $8, 0x0200\n"
	"	ctc2	$8, $vi28\n"
	"	sync.p\n"
	"	.set	pop"
	::: "$8");				/* reset VU1 */
    set_cp0_status(ST0_CU2, 0);
    move_quad(VIF1_FIFO, (unsigned long)&init_vif1code[0]);
    move_quad(VIF1_FIFO, (unsigned long)&init_vif1code[4]);

    apath = (GIFREG(PS2_GIFREG_STAT) >> 10) & 3;
    if (apath == 1 || apath == 2) {
	GIFREG(PS2_GIFREG_CTRL) = 0x00000001;	/* reset GIF */
	store_double(GSSREG2(PS2_GSSREG_CSR), (u64)0x0100);	/* reset GS */
    }
}

static void set_cop2_usable(int onoff)
{
    /* get status register of the current process from caller stack frame */
    extern unsigned long kernelsp;
    unsigned long *status = (unsigned long *)(kernelsp - PT_SIZE + PT_STATUS);

    /* set/clear COP2 (VU0) usable bit */
    if (onoff)
	*status |= ST0_CU2;
    else
	*status &= ~ST0_CU2;
}

static int ps2vpu_open_count[2];	/* only one process can open */

static int ps2vpu_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;
    int vusw = MINOR_UNIT(inode->i_rdev);

    if (vusw < 0 || vusw > 1)
	return -ENODEV;
    if (ps2vpu_open_count[vusw])
	return -EBUSY;
    ps2vpu_open_count[vusw]++;

    if ((dev = ps2dma_dev_init(DMA_VIF0 + vusw, -1)) == NULL) {
	ps2vpu_open_count[vusw]--;
	return -ENOMEM;
    }
    file->private_data = dev;
    dev->data = (void *)vusw;

    if (vusw == 0) {
	ps2vpu0_reset();
	set_cop2_usable(1);
    } else if (vusw == 1) {
	ps2vpu1_reset();
    }
    return 0;
}

static int ps2vpu_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2vpu%d: DMA timeout\n", vusw);
    kfree(dev);

    if (vusw == 0)
	set_cop2_usable(0);
    ps2vpu_open_count[vusw]--;
    return 0;
}

static int ps2vpu_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;
    int vusw = (int)dev->data;
    unsigned long start, offset, len, mlen;

    if (vma->vm_offset & (PAGE_SIZE - 1))
	return -ENXIO;
    start = vma->vm_start;
    offset = vma->vm_offset;
    len = vma->vm_end - vma->vm_start;
    if (offset + len > vumap[vusw].ulen + vumap[vusw].vulen)
	return -EINVAL;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    /* map micro Mem */
    if (offset < vumap[vusw].ulen) {
	mlen = vumap[vusw].ulen - offset;
	if (mlen > len)
	    mlen = len;
	if (remap_page_range(start, vumap[vusw].ubase + offset, mlen,
			     vma->vm_page_prot))
	    return -EAGAIN;
	start += mlen;
	len -= mlen;
	offset = vumap[vusw].ulen;
    }

    /* map VU Mem */
    if (len > 0) {
	offset -= vumap[vusw].ulen;
	if (remap_page_range(start, vumap[vusw].vubase + offset, len, 
			     vma->vm_page_prot))
	    return -EAGAIN;
    }

    vma->vm_ops = &ps2dev_vmops;
    vma->vm_file = file;
    file->f_count++;
    return 0;
}

/*
 *  Image Processing Unit (/dev/ps2ipu) driver
 */

static int ps2ipu_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    u32 val32;
    u64 val64;
    static struct ps2_fifo fifo __attribute__((aligned(16)));

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    if ((result = ps2dev_recv_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_SIPUCMD:
	if (copy_from_user(&val32, (void *)arg, sizeof(val32)))
	    return -EFAULT;
	*(volatile u32 *)IPUREG_CMD = val32;
	return 0;
    case PS2IOC_GIPUCMD:
	val64 = load_double(IPUREG_CMD);
	if (val64 & ((u64)1 << 63))
	    return -EBUSY;		/* data is not valid */
	val32 = (u32)val64;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_SIPUCTRL:
	if (copy_from_user(&val32, (void *)arg, sizeof(val32)))
	    return -EFAULT;
	*(volatile u32 *)IPUREG_CTRL = val32;
	return 0;
    case PS2IOC_GIPUCTRL:
	val32 = *(volatile u32 *)IPUREG_CTRL;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_GIPUTOP:
	val64 = load_double(IPUREG_TOP);
	if (val64 & ((u64)1 << 63))
	    return -EBUSY;		/* data is not valid */
	val32 = (u32)val64;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_GIPUBP:
	val32 = *(volatile u32 *)IPUREG_BP;
	return copy_to_user((void *)arg, &val32, sizeof(val32)) ? -EFAULT : 0;
    case PS2IOC_SIPUFIFO:
	if (copy_from_user(&fifo, (void *)arg, sizeof(fifo)))
	    return -EFAULT;
	move_quad(IPU_I_FIFO, (unsigned long)&fifo);
	return 0;
    case PS2IOC_GIPUFIFO:
	move_quad((unsigned long)&fifo, IPU_O_FIFO);
	return copy_to_user((void *)arg, &fifo, sizeof(fifo)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static u32 init_ipu_iq[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x13101008, 0x16161310, 0x16161616, 0x1b1a181a,
    0x1a1a1b1b, 0x1b1b1a1a, 0x1d1d1d1b, 0x1d222222,
    0x1b1b1d1d, 0x20201d1d, 0x26252222, 0x22232325,
    0x28262623, 0x30302828, 0x38382e2e, 0x5345453a,
    0x10101010, 0x10101010, 0x10101010, 0x10101010,
};

static u32 init_ipu_vq[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x04210000, 0x03e00842, 0x14a51084, 0x1ce718c6,
    0x2529001f, 0x7c00294a, 0x35ad318c, 0x39ce7fff
};

static void wait_ipu_ready(void)
{
    while (*(volatile s32 *)IPUREG_CTRL < 0)
	;
}

static void ps2ipu_reset(void)
{
    *(volatile u32 *)IPUREG_CTRL = 1 << 30;		/* reset IPU */
    wait_ipu_ready();
    *(volatile u32 *)IPUREG_CMD = 0x00000000;		/* BCLR */
    wait_ipu_ready();

    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[0]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[4]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[8]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[12]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[16]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[16]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[16]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_iq[16]);
    *(volatile u32 *)IPUREG_CMD = 0x50000000;		/* SETIQ (I) */
    wait_ipu_ready();
    *(volatile u32 *)IPUREG_CMD = 0x58000000;		/* SETIQ (NI) */
    wait_ipu_ready();

    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_vq[0]);
    move_quad(IPU_I_FIFO, (unsigned long)&init_ipu_vq[4]);
    *(volatile u32 *)IPUREG_CMD = 0x60000000;		/* SETVQ */
    wait_ipu_ready();

    *(volatile u32 *)IPUREG_CMD = 0x90000000;		/* SETTH */
    wait_ipu_ready();

    *(volatile u32 *)IPUREG_CTRL = 1 << 30;		/* reset IPU */
    wait_ipu_ready();
    *(volatile u32 *)IPUREG_CMD = 0x00000000;		/* BCLR */
    wait_ipu_ready();
}

static int ps2ipu_open_count = 0;	/* only one process can open */

static int ps2ipu_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2ipu_open_count)
	return -EBUSY;
    ps2ipu_open_count++;

    if ((dev = ps2dma_dev_init(DMA_IPU_to, DMA_IPU_from)) == NULL) {
	ps2ipu_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    ps2ipu_reset();
    return 0;
}

static int ps2ipu_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2ipu: DMA timeout\n");
    kfree(dev);
    ps2ipu_open_count--;
    return 0;
}

static int ps2ipu_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long start, offset, len;

    if (vma->vm_offset & (PAGE_SIZE - 1))
	return -ENXIO;
    start = vma->vm_start;
    offset = vma->vm_offset;
    len = vma->vm_end - vma->vm_start;
    if (offset + len > PAGE_SIZE * 2)
	return -EINVAL;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    /* map IPU registers */
    if (offset < PAGE_SIZE) {
	if (remap_page_range(start, 0x10002000, PAGE_SIZE, vma->vm_page_prot))
	    return -EAGAIN;
	start += PAGE_SIZE;
	len -= PAGE_SIZE;
    }
    /* map IPU FIFO */
    if (len > 0) {
	if (remap_page_range(start, 0x10007000, PAGE_SIZE, vma->vm_page_prot))
	    return -EAGAIN;
    }

    vma->vm_ops = &ps2dev_vmops;
    vma->vm_file = file;
    file->f_count++;
    return 0;
}

/*
 *  Scratchpad RAM (/dev/ps2spr) driver
 */

static int ps2spr_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
    int result;
    struct dma_device *dev = (struct dma_device *)file->private_data;
    struct ps2_sprinfo sprinfo;

    if ((result = ps2dev_send_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    if ((result = ps2dev_recv_ioctl(dev, cmd, arg)) != -ENOIOCTLCMD)
	return result;
    switch (cmd) {
    case PS2IOC_SPRINFO:
	sprinfo.size = SPR_SIZE;
	return copy_to_user((void *)arg, &sprinfo, sizeof(sprinfo)) ? -EFAULT : 0;
    }
    return -EINVAL;
}

static int ps2spr_open_count = 0;	/* only one process can open */

static int ps2spr_open(struct inode *inode, struct file *file)
{
    struct dma_device *dev;

    if (ps2spr_open_count)
	return -EBUSY;
    ps2spr_open_count++;

    if ((dev = ps2dma_dev_init(DMA_SPR_to, DMA_SPR_from)) == NULL) {
	ps2spr_open_count--;
	return -ENOMEM;
    }
    file->private_data = dev;
    return 0;
}

static int ps2spr_release(struct inode *inode, struct file *file)
{
    struct dma_device *dev = (struct dma_device *)file->private_data;

    if (ps2dma_finish(dev) != 0)
	printk("ps2spr: DMA timeout\n");
    kfree(dev);
    ps2spr_open_count--;
    return 0;
}

static int ps2spr_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long start;
    struct vm_area_struct *svma;

    if (vma->vm_offset != 0)
	return -ENXIO;
    if (vma->vm_end - vma->vm_start != SPR_SIZE)
	return -EINVAL;

    /* make vma address range fit 16KB alignment */
    start = (vma->vm_start + SPR_SIZE - 1) & (PAGE_MASK << 2);
    for (svma = find_vma(current->mm, start); ; svma = svma->vm_next) {
	if (start + SPR_SIZE > TASK_SIZE)
	    return -ENOMEM;
	if (svma == NULL || start + SPR_SIZE <= svma->vm_start)
	    break;
	start = (svma->vm_end + SPR_SIZE - 1) & (PAGE_MASK << 2);
    }
    vma->vm_start = start;
    vma->vm_end = vma->vm_start + SPR_SIZE;

#ifdef __mips__
    pgprot_val(vma->vm_page_prot) = (pgprot_val(vma->vm_page_prot) & ~_CACHE_MASK) | _CACHE_UNCACHED;
#else
#error "for MIPS CPU only"
#endif
    vma->vm_flags |= VM_IO;

    if (remap_page_range(vma->vm_start, 0x80000000, SPR_SIZE, vma->vm_page_prot))
	return -EAGAIN;
    vma->vm_ops = &ps2dev_vmops;
    vma->vm_file = file;
    file->f_count++;
    return 0;
}

/*
 *  file operations structures
 */

struct file_operations ps2gs_fops = {
    ps2dev_llseek,	/* llseek (error) */
    NULL,		/* read */
    ps2dev_write,	/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    ps2gs_ioctl,	/* ioctl */
    NULL,		/* mmap */
    ps2gs_open,		/* open */
    NULL,		/* flush */
    ps2gs_release,	/* release */
    ps2dev_fsync,	/* fsync */
    NULL,		/* fasync */
};

struct file_operations ps2vpu_fops = {
    ps2dev_llseek,	/* llseek (error) */
    NULL,		/* read */
    ps2dev_write,	/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    ps2vpu_ioctl,	/* ioctl */
    ps2vpu_mmap,	/* mmap */
    ps2vpu_open,	/* open */
    NULL,		/* flush */
    ps2vpu_release,	/* release */
    ps2dev_fsync,	/* fsync */
    NULL,		/* fasync */
};

struct file_operations ps2ipu_fops = {
    ps2dev_llseek,	/* llseek (error) */
    ps2dev_read,	/* read */
    ps2dev_write,	/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    ps2ipu_ioctl,	/* ioctl */
    ps2ipu_mmap,	/* mmap */
    ps2ipu_open,	/* open */
    NULL,		/* flush */
    ps2ipu_release,	/* release */
    ps2dev_fsync,	/* fsync */
    NULL,		/* fasync */
};

struct file_operations ps2spr_fops = {
    ps2dev_llseek,	/* llseek (error) */
    NULL,		/* read */
    NULL,		/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    ps2spr_ioctl,	/* ioctl */
    ps2spr_mmap,	/* mmap */
    ps2spr_open,	/* open */
    NULL,		/* flush */
    ps2spr_release,	/* release */
    ps2dev_fsync,	/* fsync */
    NULL,		/* fasync */
};

static int ps2dev_init_open(struct inode *inode, struct file *file)
{
    switch (MINOR_FUNC(inode->i_rdev)) {
    case PS2MEM_FUNC:
#ifdef CONFIG_PS2DEV_COMPAT
    {
	int ps2cp_init_open(struct inode *inode, struct file *file);

	if (ps2cp_init_open(inode, file) == 0)
	    return 0;
    }
#endif
        file->f_op = &ps2mem_fops;
        break;
    case PS2EVENT_FUNC:
        file->f_op = &ps2ev_fops;
        break;
    case PS2GS_FUNC:
        file->f_op = &ps2gs_fops;
        break;
    case PS2VPU_FUNC:
        file->f_op = &ps2vpu_fops;
        break;
    case PS2IPU_FUNC:
        file->f_op = &ps2ipu_fops;
        break;
    case PS2SPR_FUNC:
        file->f_op = &ps2spr_fops;
        break;
    default:
        return -ENXIO;
    }
    if (file->f_op && file->f_op->open)
        return file->f_op->open(inode, file);
    return 0;
}

static struct file_operations ps2dev_init_fops = {
    NULL,		/* lseek */
    NULL,		/* read */
    NULL,		/* write */
    NULL,		/* readdir */
    NULL,		/* poll */
    NULL,		/* ioctl */
    NULL,		/* mmap */
    ps2dev_init_open,	/* just a selector for the real open */
    NULL,		/* flush */
    NULL,		/* release */
    NULL,		/* fsync */
    NULL,		/* fasync */
};

void * __ioremap(unsigned long, unsigned long, unsigned long);

__initfunc(void ps2dev_init(void))
{
    extern struct dma_channel ps2dma_channels[];
    u64 gs_revision;
    
    ps2ev_init();
    ps2dma_init();
    ps2dma_channels[DMA_GIF].reset = ps2gif_reset;
    ps2dma_channels[DMA_VIF0].reset = ps2vpu0_reset;
    ps2dma_channels[DMA_VIF1].reset = ps2vpu1_reset;
    ps2dma_channels[DMA_IPU_to].reset = ps2ipu_reset;
    ps2gs_get_gssreg(PS2_GSSREG_CSR, &gs_revision);

    /* map scratchpad RAM */
    ps2spr_vaddr = __ioremap(0x80000000, SPR_SIZE, _CACHE_UNCACHED);

    if (register_chrdev(PS2DEV_MAJOR, "ps2dev", &ps2dev_init_fops))
	printk("unable to get major %d for PlayStation 2 devices\n",
	       PS2DEV_MAJOR);
    printk("PlayStation 2 device support: GIF, VIF, GS, VU, IPU, SPR\n");
    printk("Graphics Synthesizer revision: %08x\n",
	   ((u32)gs_revision >> 16) & 0xffff);

#ifdef CONFIG_DUMMY_CONSOLE
    {
	extern struct consw ps2_con;
	take_over_console(&ps2_con, 0, MAX_NR_CONSOLES - 1, 1);
    }
#endif
}
