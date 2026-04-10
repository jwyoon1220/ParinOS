/*
 * src/std/heap.cpp — Kernel heap: C API over TlsfAllocator
 *
 * Exposes kmalloc / kfree / kcalloc / krealloc / init_heap as
 * extern "C" symbols so every kernel .c file can call them without change.
 * The TlsfAllocator object handles all allocation internally in O(1) time.
 *
 * Heap growth strategy
 * ────────────────────
 * When TlsfAllocator::malloc() returns nullptr the heap is grown by
 * mapping additional physical pages via PMM + VMM and registering the
 * new region as an extra pool with TlsfAllocator::add_pool().
 */

#include "tlsf.hpp"
#include "malloc.h"

extern "C" {
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/mem.h"
#include "../hal/vga.h"
}

// Number of 4 KB pages to add each time the heap needs to grow.
static constexpr uint32_t HEAP_GROW_PAGES = 4u;

// The single global allocator instance.
static TlsfAllocator g_allocator;

static uint32_t g_heap_top = 0u;  // next unmapped virtual address

// ─────────────────────────────────────────────────────────────────────────────
//  Internal: map `pages` new physical pages at g_heap_top, add to the pool.
//  Returns the number of pages successfully mapped.
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t heap_grow(uint32_t pages)
{
    uint32_t mapped = 0u;
    for (uint32_t i = 0; i < pages; ++i) {
        void *frame = pmm_alloc_page();
        if (!frame) {
            kprintf("[Heap] Out of physical memory (grew %u pages)\n", mapped);
            break;
        }
        if (vmm_map_page(g_heap_top, reinterpret_cast<uint32_t>(frame), PAGE_RW)
                != VMM_SUCCESS) {
            pmm_free_frame(frame);
            kprintf("[Heap] VMM map failed at 0x%x\n", g_heap_top);
            break;
        }
        g_heap_top += PAGE_SIZE;
        ++mapped;
    }

    if (mapped > 0u) {
        uint32_t pool_start = g_heap_top - mapped * PAGE_SIZE;
        g_allocator.add_pool(
            reinterpret_cast<void*>(pool_start),
            static_cast<size_t>(mapped) * PAGE_SIZE);
    }
    return mapped;
}

// ─────────────────────────────────────────────────────────────────────────────
//  extern "C" public API  — matches malloc.h exactly
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

void init_heap(uint32_t start_vaddr, uint32_t initial_pages)
{
    g_heap_top = start_vaddr + initial_pages * PAGE_SIZE;
    g_allocator.init(
        reinterpret_cast<void*>(start_vaddr),
        static_cast<size_t>(initial_pages) * PAGE_SIZE);

    kprintf("[Heap] TLSF heap at 0x%x (%u KB, O(1) alloc)\n",
            start_vaddr,
            static_cast<unsigned>(initial_pages * PAGE_SIZE / 1024u));
}

void *kmalloc(size_t size)
{
    if (size == 0u) return nullptr;

    void *ptr = g_allocator.malloc(size);
    if (ptr) return ptr;

    // TLSF exhausted — grow and retry once.
    uint32_t bytes_needed = static_cast<uint32_t>(size) + 64u;
    uint32_t pages = (bytes_needed + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (pages < HEAP_GROW_PAGES) pages = HEAP_GROW_PAGES;

    if (heap_grow(pages) == 0u) return nullptr;
    return g_allocator.malloc(size);
}

void kfree(void *ptr)
{
    g_allocator.free(ptr);
}

void *kcalloc(size_t num, size_t size)
{
    // Overflow-safe multiply.
    if (size != 0u && num > static_cast<size_t>(-1) / size)
        return nullptr;
    size_t total = num * size;
    void  *ptr   = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *krealloc(void *ptr, size_t size)
{
    if (size == 0u) { kfree(ptr); return nullptr; }
    if (!ptr)       return kmalloc(size);

    void *np = g_allocator.realloc(ptr, size);
    if (np) return np;

    // Grow and retry.
    if (heap_grow(HEAP_GROW_PAGES) == 0u) return nullptr;
    return g_allocator.realloc(ptr, size);
}

void dump_heap_stat(void)
{
    g_allocator.dump_stats();
}

void dump_heap(void)
{
    g_allocator.dump_stats();
}

} // extern "C"
