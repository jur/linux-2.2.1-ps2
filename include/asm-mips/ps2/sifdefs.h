/*
 * linux/include/asm-mips/ps2/sifdefs.h
 *
 *        Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: sifdefs.h,v 1.6 2001/04/03 06:59:04 takemura Exp $
 */

#ifndef __ASM_PS2_SIFDEFS_H
#define __ASM_PS2_SIFDEFS_H

/*
 * SIF DMA defines
 */

#define SIF_DMA_INT_I	0x2
#define SIF_DMA_INT_O	0x4

typedef struct {
	unsigned int	data;
	unsigned int	addr;
	unsigned int	size;
	unsigned int	mode;
} ps2sif_dmadata_t;

extern unsigned int ps2sif_setdma(ps2sif_dmadata_t *sdd, int len);
extern int ps2sif_dmastat(unsigned int id);
extern void ps2sif_writebackdcache(void *, int);

/*
 * SIF RPC defines
 */

typedef struct _sif_rpc_data {
	void			*paddr;	/* packet address */
	unsigned int		pid;	/* packet id */
	struct wait_queue	*wq;	/* wait queue */
	unsigned int		mode;	/* call mode */
} ps2sif_rpcdata_t;

typedef void (*ps2sif_endfunc_t)(void *);

typedef struct _sif_client_data {
	struct _sif_rpc_data	rpcd;
	unsigned int		command;
	void			*buff;
	void			*cbuff;
	ps2sif_endfunc_t	func;
	void			*para;
	struct _sif_serve_data	*serve;
} ps2sif_clientdata_t;

typedef struct _sif_receive_data {
	struct _sif_rpc_data	rpcd;
	void			*src;
	void			*dest;
	int			size;
	ps2sif_endfunc_t	func;
	void			*para;
} ps2sif_receivedata_t;

typedef void *(*ps2sif_rpcfunc_t)(unsigned int, void *, int);

typedef struct _sif_serve_data {
	unsigned int		command;
	ps2sif_rpcfunc_t	func;
	void			*buff;
	int			size;	
	ps2sif_rpcfunc_t	cfunc;
	void			*cbuff;
	int			csize;	
	ps2sif_clientdata_t *client;
	void			*paddr;
	unsigned int		fno;
	void			*receive;
	int			rsize;
	int			rmode;
	unsigned int		rid;
	struct _sif_serve_data	*link;
	struct _sif_serve_data	*next;
	struct _sif_queue_data	*base;
} ps2sif_servedata_t;

typedef struct _sif_queue_data {
	int             	active;
	struct _sif_serve_data	*link;
	struct _sif_serve_data	*start;
	struct _sif_serve_data	*end;
	struct _sif_queue_data	*next;  
	struct wait_queue	*waitq;
	void			(*callback)(void*);
	void			*callback_arg;
} ps2sif_queuedata_t; 

/* call & bind mode */
#define SIF_RPCM_NOWAIT		0x01	/* not wait for end of function */
#define SIF_RPCM_NOWBDC		0x02	/* no write back d-cache */

/* calling error */
#define SIF_RPCE_GETP	1	/* fail to get packet data */
#define SIF_RPCE_SENDP	2	/* fail to send dma packet */
#define E_SIF_PKT_ALLOC 0xd610	/* Can't allocate SIF packet. */

/* functions */

int ps2sif_bindrpc(ps2sif_clientdata_t *, unsigned int, unsigned int, ps2sif_endfunc_t, void *);
int ps2sif_callrpc(ps2sif_clientdata_t *, unsigned int, unsigned int, void *, int, void *, int, ps2sif_endfunc_t, void *);

int ps2sif_checkstatrpc(ps2sif_rpcdata_t *);

void ps2sif_setrpcqueue(ps2sif_queuedata_t *, void (*)(void*), void *);
ps2sif_servedata_t *ps2sif_getnextrequest(ps2sif_queuedata_t *);
void ps2sif_execrequest(ps2sif_servedata_t *);
void ps2sif_registerrpc(ps2sif_servedata_t *, unsigned int, ps2sif_rpcfunc_t, void *, ps2sif_rpcfunc_t, void *, ps2sif_queuedata_t *);
int ps2sif_getotherdata(ps2sif_receivedata_t *, void *, void *, int, unsigned int, ps2sif_endfunc_t, void *);
ps2sif_servedata_t *ps2sif_removerpc(ps2sif_servedata_t *, ps2sif_queuedata_t *);
ps2sif_queuedata_t *ps2sif_removerpcqueue(ps2sif_queuedata_t *);

/*
 * IOP heap defines
 */

int ps2sif_initiopheap(void);
void *ps2sif_allociopheap(int);
int ps2sif_freeiopheap(void *);
unsigned long ps2sif_virttobus(volatile void *);
void *ps2sif_bustovirt(unsigned long);

/*
 * SBIOS defines
 */

int sbios_rpc(int func, void *arg, int *result);

#endif /* __ASM_PS2_SIFDEFS_H */
