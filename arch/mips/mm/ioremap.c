/*
 * arch/mips/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed because R5900's scratchpad RAM cannot be accessed
 * through unmapped area (KSEG0, 1).
 *
 *	Copyright (C) 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * This file is based on arch/i386/mm/ioremap.c:
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

#include <linux/vmalloc.h>
#include <asm/io.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (!pte_none(*pte))
			printk("remap_area_pte: page already exists\n");
		set_pte(pte, mk_pte_phys(phys_addr, __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | 
					_PAGE_DIRTY | _PAGE_ACCESSED | flags)));
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	do {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int remap_area_pages(unsigned long address, unsigned long phys_addr,
				 unsigned long size, unsigned long flags)
{
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	while (address < end) {
		pmd_t *pmd = pmd_alloc_kernel(dir, address);
		if (!pmd)
			return -ENOMEM;
		if (remap_area_pmd(pmd, address, end - address,
					 phys_addr + address, flags))
			return -ENOMEM;
		set_pgdir(address, *dir);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_all();
	return 0;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void * addr;
	struct vm_struct * area;
	unsigned long offset;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(size + offset);

	/*
	 * Don't allow mappings that wrap..
	 */
	if (!size || size > phys_addr + size)
		return NULL;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages(VMALLOC_VMADDR(addr), phys_addr, size, flags)) {
		vfree(addr);
		return NULL;
	}
	return (void *) (offset + (char *)addr);
}

void __iounmap(void *addr)
{
	return vfree((void *) (PAGE_MASK & (unsigned long) addr));
}
