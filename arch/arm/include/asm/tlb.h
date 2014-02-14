/*
 *  arch/arm/include/asm/tlb.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Experimentation shows that on a StrongARM, it appears to be faster
 *  to use the "invalidate whole tlb" rather than "invalidate single
 *  tlb" for this.
 *
 *  This appears true for both the process fork+exit case, as well as
 *  the munmap-large-area case.
 */
#ifndef __ASMARM_TLB_H
#define __ASMARM_TLB_H

#include <asm/cacheflush.h>

#ifndef CONFIG_MMU

#include <linux/pagemap.h>

#define tlb_flush(tlb)	((void) tlb)

#include <asm-generic/tlb.h>

#else 

#include <linux/swap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

#if defined(CONFIG_SMP) || defined(CONFIG_CPU_32v7)
#define tlb_fast_mode(tlb)	0
#else
#define tlb_fast_mode(tlb)	1
#endif

#define MMU_GATHER_BUNDLE	8

struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		fullmm;
	struct vm_area_struct	*vma;
	unsigned long		range_start;
	unsigned long		range_end;
	unsigned int		nr;
	unsigned int		max;
	struct page		**pages;
	struct page		*local[MMU_GATHER_BUNDLE];
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (tlb->fullmm || !tlb->vma)
		flush_tlb_mm(tlb->mm);
	else if (tlb->range_end > 0) {
		flush_tlb_range(tlb->vma, tlb->range_start, tlb->range_end);
		tlb->range_start = TASK_SIZE;
		tlb->range_end = 0;
	}
}

static inline void tlb_add_flush(struct mmu_gather *tlb, unsigned long addr)
{
	if (!tlb->fullmm) {
		if (addr < tlb->range_start)
			tlb->range_start = addr;
		if (addr + PAGE_SIZE > tlb->range_end)
			tlb->range_end = addr + PAGE_SIZE;
	}
}

static inline void __tlb_alloc_page(struct mmu_gather *tlb)
{
	unsigned long addr = __get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);

	if (addr) {
		tlb->pages = (void *)addr;
		tlb->max = PAGE_SIZE / sizeof(struct page *);
	}
}

static inline void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush(tlb);
	if (!tlb_fast_mode(tlb)) {
		free_pages_and_swap_cache(tlb->pages, tlb->nr);
		tlb->nr = 0;
		if (tlb->pages == tlb->local)
			__tlb_alloc_page(tlb);
	}
}

static inline void
tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm, unsigned int fullmm)
{
	tlb->mm = mm;
	tlb->fullmm = fullmm;
	tlb->vma = NULL;
	tlb->max = ARRAY_SIZE(tlb->local);
	tlb->pages = tlb->local;
	tlb->nr = 0;
	__tlb_alloc_page(tlb);
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	tlb_flush_mmu(tlb);

	
	check_pgt_cache();

	if (tlb->pages != tlb->local)
		free_pages((unsigned long)tlb->pages, 0);
}

static inline void
tlb_remove_tlb_entry(struct mmu_gather *tlb, pte_t *ptep, unsigned long addr)
{
	tlb_add_flush(tlb, addr);
}

static inline void
tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm) {
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
		tlb->vma = vma;
		tlb->range_start = TASK_SIZE;
		tlb->range_end = 0;
	}
}

static inline void
tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm)
		tlb_flush(tlb);
}

static inline int __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (tlb_fast_mode(tlb)) {
		free_page_and_swap_cache(page);
		return 1; 
	}

	tlb->pages[tlb->nr++] = page;
	VM_BUG_ON(tlb->nr > tlb->max);
	return tlb->max - tlb->nr;
}

static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (!__tlb_remove_page(tlb, page))
		tlb_flush_mmu(tlb);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
	unsigned long addr)
{
	pgtable_page_dtor(pte);

	addr &= PMD_MASK;
	tlb_add_flush(tlb, addr + SZ_1M - PAGE_SIZE);
	tlb_add_flush(tlb, addr + SZ_1M);

	tlb_remove_page(tlb, pte);
}

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmdp,
				  unsigned long addr)
{
#ifdef CONFIG_ARM_LPAE
	tlb_add_flush(tlb, addr);
	tlb_remove_page(tlb, virt_to_page(pmdp));
#endif
}

#define pte_free_tlb(tlb, ptep, addr)	__pte_free_tlb(tlb, ptep, addr)
#define pmd_free_tlb(tlb, pmdp, addr)	__pmd_free_tlb(tlb, pmdp, addr)
#define pud_free_tlb(tlb, pudp, addr)	pud_free((tlb)->mm, pudp)

#define tlb_migrate_finish(mm)		do { } while (0)

#endif 
#endif