/*
 * src/std/tlsf.c — Two-Level Segregated Fit (TLSF) heap allocator
 *
 * O(1) malloc / free / realloc for the ParinOS kernel.
 *
 * Algorithm parameters (32-bit kernel):
 *   SL_INDEX_BITS  = 4  → 16 second-level sub-classes per first-level class
 *   FL_INDEX_SHIFT = 4  → first FL that uses the full SL formula; blocks < 16 B
 *                         are clamped to BLOCK_SIZE_MIN before insertion
 *   FL_INDEX_MAX   = 24 → handles single free blocks up to 2^24 = 16 MB
 *   FL_INDEX_COUNT = 21 → (FL_INDEX_MAX - FL_INDEX_SHIFT + 1)
 *   BLOCK_SIZE_MIN = 16 → smallest payload class (= 1 << FL_INDEX_SHIFT)
 *   HDR_OVERHEAD   = 8  → bytes of header per allocated block
 *                         (prev_phys pointer + size_flags word)
 *
 * Block memory layout (ascending addresses):
 *
 *   ┌───────────────────────────────┐
 *   │  prev_phys  (4B)              │  pointer to physically-previous block
 *   │  size_flags (4B)              │  payload size | BLK_FREE | PREV_FREE
 *   ├───────────────────────────────┤  ← user payload starts here  (+8 B)
 *   │  next_free  (4B)  ─┐          │  \ only present (valid) when block
 *   │  prev_free  (4B)  ─┘          │  / is free; overlaps user payload
 *   ├───────────────────────────────┤
 *   │       ...user data...         │
 *   └───────────────────────────────┘
 *
 * Two sentinel blocks per pool (size = 0) mark the physical boundaries so
 * that coalescing never walks off the end of a pool.
 */

#include "tlsf.h"
#include "../mem/mem.h"
#include "../hal/vga.h"
#include <stdint.h>
#include <stddef.h>

/* ─────────────────────────────────────────────────────────────────────────
 *  Compile-time parameters
 * ───────────────────────────────────────────────────────────────────────── */

#define SL_INDEX_BITS   4
#define SL_CLASS_COUNT  (1u << SL_INDEX_BITS)             /* 16            */
#define FL_INDEX_SHIFT  SL_INDEX_BITS                     /* 4             */
#define FL_INDEX_MAX    24
#define FL_INDEX_COUNT  (FL_INDEX_MAX - FL_INDEX_SHIFT + 1) /* 21          */

#define ALIGN_SIZE  4u
#define ALIGN_MASK  (ALIGN_SIZE - 1u)

/* Minimum payload size — equals the width of the smallest FL class.
 * All user requests are rounded up to at least this value.            */
#define BLOCK_SIZE_MIN  (1u << FL_INDEX_SHIFT)            /* 16            */

/* ─────────────────────────────────────────────────────────────────────────
 *  Block header
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct blk {
    struct blk *prev_phys;   /* physically-adjacent lower block (NULL = first)  */
    size_t      size_flags;  /* payload bytes | BLK_FREE | PREV_FREE             */
    /* The following two fields exist ONLY when the block is free.
     * When allocated they overlap with the beginning of the user payload.     */
    struct blk *next_free;
    struct blk *prev_free;
} blk_t;

/* Flags packed into the low 2 bits of size_flags */
#define BLK_FREE    1u   /* this block is free                                  */
#define PREV_FREE   2u   /* the physically-previous block is free               */
#define SIZE_MASK   (~(size_t)3u)

/* Byte distance from the block header start to the user data pointer.
 * = offsetof(blk_t, next_free) = 8 on a 32-bit build.                        */
#define HDR_OVERHEAD  ((size_t)offsetof(blk_t, next_free))

/* ── Accessors ──────────────────────────────────────────────────────────── */
#define blk_size(b)          ((b)->size_flags & SIZE_MASK)
#define blk_set_size(b, s)   ((b)->size_flags = ((b)->size_flags & ~SIZE_MASK) \
                                                | ((size_t)(s) & SIZE_MASK))
#define blk_is_free(b)       ((b)->size_flags &  BLK_FREE)
#define blk_is_last(b)       (blk_size(b) == 0u)   /* sentinel marker         */

/* Pointer to the user payload region of block b */
#define blk_payload(b)       ((void *)((uint8_t *)(b) + HDR_OVERHEAD))

/* Recover the block header from a user pointer */
#define blk_from_payload(p)  ((blk_t *)((uint8_t *)(p) - HDR_OVERHEAD))

/* Physical next block (immediately after this one in linear address space) */
#define blk_next_phys(b)     ((blk_t *)((uint8_t *)blk_payload(b) + blk_size(b)))

/* ─────────────────────────────────────────────────────────────────────────
 *  TLSF control structure — single global instance
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t fl_bitmap;                             /* one bit per FL class    */
    uint32_t sl_bitmap[FL_INDEX_COUNT];             /* one bit per SL class    */
    blk_t   *free_lists[FL_INDEX_COUNT][SL_CLASS_COUNT]; /* segregated lists   */
    blk_t    null_blk;  /* sentinel node — all empty list heads point here     */
} tlsf_ctl_t;

static tlsf_ctl_t g_ctl;

/* ─────────────────────────────────────────────────────────────────────────
 *  O(1) bit-scan helpers
 *  GCC __builtin_clz / __builtin_ctz are available in -ffreestanding mode.
 * ───────────────────────────────────────────────────────────────────────── */

/* Position of the most-significant set bit (0-based, LSB = 0). */
static inline int bsr32(uint32_t x) { return 31 - __builtin_clz(x); }

/* Position of the least-significant set bit (0-based). */
static inline int bsf32(uint32_t x) { return __builtin_ctz(x); }

/* ─────────────────────────────────────────────────────────────────────────
 *  Size ↔ (fl, sl) index mapping
 *
 *  For size s >= BLOCK_SIZE_MIN (= 2^FL_INDEX_SHIFT):
 *    fl = bsr32(s) - FL_INDEX_SHIFT
 *    sl = (s >> (bsr32(s) - SL_INDEX_BITS)) & (SL_CLASS_COUNT - 1)
 *
 *  Each (fl, sl) class covers the half-open interval
 *    [base + sl * width,  base + (sl+1) * width)
 *  where base = 2^(fl + FL_INDEX_SHIFT) and width = 2^fl.
 *  The smallest class (fl=0, sl=0) covers exactly the single value
 *  BLOCK_SIZE_MIN (width = 1 byte at fl=0).
 * ───────────────────────────────────────────────────────────────────────── */

/* map_insert: floor mapping — used when *inserting* a freed block.          */
static void map_insert(size_t sz, int *fl_out, int *sl_out)
{
    int msb = bsr32((uint32_t)sz);
    *fl_out  = msb - (int)FL_INDEX_SHIFT;
    *sl_out  = (int)((sz >> (msb - (int)SL_INDEX_BITS)) & (SL_CLASS_COUNT - 1u));
}

/* map_search: ceiling mapping — used when *searching* for a free block of
 * at least `sz` bytes.  Rounds sz up so that the minimum block in the
 * returned class is >= sz, guaranteeing a valid fit in O(1).               */
static void map_search(size_t sz, int *fl_out, int *sl_out)
{
    /* At FL_INDEX_SHIFT each SL class is exactly 1 byte wide, so no rounding
     * is needed for fl=0.  For fl >= 1 the class width is 2^fl bytes and we
     * must round up by (class_width - 1).                                    */
    int msb = bsr32((uint32_t)sz);
    int shift = msb - (int)SL_INDEX_BITS;
    if (shift > 0)
        sz += (1u << shift) - 1u;   /* round up to next SL boundary          */
    map_insert(sz, fl_out, sl_out);
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Free-list bitmap operations
 * ───────────────────────────────────────────────────────────────────────── */

static void fl_insert(blk_t *b)
{
    int fl, sl;
    map_insert(blk_size(b), &fl, &sl);

    b->next_free = g_ctl.free_lists[fl][sl];
    b->prev_free = &g_ctl.null_blk;
    g_ctl.free_lists[fl][sl]->prev_free = b;
    g_ctl.free_lists[fl][sl] = b;

    g_ctl.fl_bitmap    |= (1u << fl);
    g_ctl.sl_bitmap[fl] |= (1u << sl);
}

static void fl_remove(blk_t *b)
{
    int fl, sl;
    map_insert(blk_size(b), &fl, &sl);

    b->prev_free->next_free = b->next_free;
    b->next_free->prev_free = b->prev_free;

    if (g_ctl.free_lists[fl][sl] == b) {
        g_ctl.free_lists[fl][sl] = b->next_free;
        if (b->next_free == &g_ctl.null_blk) {
            g_ctl.sl_bitmap[fl] &= ~(1u << sl);
            if (!g_ctl.sl_bitmap[fl])
                g_ctl.fl_bitmap &= ~(1u << fl);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 *  O(1) free-block search
 *
 *  Searches the bitmap for the lowest (fl, sl) class >= the requested
 *  (fl, sl) that contains at least one free block.
 * ───────────────────────────────────────────────────────────────────────── */
static blk_t *find_free_block(int fl, int sl)
{
    /* Search within the same FL for an SL class >= sl */
    uint32_t sl_map = g_ctl.sl_bitmap[fl] & (~0u << sl);
    if (!sl_map) {
        /* No suitable block at this FL level; search higher FL classes */
        uint32_t fl_map = g_ctl.fl_bitmap & (~0u << (fl + 1));
        if (!fl_map)
            return NULL;   /* completely out of memory */
        fl = bsf32(fl_map);
        sl_map = g_ctl.sl_bitmap[fl];
    }
    sl = bsf32(sl_map);
    return g_ctl.free_lists[fl][sl];   /* != &null_blk since sl_map != 0 */
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Block helpers
 * ───────────────────────────────────────────────────────────────────────── */

/* Propagate the free-status of `b` into the PREV_FREE flag of its
 * physical successor (needed to keep the coalescing invariant).            */
static inline void set_next_prev_free(blk_t *b, int is_free)
{
    blk_t *nxt = blk_next_phys(b);
    if (is_free) nxt->size_flags |=  PREV_FREE;
    else         nxt->size_flags &= ~PREV_FREE;
}

/* Split block `b` so that it holds `size` bytes of payload.
 * Returns the remainder block (now free, not yet in any list) or NULL if
 * the remainder would be smaller than BLOCK_SIZE_MIN.                      */
static blk_t *blk_split(blk_t *b, size_t size)
{
    size_t rem_sz = blk_size(b) - size - HDR_OVERHEAD;
    if (rem_sz < BLOCK_SIZE_MIN)
        return NULL;

    blk_t *tail = (blk_t *)((uint8_t *)blk_payload(b) + size);
    /* PREV_FREE = 0: `b` will be marked as used right after the split      */
    tail->size_flags = rem_sz | BLK_FREE;
    tail->prev_phys  = b;
    blk_next_phys(tail)->prev_phys = tail;

    blk_set_size(b, size);
    return tail;
}

/* Merge `b` with its physical next neighbour if that neighbour is free.   */
static blk_t *merge_next(blk_t *b)
{
    blk_t *nxt = blk_next_phys(b);
    if (blk_is_free(nxt) && !blk_is_last(nxt)) {
        fl_remove(nxt);
        blk_set_size(b, blk_size(b) + HDR_OVERHEAD + blk_size(nxt));
        blk_next_phys(b)->prev_phys = b;
    }
    return b;
}

/* Merge `b` with its physical previous neighbour if PREV_FREE is set.     */
static blk_t *merge_prev(blk_t *b)
{
    if (b->size_flags & PREV_FREE) {
        blk_t *prv = b->prev_phys;
        fl_remove(prv);
        blk_set_size(prv, blk_size(prv) + HDR_OVERHEAD + blk_size(b));
        blk_next_phys(prv)->prev_phys = prv;
        b = prv;
    }
    return b;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Public API
 * ───────────────────────────────────────────────────────────────────────── */

void tlsf_add_pool(void *mem, size_t bytes)
{
    /* Minimum: three HDR_OVERHEAD-sized sentinels + one minimum payload block */
    if (!mem || bytes < HDR_OVERHEAD * 3u + BLOCK_SIZE_MIN)
        return;

    /*
     * Pool memory layout:
     *   [sentinel_before (8B)] [main_block header (8B) + payload (P B)]
     *   [sentinel_after  (8B)]
     *
     *   P = bytes - 3 * HDR_OVERHEAD
     *
     * sentinel_before: size=0, not free, prev_phys=NULL
     *   → its physical successor is the main_block
     *   → main_block will see PREV_FREE=0 (sentinel is not free)
     *
     * sentinel_after: size=0 (blk_is_last == true)
     *   → merge_next / merge_prev never cross pool boundaries
     */
    blk_t *sb = (blk_t *)mem;
    sb->prev_phys  = NULL;
    sb->size_flags = 0;   /* size=0, !BLK_FREE, !PREV_FREE */

    size_t main_sz = bytes - HDR_OVERHEAD * 3u;
    blk_t *mb      = blk_next_phys(sb);   /* sb + HDR_OVERHEAD */
    mb->prev_phys  = sb;
    mb->size_flags = main_sz | BLK_FREE;  /* PREV_FREE=0: sb is not free */

    blk_t *se      = blk_next_phys(mb);   /* mb + HDR_OVERHEAD + main_sz */
    se->prev_phys  = mb;
    se->size_flags = PREV_FREE;            /* size=0, main_block is free */

    fl_insert(mb);
}

void tlsf_init(void *pool, size_t bytes)
{
    /* Zero the control structure (bitmaps, free-list pointers) */
    memset(&g_ctl, 0, sizeof(g_ctl));

    /* The null-block sentinel replaces NULL in all free-list links.
     * Initialise it to point to itself so insert / remove never
     * need NULL checks.                                                     */
    g_ctl.null_blk.prev_free  = &g_ctl.null_blk;
    g_ctl.null_blk.next_free  = &g_ctl.null_blk;
    g_ctl.null_blk.size_flags = 0;
    g_ctl.null_blk.prev_phys  = NULL;

    /* Point all free-list heads at the null sentinel */
    for (int f = 0; f < FL_INDEX_COUNT; f++)
        for (int s = 0; s < (int)SL_CLASS_COUNT; s++)
            g_ctl.free_lists[f][s] = &g_ctl.null_blk;

    tlsf_add_pool(pool, bytes);
}

void *tlsf_malloc(size_t size)
{
    if (size == 0) return NULL;

    /* Align up, enforce minimum payload size */
    size = (size + ALIGN_MASK) & ~ALIGN_MASK;
    if (size < BLOCK_SIZE_MIN) size = BLOCK_SIZE_MIN;

    /* Bounds check: request must fit within the maximum FL class */
    if (size > (1u << FL_INDEX_MAX))
        return NULL;

    int fl, sl;
    map_search(size, &fl, &sl);

    if (fl >= FL_INDEX_COUNT)
        return NULL;

    blk_t *b = find_free_block(fl, sl);
    if (!b || b == &g_ctl.null_blk)
        return NULL;

    fl_remove(b);

    /* Split the block if the remainder can form a valid free block */
    blk_t *tail = blk_split(b, size);
    if (tail) {
        /* tail->PREV_FREE is already 0 (b is about to be marked used) */
        fl_insert(tail);
    }

    /* Mark b as used and tell its successor that its predecessor is not free */
    b->size_flags &= ~BLK_FREE;
    set_next_prev_free(b, 0);

    return blk_payload(b);
}

void tlsf_free(void *ptr)
{
    if (!ptr) return;

    blk_t *b = blk_from_payload(ptr);

    /* Mark block as free and update the successor's PREV_FREE flag */
    b->size_flags |= BLK_FREE;
    set_next_prev_free(b, 1);

    /* Coalesce with physically adjacent free blocks (both O(1)) */
    b = merge_next(b);
    b = merge_prev(b);

    fl_insert(b);
}

void *tlsf_realloc(void *ptr, size_t new_size)
{
    if (!ptr)      return tlsf_malloc(new_size);
    if (!new_size) { tlsf_free(ptr); return NULL; }

    blk_t *b = blk_from_payload(ptr);

    /* Align new_size the same way tlsf_malloc would */
    new_size = (new_size + ALIGN_MASK) & ~ALIGN_MASK;
    if (new_size < BLOCK_SIZE_MIN) new_size = BLOCK_SIZE_MIN;

    /* If the current block is already large enough, return as-is */
    if (blk_size(b) >= new_size)
        return ptr;

    void *new_ptr = tlsf_malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, blk_size(b));
        tlsf_free(ptr);
    }
    return new_ptr;
}

void tlsf_dump_stats(void)
{
    size_t   total_free   = 0;
    unsigned free_blocks  = 0;

    for (int f = 0; f < FL_INDEX_COUNT; f++) {
        for (int s = 0; s < (int)SL_CLASS_COUNT; s++) {
            blk_t *b = g_ctl.free_lists[f][s];
            while (b != &g_ctl.null_blk) {
                total_free += blk_size(b);
                free_blocks++;
                b = b->next_free;
            }
        }
    }

    kprintf("--- TLSF Heap Stats ---\n");
    kprintf("Free: %u KB (%u free blocks)\n",
            (unsigned)(total_free / 1024u), free_blocks);
    kprintf("-----------------------\n");
}
