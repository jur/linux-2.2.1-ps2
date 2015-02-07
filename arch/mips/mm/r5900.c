/* $Id: r5900.c,v 1.12.6.1 2001/08/17 04:57:25 shin Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * r5900.c: R5900 processor specific MMU/Cache routines.
 *
 * Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is based on r4xx0.c:
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle ralf@gnu.org
 *
 */
#include <linux/autoconf.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bcache.h>
#include <asm/io.h>
#include <asm/sgi.h>
#include <asm/sgimc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance. */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "sync.p\n\t" \
				     ".set reorder\n\t")

/* Primary cache parameters. */
static int icache_size, dcache_size; /* Size in bytes */
static int ic_lsize, dc_lsize;       /* LineSize in bytes */

#include <asm/r5900_cacheops.h>
#include <asm/r5900_cache.h>

#undef DEBUG_CACHE

/*
 * Dummy cache handling routines for machines without boardcaches
 */
static void no_sc_noop(void) {}

static struct bcache_ops no_sc_ops = {
	(void *)no_sc_noop, (void *)no_sc_noop,
	(void *)no_sc_noop, (void *)no_sc_noop
};

static struct bcache_ops *bcops = &no_sc_ops;

/*
 * Zero an entire page.  Basically a simple unrolled loop should do the
 * job but we want more performance by saving memory bus bandwidth.
 * have five flavours of the routine available for:
 */

static void r5900_clear_page_d16(unsigned long page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tsq\t$0,(%0)\n\t"
		"sq\t$0,16(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"sq\t$0,-32(%0)\n\t"
		"sq\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"nop\n\t"		/* inhibit short loop */
		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE));
}

/*
 * This is still inefficient.  We only can do better if we know the
 * virtual address where the copy will be accessed.
 */

#ifdef CONFIG_CONTEXT_R5900
static void r5900_copy_page_d16(unsigned long to, unsigned long from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"daddiu\t$1,%0,%8\n"
		"1:\tlq\t%2,(%1)\n\t"
		"lq\t%3,16(%1)\n\t"
		"sq\t%2,(%0)\n\t"
		"sq\t%3,16(%0)\n\t"

		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"

		"lq\t%2,-32(%1)\n\t"
		"lq\t%3,-16(%1)\n\t"
		"sq\t%2,-32(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sq\t%3,-16(%0)\n\t"

		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE));
}
#else
static void r5900_copy_page_d16(unsigned long to, unsigned long from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"daddiu\t$1,%0,%8\n"
		"1:\tlw\t%2,(%1)\n\t"
		"lw\t%3,4(%1)\n\t"
		"lw\t%4,8(%1)\n\t"
		"lw\t%5,12(%1)\n\t"
		"sw\t%2,(%0)\n\t"
		"sw\t%3,4(%0)\n\t"
		"sw\t%4,8(%0)\n\t"
		"sw\t%5,12(%0)\n\t"
		"lw\t%2,16(%1)\n\t"
		"lw\t%3,20(%1)\n\t"
		"lw\t%4,24(%1)\n\t"
		"lw\t%5,28(%1)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%3,20(%0)\n\t"
		"sw\t%4,24(%0)\n\t"
		"sw\t%5,28(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"lw\t%2,-32(%1)\n\t"
		"lw\t%3,-28(%1)\n\t"
		"lw\t%4,-24(%1)\n\t"
		"lw\t%5,-20(%1)\n\t"
		"sw\t%2,-32(%0)\n\t"
		"sw\t%3,-28(%0)\n\t"
		"sw\t%4,-24(%0)\n\t"
		"sw\t%5,-20(%0)\n\t"
		"lw\t%2,-16(%1)\n\t"
		"lw\t%3,-12(%1)\n\t"
		"lw\t%4,-8(%1)\n\t"
		"lw\t%5,-4(%1)\n\t"
		"sw\t%2,-16(%0)\n\t"
		"sw\t%3,-12(%0)\n\t"
		"sw\t%4,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t%5,-4(%0)\n\t"
		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE));
}
#endif

static inline void r5900_flush_cache_all_d64i64(void)
{
	unsigned long flags;

	save_and_cli(flags);
	blast_dcache64(); blast_icache64();
	restore_flags(flags);
}

static void r5900_flush_cache_range_d64i64(struct mm_struct *mm,
					 unsigned long start,
					 unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		save_and_cli(flags);
		blast_dcache64(); blast_icache64();
		restore_flags(flags);
	}
}

static void r5900_flush_cache_mm_d64i64(struct mm_struct *mm)
{
	if(mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r5900_flush_cache_all_d64i64();
	}
}

static void r5900_flush_cache_page_d64i64(struct vm_area_struct *vma,
					unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int text;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if(mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	text = (vma->vm_flags & VM_EXEC);
	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if(mm == current->mm) {
		blast_dcache64_page(page);
		if(text)
			blast_icache64_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache64_page_indexed(page);
		if(text)
			blast_icache64_page_indexed(page);
	}
out:
	restore_flags(flags);
}

static void r5900_flush_page_to_ram_d64i64(unsigned long page)
{
	page &= PAGE_MASK;
	if((page >= KSEG0 && page < KSEG1) || (page >= KSEG2)) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("cram[%08lx]", page);
#endif
		save_and_cli(flags);
		blast_dcache64_page(page);
		restore_flags(flags);
	}
}

/*
 * Writeback and invalidate the primary cache dcache before DMA.
 */
static void
r5900_dma_cache_wback_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
	}
	bcops->bc_wback_inv(addr, size);
}

static void
r5900_dma_cache_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
	}

	bcops->bc_inv(addr, size);
}

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void r5900_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long daddr, iaddr;

	daddr = addr & ~(dc_lsize - 1);
	protected_writeback_dcache_line(daddr);
	protected_writeback_dcache_line(daddr + dc_lsize);
	iaddr = addr & ~(ic_lsize - 1);
	protected_flush_icache_line(iaddr);
	protected_flush_icache_line(iaddr + ic_lsize);
}

#undef DEBUG_TLB
#undef DEBUG_TLBUPDATE

#define NTLB_ENTRIES       48  /* Fixed on all R4XX0 variants... */

#define NTLB_ENTRIES_HALF  24  /* Fixed on all R4XX0 variants... */

static inline void r5900_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = (get_entryhi() & 0xff);
	set_entryhi(KSEG0 | 0xff);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while(entry < NTLB_ENTRIES) {
		set_index(entry);
		BARRIER;
		tlb_write_indexed();
		BARRIER;
		entry++;
	}
	BARRIER;
	set_entryhi(old_ctx);
	BARRIER;
	restore_flags(flags);
}

static void r5900_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
		if(mm == current->mm)
			set_entryhi(mm->context & 0xff);
		BARRIER;
		restore_flags(flags);
	}
}

static void r5900_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xff),
		       start, end);
#endif
		save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if(size <= NTLB_ENTRIES_HALF) {
			int oldpid = (get_entryhi() & 0xff);
			int newpid = (mm->context & 0xff);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				BARRIER;
				tlb_probe();
				BARRIER;
				idx = get_index();
				set_entrylo0(0);
				set_entrylo1(0);
				set_entryhi(KSEG0 | 0xff);
				BARRIER;
				if(idx < 0)
					continue;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
			BARRIER;
		} else {
			get_new_mmu_context(mm, asid_cache);
			if(mm == current->mm)
				set_entryhi(mm->context & 0xff);
			BARRIER;
		}
		restore_flags(flags);
	}
}

static void r5900_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (vma->vm_mm->context & 0xff);
		page &= (PAGE_MASK << 1);
		save_and_cli(flags);
		oldpid = (get_entryhi() & 0xff);
		set_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		BARRIER;
		idx = get_index();
		set_entrylo0(0);
		set_entrylo1(0);
		set_entryhi(KSEG0 | 0xff);
		if(idx < 0)
			goto finish;
		BARRIER;
		tlb_write_indexed();

	finish:
		BARRIER;
		set_entryhi(oldpid);
		restore_flags(flags);
	}
}

/* Load a new root pointer into the TLB. */
static void r5900_load_pgd(unsigned long pg_dir)
{
}

static void r5900_pgd_init(unsigned long page)
{
	unsigned long *p = (unsigned long *) page;
	int i;

	for(i = 0; i < USER_PTRS_PER_PGD; i+=8) {
		p[i + 0] = (unsigned long) invalid_pte_table;
		p[i + 1] = (unsigned long) invalid_pte_table;
		p[i + 2] = (unsigned long) invalid_pte_table;
		p[i + 3] = (unsigned long) invalid_pte_table;
		p[i + 4] = (unsigned long) invalid_pte_table;
		p[i + 5] = (unsigned long) invalid_pte_table;
		p[i + 6] = (unsigned long) invalid_pte_table;
		p[i + 7] = (unsigned long) invalid_pte_table;
	}
}

#ifdef DEBUG_TLBUPDATE
static unsigned long ehi_debug[NTLB_ENTRIES];
static unsigned long el0_debug[NTLB_ENTRIES];
static unsigned long el1_debug[NTLB_ENTRIES];
#endif

static void r5900_update_mmu_cache(struct vm_area_struct * vma,
				 unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	pid = (get_entryhi() & 0xff);

#ifdef DEBUG_TLB
	if((pid != (vma->vm_mm->context & 0xff)) || (vma->vm_mm->context == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d tlbpid=%d\n",
		       (int) (vma->vm_mm->context & 0xff), pid);
	}
#endif

	save_and_cli(flags);
	address &= (PAGE_MASK << 1);
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	BARRIER;
	tlb_probe();
	BARRIER;
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	BARRIER;
	if ((signed long)pte_val(*ptep) < 0) {
	    /* scratchpad RAM mapping */
	    address &= (PAGE_MASK << 2);	/* must be 16KB aligned */
	    set_entryhi(address | (pid));
	    BARRIER;
	    tlb_probe();
	    BARRIER;
	    idx = get_index();
	    BARRIER;
	    set_entrylo0(0x80000006);	/* S, D, V */
	    set_entrylo1(0x00000006);	/*    D, V */
	    set_entryhi(address | (pid));
	} else {
	    set_entrylo0(pte_val(*ptep++) >> 6);
	    set_entrylo1(pte_val(*ptep) >> 6);
	    set_entryhi(address | (pid));
	}
	BARRIER;
	if(idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	BARRIER;
	set_entryhi(pid);
	BARRIER;
	restore_flags(flags);
}

static void r5900_show_regs(struct pt_regs * regs)
{
	/* Saved main processor registers. */
	printk("$0 : %08lx %08lx %08lx %08lx\n",
	       0UL, get_gpreg(regs, 1),
	       get_gpreg(regs, 2), get_gpreg(regs,3));
	printk("$4 : %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 4), get_gpreg(regs,5),
	       get_gpreg(regs, 6), get_gpreg(regs,7));
	printk("$8 : %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 8), get_gpreg(regs,9),
	       get_gpreg(regs, 10), get_gpreg(regs,11));
	printk("$12: %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 12), get_gpreg(regs,13),
	       get_gpreg(regs, 14), get_gpreg(regs,15));
	printk("$16: %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 16), get_gpreg(regs,17),
	       get_gpreg(regs, 18), get_gpreg(regs,19));
	printk("$20: %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 20), get_gpreg(regs,21),
	       get_gpreg(regs, 22), get_gpreg(regs,23));
	printk("$24: %08lx %08lx\n",
	       get_gpreg(regs, 24), get_gpreg(regs,25));
	printk("$28: %08lx %08lx %08lx %08lx\n",
	       get_gpreg(regs, 28), get_gpreg(regs,29),
	       get_gpreg(regs, 30), get_gpreg(regs,31));

	/* Saved cp0 registers. */
	printk("epc   : %08lx\nStatus: %08lx\nCause : %08lx\n",
	       regs->cp0_epc, regs->cp0_status, regs->cp0_cause);
}
			
static void r5900_show_tlbs(void)
{
	unsigned long flags;
	int i, h, t0, t1;

	save_and_cli(flags);
	h = get_entryhi();
	printk("TLB dump: hi=%08x\n", h);
	for (i = 0; i < NTLB_ENTRIES; i++) {
	BARRIER;
		set_index(i);
	BARRIER;
		tlb_read();
	BARRIER;
		t0 = get_entrylo0();
		t1 = get_entrylo1();
		h = get_entryhi();
	BARRIER;
		printk("%02d: %08x %08x %08x\t", i, h, t0, t1);
		i++;
	BARRIER;
		set_index(i);
	BARRIER;
		tlb_read();
	BARRIER;
		t0 = get_entrylo0();
		t1 = get_entrylo1();
		h = get_entryhi();
	BARRIER;
		printk("\t%08x %08x %08x\n", h, t0, t1);
	}
	set_entryhi(h);
	BARRIER;
	restore_flags(flags);
}

static void r5900_add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				      unsigned long entryhi, unsigned long pagemask)
{
        unsigned long flags;
        unsigned long wired;
        unsigned long old_pagemask;
        unsigned long old_ctx;

        save_and_cli(flags);
        /* Save old context and create impossible VPN2 value */
        old_ctx = (get_entryhi() & 0xff);
        old_pagemask = get_pagemask();
        wired = get_wired();
        set_wired (wired + 1);
        set_index (wired);
        BARRIER;    
        set_pagemask (pagemask);
        set_entryhi(entryhi);
        set_entrylo0(entrylo0);
        set_entrylo1(entrylo1);
        BARRIER;    
        tlb_write_indexed();
        BARRIER;    
    
        set_entryhi(old_ctx);
        BARRIER;    
        set_pagemask (old_pagemask);
        flush_tlb_all();    
        restore_flags(flags);
}

/* Detect and size the various R5900 caches. */
__initfunc(static void probe_icache(unsigned long config))
{
	icache_size = 1 << (12 + ((config >> 9) & 7));
	ic_lsize = 64;	/* fixed */

	printk("Primary instruction cache %dkb, linesize %d bytes\n",
	       icache_size >> 10, 64);
}

__initfunc(static void probe_dcache(unsigned long config))
{
	dcache_size = 1 << (12 + ((config >> 6) & 7));
	dc_lsize = 64;	/* fixed */

	printk("Primary data cache %dkb, linesize %d bytes\n",
	       dcache_size >> 10, 64);
}

__initfunc(static void setup_noscache_funcs(void))
{
	clear_page = r5900_clear_page_d16;
	copy_page = r5900_copy_page_d16;
	flush_cache_all = r5900_flush_cache_all_d64i64;
	flush_cache_mm = r5900_flush_cache_mm_d64i64;
	flush_cache_range = r5900_flush_cache_range_d64i64;
	flush_cache_page = r5900_flush_cache_page_d64i64;
	flush_page_to_ram = r5900_flush_page_to_ram_d64i64;
	dma_cache_wback_inv = r5900_dma_cache_wback_inv_pc;
	dma_cache_inv = r5900_dma_cache_inv_pc;
}

typedef int (*probe_func_t)(unsigned long);

__initfunc(static inline void setup_scache(unsigned int config))
{
	setup_noscache_funcs();
}

static int r5900_user_mode(struct pt_regs *regs)
{
	return (regs->cp0_status & ST0_KSU) == KSU_USER;
}

__initfunc(void ld_mmu_r5900(void))
{
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

	set_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NONCOHERENT);

	probe_icache(config);
	probe_dcache(config);
	setup_scache(config);

	flush_cache_sigtramp = r5900_flush_cache_sigtramp;

	flush_tlb_all = r5900_flush_tlb_all;
	flush_tlb_mm = r5900_flush_tlb_mm;
	flush_tlb_range = r5900_flush_tlb_range;
	flush_tlb_page = r5900_flush_tlb_page;
	r4xx0_asid_setup();

	load_pgd = r5900_load_pgd;
	pgd_init = r5900_pgd_init;
	update_mmu_cache = r5900_update_mmu_cache;

	show_regs = r5900_show_regs;
	show_tlbs = r5900_show_tlbs;
    
        add_wired_entry = r5900_add_wired_entry;

	user_mode = r5900_user_mode;

	flush_cache_all();
	write_32bit_cp0_register(CP0_WIRED, 0);

	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	write_32bit_cp0_register(CP0_PAGEMASK, PM_4K);
	flush_tlb_all();

	/*
	 * Display CP0 config reg. to verify the workaround 
	 * for branch prediction bug is done, or not.
	 * R5900 has a problem of branch prediction.
	 */
	{
	    u32 config;
	    config = read_32bit_cp0_register(CP0_CONFIG);
	    printk("  Branch Prediction  : %s\n",  
			(config & CONF_R5900_BPE)? "on":"off");
	    printk("  Double Issue       : %s\n",  
			(config & CONF_R5900_DIE)? "on":"off");
	}
}
