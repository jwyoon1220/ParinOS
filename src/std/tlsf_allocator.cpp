/*
 * src/std/tlsf_allocator.cpp — TlsfAllocator C++ implementation
 *
 * Two-Level Segregated Fit (TLSF) allocator.
 * malloc / free / realloc run in worst-case O(1) time.
 *
 * Reference:
 *   "TLSF: A New Dynamic Memory Allocator for Real-Time Systems"
 *   M. Masmano, I. Ripoll, A. Crespo, J. Real — ECRTS 2004
 */

#include "tlsf.hpp"

// C headers must be wrapped in extern "C" because they lack the guard themselves.
extern "C" {
#include "../mem/mem.h"     // memset, memcpy
#include "../hal/vga.h"     // kprintf
}

// ─────────────────────────────────────────────────────────────────────────────
//  Index mapping
// ─────────────────────────────────────────────────────────────────────────────

void TlsfAllocator::map_insert(size_t sz, int &fl, int &sl) const
{
    int msb = bsr32(static_cast<uint32_t>(sz));
    fl = msb - FL_INDEX_SHIFT;
    sl = static_cast<int>((sz >> (msb - SL_INDEX_BITS)) & (SL_CLASS_COUNT - 1u));
}

void TlsfAllocator::map_search(size_t sz, int &fl, int &sl) const
{
    int msb   = bsr32(static_cast<uint32_t>(sz));
    int shift = msb - SL_INDEX_BITS;
    if (shift > 0)
        sz += (size_t(1) << shift) - 1u;  // round up to next SL boundary
    map_insert(sz, fl, sl);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Free-list operations
// ─────────────────────────────────────────────────────────────────────────────

void TlsfAllocator::fl_insert(TlsfBlock *b)
{
    int fl, sl;
    map_insert(b->size(), fl, sl);

    b->next_free = ctl_.free_lists[fl][sl];
    b->prev_free = &ctl_.null_blk;
    ctl_.free_lists[fl][sl]->prev_free = b;
    ctl_.free_lists[fl][sl] = b;

    ctl_.fl_bitmap    |= (1u << fl);
    ctl_.sl_bitmap[fl] |= (1u << sl);
}

void TlsfAllocator::fl_remove(TlsfBlock *b)
{
    int fl, sl;
    map_insert(b->size(), fl, sl);

    b->prev_free->next_free = b->next_free;
    b->next_free->prev_free = b->prev_free;

    if (ctl_.free_lists[fl][sl] == b) {
        ctl_.free_lists[fl][sl] = b->next_free;
        if (b->next_free == &ctl_.null_blk) {
            ctl_.sl_bitmap[fl] &= ~(1u << sl);
            if (!ctl_.sl_bitmap[fl])
                ctl_.fl_bitmap &= ~(1u << fl);
        }
    }
}

TlsfBlock *TlsfAllocator::find_free_block(int fl, int sl)
{
    // Search within the same FL for a higher-or-equal SL class.
    uint32_t sl_map = ctl_.sl_bitmap[fl] & (~0u << sl);
    if (!sl_map) {
        // No block at this FL; look at higher FL classes.
        uint32_t fl_map = ctl_.fl_bitmap & (~0u << (fl + 1));
        if (!fl_map) return nullptr;
        fl = bsf32(fl_map);
        sl_map = ctl_.sl_bitmap[fl];
    }
    sl = bsf32(sl_map);
    return ctl_.free_lists[fl][sl];  // != null_blk because sl_map != 0
}

// ─────────────────────────────────────────────────────────────────────────────
//  Block helpers
// ─────────────────────────────────────────────────────────────────────────────

void TlsfAllocator::set_next_prev_free(TlsfBlock *b, bool is_free)
{
    TlsfBlock *nxt = b->next_phys();
    if (is_free) nxt->size_flags |=  TlsfBlock::F_PREV_FREE;
    else         nxt->size_flags &= ~TlsfBlock::F_PREV_FREE;
}

/**
 * Split block `b` so it holds exactly `size` bytes of payload.
 * Returns the remainder block (not yet in any list), or nullptr
 * if the remainder would be smaller than TLSF_BLOCK_MIN.
 */
TlsfBlock *TlsfAllocator::blk_split(TlsfBlock *b, size_t size)
{
    size_t rem_sz = b->size() - size - TlsfBlock::OVERHEAD;
    if (rem_sz < TLSF_BLOCK_MIN) return nullptr;

    auto *tail = reinterpret_cast<TlsfBlock*>(
        reinterpret_cast<uint8_t*>(b->payload()) + size);

    // PREV_FREE = 0 because `b` will be marked used right after the split.
    tail->size_flags = rem_sz | TlsfBlock::F_FREE;
    tail->prev_phys  = b;
    tail->next_phys()->prev_phys = tail;

    b->set_size(size);
    return tail;
}

/** Coalesce `b` with its physical next neighbour if it is free. */
TlsfBlock *TlsfAllocator::merge_next(TlsfBlock *b)
{
    TlsfBlock *nxt = b->next_phys();
    if (nxt->is_free() && !nxt->is_last()) {
        fl_remove(nxt);
        b->set_size(b->size() + TlsfBlock::OVERHEAD + nxt->size());
        b->next_phys()->prev_phys = b;
    }
    return b;
}

/** Coalesce `b` with its physical previous neighbour if PREV_FREE is set. */
TlsfBlock *TlsfAllocator::merge_prev(TlsfBlock *b)
{
    if (b->size_flags & TlsfBlock::F_PREV_FREE) {
        TlsfBlock *prv = b->prev_phys;
        fl_remove(prv);
        prv->set_size(prv->size() + TlsfBlock::OVERHEAD + b->size());
        prv->next_phys()->prev_phys = prv;
        b = prv;
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void TlsfAllocator::add_pool(void *mem, size_t bytes)
{
    if (!mem || bytes < TlsfBlock::OVERHEAD * 3u + TLSF_BLOCK_MIN)
        return;

    /*
     * Pool layout:
     *   [sentinel_before (OVERHEAD B)] [main_block header + payload]
     *   [sentinel_after  (OVERHEAD B)]
     *
     *   payload = bytes - 3 * OVERHEAD
     */
    auto *sb = reinterpret_cast<TlsfBlock*>(mem);
    sb->prev_phys  = nullptr;
    sb->size_flags = 0;    // size=0, not free, PREV_FREE=0

    size_t main_sz = bytes - TlsfBlock::OVERHEAD * 3u;
    TlsfBlock *mb  = sb->next_phys();
    mb->prev_phys  = sb;
    mb->size_flags = main_sz | TlsfBlock::F_FREE;  // PREV_FREE=0: sb not free

    TlsfBlock *se  = mb->next_phys();
    se->prev_phys  = mb;
    se->size_flags = TlsfBlock::F_PREV_FREE;  // size=0 → is_last() == true

    fl_insert(mb);
}

void TlsfAllocator::init(void *pool, size_t bytes)
{
    memset(&ctl_, 0, sizeof(ctl_));

    // The null-block sentinel replaces nullptr in all free-list links.
    // Initialise it to point to itself so insert/remove need no null checks.
    ctl_.null_blk.prev_free  = &ctl_.null_blk;
    ctl_.null_blk.next_free  = &ctl_.null_blk;
    ctl_.null_blk.size_flags = 0;
    ctl_.null_blk.prev_phys  = nullptr;

    // Point every free-list head at the null sentinel.
    for (int f = 0; f < FL_INDEX_COUNT; ++f)
        for (int s = 0; s < SL_CLASS_COUNT; ++s)
            ctl_.free_lists[f][s] = &ctl_.null_blk;

    add_pool(pool, bytes);
}

void *TlsfAllocator::malloc(size_t size)
{
    if (size == 0) return nullptr;

    // Align up; enforce minimum payload size.
    size = (size + TLSF_ALIGN_MASK) & ~TLSF_ALIGN_MASK;
    if (size < TLSF_BLOCK_MIN) size = TLSF_BLOCK_MIN;

    // Bounds check against maximum FL class.
    if (size > (size_t(1) << FL_INDEX_MAX)) return nullptr;

    int fl, sl;
    map_search(size, fl, sl);
    if (fl >= FL_INDEX_COUNT) return nullptr;

    TlsfBlock *b = find_free_block(fl, sl);
    if (!b || b == &ctl_.null_blk) return nullptr;

    fl_remove(b);

    // Split the block if the remainder can form a valid free block.
    TlsfBlock *tail = blk_split(b, size);
    if (tail) fl_insert(tail);

    // Mark as used and clear successor's PREV_FREE bit.
    b->size_flags &= ~TlsfBlock::F_FREE;
    set_next_prev_free(b, false);

    return b->payload();
}

void TlsfAllocator::free(void *ptr)
{
    if (!ptr) return;

    TlsfBlock *b = TlsfBlock::from_payload(ptr);

    b->size_flags |= TlsfBlock::F_FREE;
    set_next_prev_free(b, true);

    b = merge_next(b);
    b = merge_prev(b);

    fl_insert(b);
}

void *TlsfAllocator::realloc(void *ptr, size_t new_size)
{
    if (!ptr)      return malloc(new_size);
    if (!new_size) { free(ptr); return nullptr; }

    TlsfBlock *b = TlsfBlock::from_payload(ptr);

    // Align new_size the same way malloc() would.
    new_size = (new_size + TLSF_ALIGN_MASK) & ~TLSF_ALIGN_MASK;
    if (new_size < TLSF_BLOCK_MIN) new_size = TLSF_BLOCK_MIN;

    if (b->size() >= new_size) return ptr;  // already large enough

    void *np = malloc(new_size);
    if (np) {
        memcpy(np, ptr, b->size());
        free(ptr);
    }
    return np;
}

void TlsfAllocator::dump_stats() const
{
    size_t   total_free  = 0;
    unsigned free_blocks = 0;

    for (int f = 0; f < FL_INDEX_COUNT; ++f) {
        for (int s = 0; s < SL_CLASS_COUNT; ++s) {
            const TlsfBlock *b = ctl_.free_lists[f][s];
            while (b != &ctl_.null_blk) {
                total_free += b->size();
                ++free_blocks;
                b = b->next_free;
            }
        }
    }

    kprintf("--- TLSF Heap Stats ---\n");
    kprintf("Free: %u KB (%u free blocks)\n",
            static_cast<unsigned>(total_free / 1024u), free_blocks);
    kprintf("-----------------------\n");
}
