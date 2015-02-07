/*
 * sbcall.h
 *
 *	Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sbcall.h,v 1.14.2.5 2001/11/14 04:38:33 nakamura Exp $
 */

#ifndef __ASM_PS2_SBCALL_H
#define __ASM_PS2_SBCALL_H

#define SBIOS_VERSION	0x0250

extern int (*sbios)(int, void *);

#define SB_GETVER		0
#define SB_HALT			1
struct sb_halt_arg {
    int mode;
};
#define SB_SETDVE		2
struct sb_setdve_arg {
    int mode;
};
#define SB_PUTCHAR		3
struct sb_putchar_arg {
    char c;
};
#define SB_GETCHAR		4
#define SB_SETGSCRT		5
struct sb_setgscrt_arg {
    int inter;
    int omode;
    int ffmode;
    int *dx1, *dy1, *dx2, *dy2;
};

/** Debug output in TGE (not supported by RTE). */
#define SB_SET_PRINTS_CALLBACK 15

/*
 *  SIF DMA services
 */

#define SB_SIFINIT		16
#define SB_SIFEXIT		17
#define SB_SIFSETDMA		18
struct sb_sifsetdma_arg {
    void *sdd;
    int len;
};
#define SB_SIFDMASTAT		19
struct sb_sifdmastat_arg {
    int id;
};
#define SB_SIFSETDCHAIN		20

/*
 *  SIF CMD services
 */

#define SB_SIFINITCMD		32
#define SB_SIFEXITCMD		33
#define SB_SIFSENDCMD		34
struct sb_sifsendcmd_arg {
    u_int fid;
    void *pp;
    int ps;
    void *src;
    void *dest;
    int size;
};
#define SB_SIFCMDINTRHDLR	35
#define SB_SIFADDCMDHANDLER	36
struct sb_sifaddcmdhandler_arg {
    u_int fid;
    void *func;
    void *data;
};
#define SB_SIFREMOVECMDHANDLER	37
struct sb_sifremovecmdhandler_arg {
    u_int fid;
};
#define SB_SIFSETCMDBUFFER	38
struct sb_sifsetcmdbuffer_arg {
    void *db;
    int size;
};

/*
 *  SIF RPC services
 */

#define SB_SIFINITRPC		48
#define SB_SIFEXITRPC		49
#define SB_SIFGETOTHERDATA	50
struct sb_sifgetotherdata_arg {
    void *rd;
    void *src;
    void *dest;
    int size;
    u_int mode;
    void *func;
    void *para;
};
#define SB_SIFBINDRPC		51
struct sb_sifbindrpc_arg {
    void *bd;
    u_int command;
    u_int mode;
    void *func;
    void *para;
};
#define SB_SIFCALLRPC		52
struct sb_sifcallrpc_arg {
    void *bd;
    u_int fno;
    u_int mode;
    void *send;
    int ssize;
    void *receive;
    int rsize;
    void *func;
    void *para;
};
#define SB_SIFCHECKSTATRPC	53
struct sb_sifcheckstatrpc_arg {
    void *cd;
};
#define SB_SIFSETRPCQUEUE	54
struct sb_sifsetrpcqueue_arg {
    void *pSrqd;
    void *callback;
    void *arg;
};
#define SB_SIFREGISTERRPC	55
struct sb_sifregisterrpc_arg {
    void *pr;
    u_int command;
    void *func;
    void *buff;
    void *cfunc;
    void *cbuff;
    void *pq;
};
#define SB_SIFREMOVERPC		56
struct sb_sifremoverpc_arg {
    void *pr;
    void *pq;
};
#define SB_SIFREMOVERPCQUEUE	57
struct sb_sifremoverpcqueue_arg {
    void *pSrqd;
};
#define SB_SIFGETNEXTREQUEST	58
struct sb_sifgetnextrequest_arg {
    void *qd;
};
#define SB_SIFEXECREQUEST	59
struct sb_sifexecrequest_arg {
    void *rdp;
};

/*
 *  device services
 */

/* RPC common argument */

struct sbr_common_arg {
    int result;
    void *arg;
    void (*func)(void *, int);
    void *para;
};

/* IOP heap */

#define SBR_IOPH_INIT		64
#define SBR_IOPH_ALLOC		65
struct sbr_ioph_alloc_arg {
    int size;
};
#define SBR_IOPH_FREE		66
struct sbr_ioph_free_arg {
    void *addr;
};

/* pad device */

#define SBR_PAD_INIT		80
struct sbr_pad_init_arg {
    int mode;
};
#define SBR_PAD_END		81
#define SBR_PAD_PORTOPEN	82
struct sbr_pad_portopen_arg {
    int port;
    int slot;
    void *addr;
};
#define SBR_PAD_PORTCLOSE	83
struct sbr_pad_portclose_arg {
    int port;
    int slot;
};
#define SBR_PAD_SETMAINMODE	84
struct sbr_pad_setmainmode_arg {
    int port;
    int slot;
    int offs;
    int lock;
};
#define SBR_PAD_SETACTDIRECT	85
struct sbr_pad_setactdirect_arg {
    int port;
    int slot;
    const unsigned char *data;
};
#define SBR_PAD_SETACTALIGN	86
struct sbr_pad_setactalign_arg {
    int port;
    int slot;
    const unsigned char *data;
};
#define SBR_PAD_INFOPRESSMODE	87
struct sbr_pad_pressmode_arg {
    int port;
    int slot;
};
#define SBR_PAD_ENTERPRESSMODE	88
#define SBR_PAD_EXITPRESSMODE	89


#define SB_PAD_READ		90
struct sb_pad_read_arg {
    int port;
    int slot;
    unsigned char *rdata;
};
#define SB_PAD_GETSTATE		91
struct sb_pad_getstate_arg {
    int port;
    int slot;
};
#define SB_PAD_GETREQSTATE	92
struct sb_pad_getreqstate_arg {
    int port;
    int slot;
};
#define SB_PAD_INFOACT		93
struct sb_pad_infoact_arg {
    int port;
    int slot;
    int actno;
    int term;
};
#define SB_PAD_INFOCOMB		94
struct sb_pad_infocomb_arg {
    int port;
    int slot;
    int listno;
    int offs;
};
#define SB_PAD_INFOMODE		95
struct sb_pad_infomode_arg {
    int port;
    int slot;
    int term;
    int offs;
};

/* sound */

#define SBR_SOUND_INIT		112
struct sbr_sound_init_arg {
#define SB_SOUND_INIT_COLD	0
#define SB_SOUND_INIT_HOT	1
    int flag;
};
#define SBR_SOUND_END		113
#define SB_SOUND_GREG		114
#define SB_SOUND_SREG		115
struct sb_sound_reg_arg {
    u_int idx;
#define SB_SOUND_REG_MADR(core)		(0 + (core))
#define SB_SOUND_REG_BCR(core)		(2 + (core))
#define SB_SOUND_REG_BTCR(core)		(4 + (core))
#define SB_SOUND_REG_CHCR(core)		(6 + (core))

#define SB_SOUND_REG_MMIX(core)		(8 + (core))
#define SB_SOUND_REG_DMAMOD(core)	(10 + (core))
#define SB_SOUND_REG_MVOLL(core)	(12 + (core))
#define SB_SOUND_REG_MVOLR(core)	(14 + (core))
#define SB_SOUND_REG_EVOLL(core)	(16 + (core))
#define SB_SOUND_REG_EVOLR(core)	(18 + (core))
#define SB_SOUND_REG_AVOLL(core)	(20 + (core))
#define SB_SOUND_REG_AVOLR(core)	(22 + (core))
#define SB_SOUND_REG_BVOLL(core)	(24 + (core))
#define SB_SOUND_REG_BVOLR(core)	(26 + (core))
    u_int data;
};
#define SBR_SOUND_GCOREATTR	116
#define SBR_SOUND_SCOREATTR	117
struct sbr_sound_coreattr_arg {
    u_int idx;
#define SB_SOUND_CA_EFFECT_ENABLE	(1<<1)
#define SB_SOUND_CA_IRQ_ENABLE		(2<<1)
#define SB_SOUND_CA_MUTE_ENABLE		(3<<1)
#define SB_SOUND_CA_NOISE_CLK		(4<<1)
#define SB_SOUND_CA_SPDIF_MODE		(5<<1)
    u_int data;
};
#define SBR_SOUND_TRANS		118
struct sbr_sound_trans_arg {
    int channel;
    u_int mode;
#define SB_SOUND_TRANS_MODE_WRITE	0
#define SB_SOUND_TRANS_MODE_READ	1
#define SB_SOUND_TRANS_MODE_STOP	2
#define SB_SOUND_TRANS_MODE_DMA		(0<<3)
#define SB_SOUND_TRANS_MODE_PIO		(1<<3)
#define SB_SOUND_TRANS_MODE_ONCE	(0<<4)
#define SB_SOUND_TRANS_MODE_LOOP	(1<<4)
    u_int addr;
    u_int size;
    u_int start_addr;
};
#define SBR_SOUND_TRANSSTAT	119
struct sbr_sound_trans_stat_arg {
    int channel;
#define SB_SOUND_TRANSSTAT_WAIT		1
#define SB_SOUND_TRANSSTAT_CHECK	0
    int flag;
};
#define SBR_SOUND_TRANSCALLBACK	120
struct sbr_sound_trans_callback_arg {
    int channel;
    int (*func)(void*, int);
    void *data;
    int (*oldfunc)(void*, int);
    void *olddata;
};
#define SBR_SOUND_VOICE_TRANS	121
#define SBR_SOUND_VOICE_TRANSSTAT	122

/* memory card */

#define SBR_MC_INIT		144
#define SBR_MC_OPEN		145
struct sbr_mc_open_arg {
    int port;
    int slot;
    const char *name;
    int mode;
};
#define SBR_MC_MKDIR		146
struct sbr_mc_mkdir_arg {
    int port;
    int slot;
    const char *name;
};
#define SBR_MC_CLOSE		147
struct sbr_mc_close_arg {
    int fd;
};
#define SBR_MC_SEEK		148
struct sbr_mc_seek_arg {
    int fd;
    int offset;
    int mode;
};
#define SBR_MC_READ		149
struct sbr_mc_read_arg {
    int fd;
    void *buff;
    int size;
};
#define SBR_MC_WRITE		150
struct sbr_mc_write_arg {
    int fd;
    void *buff;
    int size;
};
#define SBR_MC_GETINFO		151
struct sbr_mc_getinfo_arg {
    int port;
    int slot;
    int *type;
    int *free;
    int *format;
};
#define SBR_MC_GETDIR		152
struct sbr_mc_getdir_arg {
    int port;
    int slot;
    const char *name;
    unsigned int mode;
    int maxent;
    void *table;
};
#define SBR_MC_FORMAT		153
struct sbr_mc_format_arg {
    int port;
    int slot;
};
#define SBR_MC_DELETE		154
struct sbr_mc_delete_arg {
    int port;
    int slot;
    const char *name;
};
#define SBR_MC_FLUSH		155
struct sbr_mc_flush_arg {
    int fd;
};
#define SBR_MC_SETFILEINFO	156
struct sbr_mc_setfileinfo_arg {
    int port;
    int slot;
    const char *name;
    const char *info;
    unsigned int valid;
};
#define SBR_MC_RENAME		157
struct sbr_mc_rename_arg {
    int port;
    int slot;
    const char *org;
    const char *new;
};
#define SBR_MC_UNFORMAT		158
struct sbr_mc_unformat_arg {
    int port;
    int slot;
};
#define SBR_MC_GETENTSPACE	159
struct sbr_mc_getentspace_arg {
    int port;
    int slot;
    const char *path;
};

/*
 * CD/DVD
 */
#define SBR_CDVD_INIT		176
#define SBR_CDVD_RESET		177
#define SBR_CDVD_READY		178
struct sbr_cdvd_ready_arg {
    int mode;
};

#define SBR_CDVD_READ		179
struct sbr_cdvd_read_arg {
    u_int lbn;
    u_int sectors;
    void *buf;
    struct sceCdRMode *rmode;
};

#define SBR_CDVD_STOP		180
#define SBR_CDVD_GETTOC		181
struct sbr_cdvd_gettoc_arg {
    u_char *buf;
    int len;
    int media;
};

#define SBR_CDVD_READRTC	182
struct sbr_cdvd_rtc_arg {
	u_char stat;		/* status */
	u_char second;		/* second */
	u_char minute;		/* minute */
	u_char hour;		/* hour   */

	u_char pad;		/* pad    */
	u_char day;		/* day    */
	u_char month;		/* 1900 or 2000 and  month  */
	u_char year;		/* year   */
};
#define SBR_CDVD_MMODE		184
struct sbr_cdvd_mmode_arg {
    int media;
};

#define SBR_CDVD_GETERROR	185
#define SBR_CDVD_GETTYPE	186
#define SBR_CDVD_TRAYREQ	187
struct sbr_cdvd_trayreq_arg {
    int req;
    int traycount;
};

#define SB_CDVD_POWERHOOK	188
struct sb_cdvd_powerhook_arg {
  void (*func)(void *);
  void *arg;
};

/* Uses sbr_cdvd_read_arg */
#define SBR_CDVD_READ_DVD		196

#endif /* __ASM_PS2_SBCALL_H */
