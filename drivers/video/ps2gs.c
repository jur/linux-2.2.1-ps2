/*
 *  linux/drivers/video/ps2gs.c
 *  PlayStation 2 miscellaneous Graphics Synthesizer functions
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/ps2/irq.h>
#include <asm/ps2/sysconf.h>
#include <asm/ps2/sbcall.h>

#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>
#include "ps2dma.h"
#include "ps2dev.h"

static int gs_mode;
static int gs_pmode;
static int gs_dx[2], gs_dy[2];

void ps2_setdve(int);
int ps2_setgscrt(int, int, int, int *, int *, int *, int *);


/*
 *  PCRTC sync parameters
 */

#define vSMODE1(VHP,VCKSEL,SLCK2,NVCK,CLKSEL,PEVS,PEHS,PVS,PHS,GCONT,SPML,PCK2,XPCK,SINT,PRST,EX,CMOD,SLCK,T1248,LC,RC)	\
	(((u64)(VHP)<<36)   | ((u64)(VCKSEL)<<34) | ((u64)(SLCK2)<<33) | \
	 ((u64)(NVCK)<<32)  | ((u64)(CLKSEL)<<30) | ((u64)(PEVS)<<29)  | \
	 ((u64)(PEHS)<<28)  | ((u64)(PVS)<<27)    | ((u64)(PHS)<<26)   | \
	 ((u64)(GCONT)<<25) | ((u64)(SPML)<<21)   | ((u64)(PCK2)<<19)  | \
	 ((u64)(XPCK)<<18)  | ((u64)(SINT)<<17)   | ((u64)(PRST)<<16)  | \
	 ((u64)(EX)<<15)    | ((u64)(CMOD)<<13)   | ((u64)(SLCK)<<12)  | \
	 ((u64)(T1248)<<10) | ((u64)(LC)<<3)      | ((u64)(RC)<<0))
#define vSYNCH1(HS,HSVS,HSEQ,HBP,HFP)	\
	(((u64)(HS)<<43) | ((u64)(HSVS)<<32) | ((u64)(HSEQ)<<22) | \
	 ((u64)(HBP)<<11) | ((u64)(HFP)<<0))
#define vSYNCH2(HB,HF) \
	(((u64)(HB)<<11) | ((u64)(HF)<<0))
#define vSYNCV(VS,VDP,VBPE,VBP,VFPE,VFP) \
	(((u64)(VS)<<53) | ((u64)(VDP)<<42) | ((u64)(VBPE)<<32) | \
	 ((u64)(VBP)<<20) | ((u64)(VFPE)<<10) | ((u64)(VFP)<<0))

struct rdisplay {
    int magv, magh, dy, dx;
};
#define vDISPLAY(DH,DW,MAGV,MAGH,DY,DX) \
	{ (MAGV), (MAGH), (DY), (DX) }
#define wDISPLAY(DH,DW,MAGV,MAGH,DY,DX) \
	(((u64)(DH)<<44) | ((u64)(DW)<<32) | ((u64)(MAGV)<<27) | \
	 ((u64)(MAGH)<<23) | ((u64)(DY)<<12) | ((u64)(DX)<<0))

struct syncparam {
    int width, height, rheight, dvemode;
    u64 smode1, smode2, srfsh, synch1, synch2, syncv;
    struct rdisplay display;
};


static const struct syncparam syncdata0[] = {
    /* 0: NTSC-NI (640x240(224)) */
    { 640, 240, 224, 0,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,2,0, 1,32,4), 0, 8,
      vSYNCH1(254,1462,124,222,64), vSYNCH2(1652,1240),
      vSYNCV(6,480,6,26,6,2), vDISPLAY(239,2559,0,3,25,632) },

    /* 1: NTSC-I (640x480(448)) */
    { 640, 480, 448, 0,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,2,0, 1,32,4), 1, 8,
      vSYNCH1(254,1462,124,222,64), vSYNCH2(1652,1240),
      vSYNCV(6,480,6,26,6,1), vDISPLAY(479,2559,0,3,50,632) },


    /* 2: PAL-NI (640x288(256)) */
    { 640, 288, 256, 1,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,3,0, 1,32,4), 0, 8,
      vSYNCH1(254,1474,127,262,48), vSYNCH2(1680,1212),
      vSYNCV(5,576,5,33,5,4), vDISPLAY(287,2559,0,3,36,652) },

    /* 3: PAL-I (640x576(512)) */
    { 640, 576, 512, 1,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,3,0, 1,32,4), 1, 8,
      vSYNCH1(254,1474,127,262,48), vSYNCH2(1680,1212),
      vSYNCV(5,576,5,33,5,1), vDISPLAY(575,2559,0,3,72,652) },


    /* 4: VESA-1A (640x480 59.940Hz) */
    { 640, 480, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,15,2), 0, 4,
      vSYNCH1(192,608,192,84,32), vSYNCH2(768,524),
      vSYNCV(2,480,0,33,0,10), vDISPLAY(479,1279,0,1,34,276) },

    /* 5: VESA-1C (640x480 75.000Hz) */
    { 640, 480, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,28,3), 0, 4,
      vSYNCH1(128,712,128,228,32), vSYNCH2(808,484),
      vSYNCV(3,480,0,16,0,1), vDISPLAY(479,1279,0,1,18,356) },


    /* 6:  VESA-2B (800x600 60.317Hz) */
    { 800, 600, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,71,6), 0, 4,
      vSYNCH1(256,800,256,164,80), vSYNCH2(976,636),
      vSYNCV(4,600,0,23,0,1), vDISPLAY(599,1599,0,1,26,420) },

    /* 7: VESA-2D (800x600 75.000Hz) */
    { 800, 600, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,44,3), 0, 4,
      vSYNCH1(160,896,160,308,32), vSYNCH2(1024,588),
      vSYNCV(3,600,0,21,0,1), vDISPLAY(599,1599,0,1,23,468) },


    /* 8: VESA-3B (1024x768 60.004Hz) */
    { 1024, 768, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 0,58,6), 0, 4,
      vSYNCH1(272,1072,272,308,48), vSYNCH2(1296,764),
      vSYNCV(6,768,0,29,0,3), vDISPLAY(767,2047,0,1,34,580) },

    /* 9: VESA-3D (1024x768 75.029Hz) */
    { 1024, 768, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,35,3), 0, 2,
      vSYNCH1(96,560,96,164,16), vSYNCH2(640,396),
      vSYNCV(3,768,0,28,0,1), vDISPLAY(767,1023,0,0,30,260) },


    /* 10: VESA-4A (1280x1024 60.020Hz) */
    { 1280, 1024, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 0,8,1), 0, 2,
      vSYNCH1(112,732,112,236,16), vSYNCH2(828,496),
      vSYNCV(3,1024,0,38,0,1), vDISPLAY(1023,1279,0,0,40,348) },

    /* 11: VESA-4B (1280x1024 75.025Hz) */
    { 1280, 1024, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 0,10,1), 0, 2,
      vSYNCH1(144,700,144,236,16), vSYNCH2(828,464),
      vSYNCV(3,1024,0,38,0,1), vDISPLAY(1023,1279,0,0,40,380) },


    /* 12: DTV-480P (720x480) */
    { 720, 480, -1, 3,
      vSMODE1(1, 1,1,1,1, 0,0, 0,0,0,2,0,0,1,1,0,0,0, 1,32,4), 0, 4,
      vSYNCH1(128,730,128,104,32), vSYNCH2(826,626),
      vSYNCV(6,483,0,30,0,6), vDISPLAY(479,1439,0,1,35,232) },

    /* 13: DTV-1080I (1920x1080) */
    { 1920, 1080, -1, 4,
      vSMODE1(0, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,22,2), 1, 4,
      vSYNCH1(104,1056,44,134,30), vSYNCH2(1064,868),
      vSYNCV(10,1080,2,28,0,5), vDISPLAY(1079,1919,0,0,40,238) },

    /* 14: DTV-720P (1280x720) */
    { 1280, 720, -1, 5,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,22,2), 0, 4,
      vSYNCH1(104,785,40,198,62), vSYNCH2(763,529),
      vSYNCV(5,720,0,20,0,5), vDISPLAY(719,1279,0,0,24,302) },
};


/* GS rev.19 or later */

static const struct syncparam syncdata1[] = {
    /* 0: NTSC-NI (640x240(224)) */
    { 640, 240, 224, 0,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,2,0, 1,32,4), 0, 8,
      vSYNCH1(254,1462,124,222,64), vSYNCH2(1652,1240),
      vSYNCV(6,480,6,26,6,2), vDISPLAY(239,2559,0,3,25,632) },

    /* 1: NTSC-I (640x480(448)) */
    { 640, 480, 448, 0,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,2,0, 1,32,4), 1, 8,
      vSYNCH1(254,1462,124,222,64), vSYNCH2(1652,1240),
      vSYNCV(6,480,6,26,6,1), vDISPLAY(479,2559,0,3,50,632) },


    /* 2: PAL-NI (640x288(256)) */
    { 640, 288, 256, 1,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,3,0, 1,32,4), 0, 8,
      vSYNCH1(254,1474,127,262,48), vSYNCH2(1680,1212),
      vSYNCV(5,576,5,33,5,4), vDISPLAY(287,2559,0,3,36,652) },

    /* 3: PAL-I (640x576(512)) */
    { 640, 576, 512, 1,
      vSMODE1(0, 1,1,1,1, 0,0, 0,0,0,4,0,0,1,1,0,3,0, 1,32,4), 1, 8,
      vSYNCH1(254,1474,127,262,48), vSYNCH2(1680,1212),
      vSYNCV(5,576,5,33,5,1), vDISPLAY(575,2559,0,3,72,652) },


    /* 4: VESA-1A (640x480 59.940Hz) */
    { 640, 480, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,15,2), 0, 4,
      vSYNCH1(192,608,192,81,47), vSYNCH2(753,527),
      vSYNCV(2,480,0,33,0,10), vDISPLAY(479,1279,0,1,34,272) },

    /* 5: VESA-1C (640x480 75.000Hz) */
    { 640, 480, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,28,3), 0, 4,
      vSYNCH1(128,712,128,225,47), vSYNCH2(793,487),
      vSYNCV(3,480,0,16,0,1), vDISPLAY(479,1279,0,1,18,352) },


    /* 6:  VESA-2B (800x600 60.317Hz) */
    { 800, 600, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,71,6), 0, 4,
      vSYNCH1(256,800,256,161,95), vSYNCH2(961,639),
      vSYNCV(4,600,0,23,0,1), vDISPLAY(599,1599,0,1,26,416) },

    /* 7: VESA-2D (800x600 75.000Hz) */
    { 800, 600, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 1,44,3), 0, 4,
      vSYNCH1(160,896,160,305,47), vSYNCH2(1009,591),
      vSYNCV(3,600,0,21,0,1), vDISPLAY(599,1599,0,1,23,464) },


    /* 8: VESA-3B (1024x768 60.004Hz) */
    { 1024, 768, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,2,0,0,1,0,0,0,0, 0,58,6), 0, 4,
      vSYNCH1(272,1072,272,305,63), vSYNCH2(1281,767),
      vSYNCV(6,768,0,29,0,3), vDISPLAY(767,2047,0,1,34,576) },

    /* 9: VESA-3D (1024x768 75.029Hz) */
    { 1024, 768, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,35,3), 0, 2,
      vSYNCH1(96,560,96,161,31), vSYNCH2(625,399),
      vSYNCV(3,768,0,28,0,1), vDISPLAY(767,1023,0,0,30,256) },


    /* 10: VESA-4A (1280x1024 60.020Hz) */
    { 1280, 1024, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 0,8,1), 0, 2,
      vSYNCH1(112,732,112,233,63), vSYNCH2(781,499),
      vSYNCV(3,1024,0,38,0,1), vDISPLAY(1023,1279,0,0,40,344) },

    /* 11: VESA-4B (1280x1024 75.025Hz) */
    { 1280, 1024, -1, 2,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 0,10,1), 0, 2,
      vSYNCH1(144,700,144,233,31), vSYNCH2(813,467),
      vSYNCV(3,1024,0,38,0,1), vDISPLAY(1023,1279,0,0,40,376) },


    /* 12: DTV-480P (720x480) */
    { 720, 480, -1, 3,
      vSMODE1(1, 1,1,1,1, 0,0, 0,0,0,2,0,0,1,1,0,0,0, 1,32,4), 0, 4,
      vSYNCH1(128,730,128,101,47), vSYNCH2(811,629),
      vSYNCV(6,483,0,30,0,6), vDISPLAY(479,1439,0,1,35,228) },

    /* 13: DTV-1080I (1920x1080) */
    { 1920, 1080, -1, 4,
      vSMODE1(0, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,22,2), 1, 4,
      vSYNCH1(104,1056,44,131,45), vSYNCH2(1012,908),
      vSYNCV(10,1080,2,28,0,5), vDISPLAY(1079,1919,0,0,40,234) },

    /* 14: DTV-720P (1280x720) */
    { 1280, 720, -1, 5,
      vSMODE1(1, 0,1,1,1, 0,0, 0,0,0,1,0,0,1,0,0,0,0, 1,22,2), 0, 4,
      vSYNCH1(104,785,40,195,71), vSYNCH2(715,565),
      vSYNCV(5,720,0,20,0,5), vDISPLAY(719,1279,0,0,24,298) },
};

static const struct syncparam *syncdata = syncdata0;

struct syncindex {
    int index;
    int modes;
    const struct syncindex *submode;
};

static const struct syncindex vesaindex[] = {
    {4, 2, NULL},	/* 0: 640x480 */
    {6, 2, NULL},	/* 1: 800x600 */
    {8, 2, NULL},	/* 2: 1024x768 */
    {10, 2, NULL},	/* 3: 1280x1024 */
};
static const struct syncindex dtvindex[] = {
    {12, 1, NULL},	/* 0: 480P */
    {13, 1, NULL},	/* 1: 1080I */
    {14, 1, NULL},	/* 2: 720P */
};
static const struct syncindex syncindex[] = {
    {4, 4, vesaindex},	/* 0: VESA */
    {12, 3, dtvindex},	/* 1: DTV */
    {0, 2, NULL},	/* 2: NTSC */
    {2, 2, NULL},	/* 3: PAL */
};

static const int gscrtmode[] = {
    0x02,	/* 0: NTSC-NI (640x240(224)) */
    0x02,	/* 1: NTSC-I (640x480(448)) */
    0x03,	/* 2: PAL-NI (640x288(256)) */
    0x03,	/* 3: PAL-I (640x576(512)) */
    0x1a,	/* 4: VESA-1A (640x480 59.940Hz) */
    0x1c,	/* 5: VESA-1C (640x480 75.000Hz) */
    0x2b,	/* 6: VESA-2B (800x600 60.317Hz) */
    0x2d,	/* 7: VESA-2D (800x600 75.000Hz) */
    0x3b,	/* 8: VESA-3B (1024x768 60.004Hz) */
    0x3d,	/* 9: VESA-3D (1024x768 75.029Hz) */
    0x4a,	/* 10: VESA-4A (1280x1024 60.020Hz) */
    0x4b,	/* 11: VESA-4B (1280x1024 75.025Hz) */
    0x50,	/* 12: DTV-480P (720x480) */
    0x51,	/* 13: DTV-1080I (1920x1080) */
    0x52,	/* 14: DTV-720P (1280x720) */
};

/*
 *  low-level PCRTC initialize
 */

static void setcrtc_old(int mode, int ffmd);
static int setcrtc_new(int mode, int ffmd);

static void setcrtc(int mode, int ffmd)
{
    u64 val;

    ps2gs_get_gssreg(PS2_GSSREG_PMODE, &val);
    ps2gs_set_gssreg(PS2_GSSREG_PMODE, val & ~(u64)3);

    if (setcrtc_new(mode, ffmd) < 0)
	setcrtc_old(mode, ffmd);

    ps2gs_set_gssreg(PS2_GSSREG_PMODE, val);
}      

static void setcrtc_old(int mode, int ffmd)
{
    u64 smode1 = syncdata[mode].smode1;

    if (syncdata[mode].dvemode != 2)		/* not VESA */
	smode1 |= (u64)(ps2_sysconf->video & 1) << 25;	/* RGBYC */

    ps2gs_set_gssreg(PS2_GSSREG_SMODE1, smode1 | ((u64)1 << 16));
    ps2gs_set_gssreg(PS2_GSSREG_SYNCH1, syncdata[mode].synch1);
    ps2gs_set_gssreg(PS2_GSSREG_SYNCH2, syncdata[mode].synch2);
    ps2gs_set_gssreg(PS2_GSSREG_SYNCV, syncdata[mode].syncv);
    ps2gs_set_gssreg(PS2_GSSREG_SMODE2, syncdata[mode].smode2 + (ffmd << 1));
    ps2gs_set_gssreg(PS2_GSSREG_SRFSH, syncdata[mode].srfsh);

    if (syncdata[mode].dvemode == 2 ||
	syncdata[mode].dvemode == 4 ||
	syncdata[mode].dvemode == 5) {	/* for VESA, DTV1080I,720P */
	/* PLL on */
	ps2gs_set_gssreg(PS2_GSSREG_SMODE1, smode1 & ~((u64)1 << 16));
	udelay(2500);	/* wait 2.5ms */
    }

    /* sync start */
    ps2gs_set_gssreg(PS2_GSSREG_SMODE1,
		     smode1 & ~((u64)1 << 16) & ~((u64)1 << 17));
    ps2_setdve(syncdata[mode].dvemode);

    /* get DISPLAY register offset */
    gs_dx[0] = gs_dx[1] = syncdata[mode].display.dx;
    gs_dy[0] = gs_dy[1] = syncdata[mode].display.dy;
}      

static int setcrtc_new(int mode, int ffmd)
{
    static int ps2gs_set_gssreg_dummy(int reg, u64 val);
    int dx1, dy1, dx2, dy2;
    int result;
    u64 smode1 = syncdata[mode].smode1;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
    if (sbios(SB_GETVER, NULL) < 0x0250) {
    	return -1;
    }
#endif

    if (syncdata[mode].dvemode != 2)		/* not VESA */
	smode1 |= (u64)(ps2_sysconf->video & 1) << 25;	/* RGBYC */

    /* set gssreg dummy value */
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SYNCH1, syncdata[mode].synch1);
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SYNCH2, syncdata[mode].synch2);
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SYNCV, syncdata[mode].syncv);
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SMODE2, syncdata[mode].smode2 + (ffmd << 1));
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SRFSH, syncdata[mode].srfsh);
    ps2gs_set_gssreg_dummy(PS2_GSSREG_SMODE1,
			   smode1 & ~((u64)1 << 16) & ~((u64)1 << 17));

    /* PCRTC real initialize */
    result = ps2_setgscrt(syncdata[mode].smode2 & 1, gscrtmode[mode], ffmd,
			  &dx1, &dy1, &dx2, &dy2);

    /* get DISPLAY register offset */
    if (mode < 4) {
	/* NTSC, PAL mode */
	gs_dx[0] = gs_dx[1] = syncdata[mode].display.dx;
	gs_dy[0] = gs_dy[1] = syncdata[mode].display.dy;
    } else {
	/* VESA, DTV mode */
	gs_dx[0] = syncdata0[mode].display.dx + dx1;
	gs_dy[0] = syncdata0[mode].display.dy + dy1;
	gs_dx[1] = syncdata0[mode].display.dx + dx2;
	gs_dy[1] = syncdata0[mode].display.dy + dy2;
    }

    return result;
}      

/*
 *  PCRTC mode set functions
 */

int ps2gs_setcrtmode(int mode, int res)
{
    u64 csr;
    int param;
    int res0 = res & 0xff;		/* resolution */
    int res1 = (res >> 8) & 0xff;	/* sync select */
    int res2 = (res >> 16) & 0x01;	/* ffmd */

    /* GS revision check */
    ps2gs_get_gssreg(PS2_GSSREG_CSR, &csr);
    if (((csr >> 16) & 0xff) < 0x19)
	syncdata = syncdata0;
    else
	syncdata = syncdata1;

    if (mode >= sizeof(syncindex) / sizeof(*syncindex) ||
	syncindex[mode].index < 0)
	return -1;
    if (res0 >= syncindex[mode].modes)
	return -1;
    if (syncindex[mode].submode == NULL) {
	param = syncindex[mode].index + res0;
    } else {
	if (res1 == 0)		/* highest sync rate */
	    param = (syncindex[mode].submode)[res0].index +
		(syncindex[mode].submode)[res0].modes - 1;
	else if (res1 <= (syncindex[mode].submode)[res0].modes)
	    param = (syncindex[mode].submode)[res0].index + res1 - 1;
	else
	    return -1;
    }
    gs_mode = param;

    setcrtc(param, res2);
    return 0;
}

int ps2gs_setdisplay(int ch, int w, int h, int dx, int dy)
{
    const struct syncparam *p = &syncdata[gs_mode];
    u64 display;
    int magh, magv;
    int pdh, pdw, pmagv, pmagh, pdy, pdx;

    if (w <= 0 || w >= 2048 || h <= 0 || h >= 2048 || ch < 0 || ch > 1)
	return -1;

    magh = p->display.magh + 1;
    magv = p->display.magv + 1;
    pmagh = p->width * magh / w;
    pmagv = p->height * magv / h;

    if (pmagh == 0 || pmagv == 0 || pmagh > 16 || pmagv > 4)
	return -1;

    pdh = h * pmagv;
    if (p->rheight > 0) {
	if (p->rheight * magv > pdh)
	    pdy = gs_dy[ch] + (p->rheight * magv - pdh) / 2 + dy * pmagv;
	else
	    pdy = gs_dy[ch] + dy * pmagv;
    } else {
	pdy = gs_dy[ch] + (p->height * magv - pdh) / 2 + dy * pmagv;
    }

    pdw = w * pmagh;
    pdx = gs_dx[ch] + (p->width * magh - pdw) / 2 + dx * pmagh;

    if (pdx < 0 || pdy < 0)
	return -1;

    display = wDISPLAY((pdh - 1) & 0x7ff, (pdw - 1) & 0xfff,
		       pmagv - 1, pmagh - 1, pdy & 0x7ff, pdx & 0xfff);

    if (ch == 0)
	ps2gs_set_gssreg(PS2_GSSREG_DISPLAY1, display);
    else if (ch == 1)
	ps2gs_set_gssreg(PS2_GSSREG_DISPLAY2, display);

    return 0;
}

int ps2gs_setdispfb(int ch, int fbp, int fbw, int psm, int dbx, int dby)
{
    u64 dispfb;

    dispfb = (fbp & 0x1ff) + ((fbw & 0x3f) << 9) +
	((psm & 0x1f) << 15) +
	((u64)(dbx & 0x7ff) << 32) +
	((u64)(dby & 0x7ff) << 43);

    if (ch == 0)
	ps2gs_set_gssreg(PS2_GSSREG_DISPFB1, dispfb);
    else if (ch == 1)
	ps2gs_set_gssreg(PS2_GSSREG_DISPFB2, dispfb);
    else
	return -1;

    return 0;
}

int ps2gs_setpmode(int sw, int mmod, int amod, int slbg, int alp)
{
    u64 pmode;

    pmode = ((u64)sw & 0x3) | ((u64)1 << 2) | (((u64)mmod & 1) << 5) |
	(((u64)amod & 1) << 6) | (((u64)slbg & 1) << 7) |
	(((u64)alp & 0xff) << 8);
    ps2gs_set_gssreg(PS2_GSSREG_PMODE, pmode);
    gs_pmode = pmode;

    return 0;
}

int ps2gs_setdpms(int mode)
{
    u64 val;

    if (syncdata[gs_mode].dvemode == 2) {
	/* VESA mode */
	ps2gs_get_gssreg(PS2_GSSREG_SMODE2, &val);
	val = (val & 0x3) + ((mode & 0x3) << 2);
	ps2gs_set_gssreg(PS2_GSSREG_SMODE2, val);
    }
    return 0;
}

int ps2gs_blank(int onoff)
{
    u64 val;

    ps2gs_get_gssreg(PS2_GSSREG_PMODE, &val);
    val &= ~3;				/* ch. off */
    if (!onoff)
	val |= gs_pmode & 3;		/* restore pmode */
    ps2gs_set_gssreg(PS2_GSSREG_PMODE, val);
    return 0;
}

/*
 *  GS register read / write
 */

static u64 gssreg[0x10];

static int ps2gs_set_gssreg_dummy(int reg, u64 val)
{
    if (reg >= PS2_GSSREG_PMODE && reg <= PS2_GSSREG_BGCOLOR) {
	gssreg[reg] = val;
    } else
	return -1;	/* bad register no. */
    return 0;
}

int ps2gs_set_gssreg(int reg, u64 val)
{
    if (reg >= PS2_GSSREG_PMODE && reg <= PS2_GSSREG_BGCOLOR) {
	gssreg[reg] = val;
	store_double(GSSREG1(reg), val);
    } else if (reg == PS2_GSSREG_CSR) {
	val &= 1 << 8;
	store_double(GSSREG2(reg), val);
    } else if (reg == PS2_GSSREG_SIGLBLID) {
	store_double(GSSREG2(reg), val);
    } else
	return -1;	/* bad register no. */
    return 0;
}

int ps2gs_get_gssreg(int reg, u64 *val)
{
    if (reg == PS2_GSSREG_CSR || reg == PS2_GSSREG_SIGLBLID) {
	/* readable register */
	*val = load_double(GSSREG2(reg));
    } else if (reg >= 0 && reg <= 0x0e) {
	/* write only register .. return saved value */
	*val = gssreg[reg];
    } else
	return -1;	/* bad register no. */
    return 0;
}

int ps2gs_set_gsreg(int reg, u64 val)
{
    struct {
    	ps2_giftag giftag; // 128bit
	u64 param[2];
    } packet;

    PS2_GIFTAG_CLEAR_TAG(&(packet.giftag));
    packet.giftag.NLOOP = 1;
    packet.giftag.EOP = 1;
    packet.giftag.PRE = 0;
    packet.giftag.PRIM = 0;
    packet.giftag.FLG = PS2_GIFTAG_FLG_PACKED;
    packet.giftag.NREG = 1;
    packet.giftag.REGS0 = PS2_GIFTAG_REGS_AD;
    packet.param[0] = val;
    packet.param[1] = reg;

    ps2sdma_send(DMA_GIF, &packet, sizeof(packet));
    return 0;
}
