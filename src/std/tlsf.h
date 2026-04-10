/*
 * src/std/tlsf.h — Two-Level Segregated Fit (TLSF) allocator
 *
 * O(1) malloc / free for the ParinOS kernel heap.
 *
 * Reference:
 *   "TLSF: A New Dynamic Memory Allocator for Real-Time Systems"
 *   M. Masmano, I. Ripoll, A. Crespo, J. Real — ECRTS 2004
 */

#ifndef PARINOS_TLSF_H
#define PARINOS_TLSF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the TLSF allocator with a single memory pool.
 * Must be called once before any tlsf_malloc / tlsf_free.
 *
 * @param pool   Start of the initial memory region (must be 4-byte aligned).
 * @param bytes  Size of the region in bytes.
 */
void  tlsf_init    (void *pool, size_t bytes);

/**
 * Add a new contiguous memory region to the TLSF pool.
 * Call this when the heap needs to grow.
 *
 * @param pool   Start of the new region (must be 4-byte aligned).
 * @param bytes  Size of the region in bytes.
 */
void  tlsf_add_pool(void *pool, size_t bytes);

/**
 * Allocate `size` bytes. Returns NULL on failure.
 * Complexity: O(1).
 */
void *tlsf_malloc  (size_t size);

/**
 * Free a pointer returned by tlsf_malloc.
 * Complexity: O(1).
 */
void  tlsf_free    (void *ptr);

/**
 * Resize an allocation. Behaves like standard realloc.
 */
void *tlsf_realloc (void *ptr, size_t new_size);

/**
 * Print summary heap statistics via kprintf (for debugging).
 */
void  tlsf_dump_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* PARINOS_TLSF_H */
