/*
 * src/std/tlsf.hpp — TlsfAllocator C++ class
 *
 * Encapsulates the Two-Level Segregated Fit (TLSF) O(1) allocator as a
 * self-contained C++ object.  The kernel keeps a single global instance
 * and exposes it through the C API in malloc.cpp (kmalloc / kfree / …).
 *
 * Design notes
 * ────────────
 *  • No exceptions, no RTTI, no global constructors with side-effects
 *    (the class is trivially constructible; call init() explicitly).
 *  • All private helpers are inline – the compiler folds them into the
 *    four public fast-path methods (malloc / free / realloc / add_pool).
 *  • The complete control structure (bitmaps + free-list array) lives
 *    inside the object — no external state.
 */

#ifndef PARINOS_TLSF_HPP
#define PARINOS_TLSF_HPP

#include <stddef.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Algorithm constants  (all O(1) guarantees depend on these being fixed)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int SL_INDEX_BITS  = 4;
static constexpr int SL_CLASS_COUNT = 1 << SL_INDEX_BITS;   // 16
static constexpr int FL_INDEX_SHIFT = SL_INDEX_BITS;         //  4
static constexpr int FL_INDEX_MAX   = 24;                    // max block 16 MB
static constexpr int FL_INDEX_COUNT = FL_INDEX_MAX - FL_INDEX_SHIFT + 1; // 21

static constexpr size_t TLSF_ALIGN_SIZE  = 4u;
static constexpr size_t TLSF_ALIGN_MASK  = TLSF_ALIGN_SIZE - 1u;
static constexpr size_t TLSF_BLOCK_MIN   = 1u << FL_INDEX_SHIFT;  // 16 B

// ─────────────────────────────────────────────────────────────────────────────
//  Block header — lives immediately before the user payload
//
//  Memory layout (ascending addresses):
//    +0  prev_phys  (4 B) — physically-previous block
//    +4  size_flags (4 B) — payload size | BLK_FREE | PREV_FREE
//    +8  next_free  (4 B) — segregated-list forward link  \  only valid
//    +12 prev_free  (4 B) — segregated-list back  link   /  when free
//    +16 … user payload …
// ─────────────────────────────────────────────────────────────────────────────

struct TlsfBlock {
    TlsfBlock *prev_phys;   // lower physically-adjacent block (null = first)
    size_t     size_flags;  // payload size | BLK_FREE | PREV_FREE

    // Free-list links — overlap user payload when block is allocated.
    TlsfBlock *next_free;
    TlsfBlock *prev_free;

    // ── Flag constants ──────────────────────────────────────────────────
    static constexpr size_t F_FREE      = 1u; // this block is free
    static constexpr size_t F_PREV_FREE = 2u; // predecessor is free
    static constexpr size_t F_SIZE_MASK = ~static_cast<size_t>(3u);

    // ── Accessors ───────────────────────────────────────────────────────
    size_t size()      const { return size_flags & F_SIZE_MASK; }
    bool   is_free()   const { return size_flags &  F_FREE; }
    bool   is_last()   const { return size() == 0u; }  // sentinel

    void set_size(size_t s) {
        size_flags = (size_flags & ~F_SIZE_MASK) | (s & F_SIZE_MASK);
    }

    // Overhead = offset of next_free = sizeof(prev_phys) + sizeof(size_flags)
    //          = 4 + 4 = 8 bytes on a 32-bit build.
    static constexpr size_t OVERHEAD =
        sizeof(TlsfBlock*) + sizeof(size_t);

    // Pointer arithmetic helpers.
    void       *payload()            { return reinterpret_cast<uint8_t*>(this) + OVERHEAD; }
    const void *payload()      const { return reinterpret_cast<const uint8_t*>(this) + OVERHEAD; }

    static TlsfBlock *from_payload(void *p) {
        return reinterpret_cast<TlsfBlock*>(reinterpret_cast<uint8_t*>(p) - OVERHEAD);
    }

    TlsfBlock *next_phys() {
        return reinterpret_cast<TlsfBlock*>(
            reinterpret_cast<uint8_t*>(payload()) + size());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  TlsfAllocator
// ─────────────────────────────────────────────────────────────────────────────

class TlsfAllocator {
public:
    // -----------------------------------------------------------------
    //  Life-cycle
    // -----------------------------------------------------------------

    TlsfAllocator() = default;

    /**
     * Initialise the allocator with an initial memory pool.
     * Must be called once before any malloc / free.
     */
    void init(void *pool, size_t bytes);

    /**
     * Register an additional memory pool (e.g. after heap growth).
     * May be called any number of times after init().
     */
    void add_pool(void *pool, size_t bytes);

    // -----------------------------------------------------------------
    //  Core allocator — all O(1) worst-case
    // -----------------------------------------------------------------

    /** Allocate `size` bytes.  Returns nullptr on failure. */
    void *malloc(size_t size);

    /** Free a pointer returned by malloc().  No-op on nullptr. */
    void  free(void *ptr);

    /** Resize an allocation.  Behaves like standard realloc(). */
    void *realloc(void *ptr, size_t new_size);

    // -----------------------------------------------------------------
    //  Diagnostics
    // -----------------------------------------------------------------

    /** Print heap statistics via kprintf(). */
    void dump_stats() const;

private:
    // ── Control structure ────────────────────────────────────────────
    struct Ctl {
        uint32_t   fl_bitmap;
        uint32_t   sl_bitmap[FL_INDEX_COUNT];
        TlsfBlock *free_lists[FL_INDEX_COUNT][SL_CLASS_COUNT];
        TlsfBlock  null_blk;   // sentinel — all empty list heads point here
    } ctl_{};

    // ── Bit-scan helpers (GCC built-ins, O(1)) ───────────────────────
    static int bsr32(uint32_t x) { return 31 - __builtin_clz(x); }
    static int bsf32(uint32_t x) { return __builtin_ctz(x); }

    // ── Index mapping ────────────────────────────────────────────────

    /** Floor mapping — used when inserting a free block. */
    void map_insert(size_t sz, int &fl, int &sl) const;

    /**
     * Ceiling mapping — used when searching for a free block.
     * Rounds up so the found block is always large enough.
     */
    void map_search(size_t sz, int &fl, int &sl) const;

    // ── Free-list operations ─────────────────────────────────────────
    void       fl_insert(TlsfBlock *b);
    void       fl_remove(TlsfBlock *b);
    TlsfBlock *find_free_block(int fl, int sl);

    // ── Block helpers ────────────────────────────────────────────────
    void       set_next_prev_free(TlsfBlock *b, bool is_free);
    TlsfBlock *blk_split(TlsfBlock *b, size_t size);
    TlsfBlock *merge_next(TlsfBlock *b);
    TlsfBlock *merge_prev(TlsfBlock *b);

    // Non-copyable, non-movable.
    TlsfAllocator(const TlsfAllocator &)            = delete;
    TlsfAllocator &operator=(const TlsfAllocator &) = delete;
};

#endif // PARINOS_TLSF_HPP
