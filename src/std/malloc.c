/*
 * src/std/malloc.c — Kernel heap allocator
 *
 * Wraps the TLSF (Two-Level Segregated Fit) allocator to provide O(1)
 * kmalloc / kfree while keeping the existing public API unchanged.
 *
 * Growth strategy:
 *   When TLSF has no free block large enough, additional physical pages are
 *   allocated via PMM, mapped contiguously at the current heap top via VMM,
 *   and registered as a new TLSF pool with tlsf_add_pool().
 */

#include "../std/malloc.h"
#include "../std/tlsf.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/mem.h"
#include "../hal/vga.h"

/* Number of pages to add each time the heap needs to grow */
#define HEAP_GROW_PAGES  4u

static uint32_t g_heap_start = 0;
static uint32_t g_heap_top   = 0;   /* next unmapped virtual address */

/* ─────────────────────────────────────────────────────────────────────────
 *  Internal: map `pages` new physical pages at g_heap_top and hand them
 *  to TLSF as a new pool.  Returns the number of pages successfully added.
 * ───────────────────────────────────────────────────────────────────────── */
static uint32_t heap_grow(uint32_t pages)
{
    uint32_t mapped = 0;
    for (uint32_t i = 0; i < pages; i++) {
        void *pframe = pmm_alloc_page();
        if (!pframe) {
            kprintf("[Heap] Out of physical memory (grew %u pages)\n", mapped);
            break;
        }
        if (vmm_map_page(g_heap_top, (uint32_t)pframe, PAGE_RW) != VMM_SUCCESS) {
            pmm_free_frame(pframe);
            kprintf("[Heap] VMM mapping failed at 0x%x\n", g_heap_top);
            break;
        }
        g_heap_top += PAGE_SIZE;
        mapped++;
    }

    if (mapped > 0) {
        uint32_t pool_start = g_heap_top - mapped * PAGE_SIZE;
        tlsf_add_pool((void *)pool_start, (size_t)mapped * PAGE_SIZE);
    }
    return mapped;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Public API
 * ───────────────────────────────────────────────────────────────────────── */

void init_heap(uint32_t start_vaddr, uint32_t initial_pages)
{
    g_heap_start = start_vaddr;
    g_heap_top   = start_vaddr + initial_pages * PAGE_SIZE;

    tlsf_init((void *)start_vaddr, (size_t)initial_pages * PAGE_SIZE);

    kprintf("[Heap] TLSF heap initialised at 0x%x (%u KB, O(1) alloc)\n",
            start_vaddr, (unsigned)(initial_pages * PAGE_SIZE / 1024u));
}

void *kmalloc(size_t size)
{
    if (size == 0) return NULL;

    void *ptr = tlsf_malloc(size);
    if (ptr) return ptr;

    /* TLSF could not satisfy the request — grow the heap and retry once */
    /* Calculate pages needed: enough to cover size + TLSF overhead, rounded up */
    uint32_t bytes_needed = (uint32_t)(size + 64u);   /* +64 for TLSF metadata */
    uint32_t pages = (bytes_needed + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (pages < HEAP_GROW_PAGES) pages = HEAP_GROW_PAGES;

    if (heap_grow(pages) == 0)
        return NULL;   /* truly out of physical memory */

    return tlsf_malloc(size);
}

void kfree(void *ptr)
{
    tlsf_free(ptr);
}

void *kcalloc(size_t num, size_t size)
{
    /* Overflow-safe multiply */
    if (size != 0 && num > (size_t)-1 / size) return NULL;
    size_t total = num * size;
    void  *ptr   = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *krealloc(void *ptr, size_t size)
{
    if (size == 0) { kfree(ptr); return NULL; }
    if (!ptr)      return kmalloc(size);

    void *new_ptr = tlsf_realloc(ptr, size);
    if (new_ptr) return new_ptr;

    /* tlsf_realloc returns NULL only when a larger block cannot be found;
     * grow the heap and retry.                                              */
    if (heap_grow(HEAP_GROW_PAGES) == 0)
        return NULL;

    return tlsf_realloc(ptr, size);
}

void dump_heap_stat(void)
{
    tlsf_dump_stats();
}

void dump_heap(void)
{
    tlsf_dump_stats();
}