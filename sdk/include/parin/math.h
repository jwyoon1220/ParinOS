/*
 * sdk/include/parin/math.h — ParinOS SDK: Integer Math Utilities
 *
 * All functions are header-only static inlines; no extra object file needed.
 */

#ifndef PARIN_MATH_H
#define PARIN_MATH_H

#include <stddef.h>
#include <stdint.h>

/* ── Clamping ─────────────────────────────────────────────────────────────── */

/** Return val clamped to [lo, hi]. */
static inline int parin_clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/** Unsigned variant of clamp. */
static inline unsigned int parin_uclampu(unsigned int val,
                                         unsigned int lo, unsigned int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* ── Alignment helpers ────────────────────────────────────────────────────── */

/** Round val up to the next multiple of align (align must be a power of two). */
static inline unsigned int parin_round_up(unsigned int val, unsigned int align) {
    return (val + align - 1) & ~(align - 1);
}

/** Round val down to the previous multiple of align (align must be a power of two). */
static inline unsigned int parin_round_down(unsigned int val, unsigned int align) {
    return val & ~(align - 1);
}

/* ── Power-of-two test ────────────────────────────────────────────────────── */

/** Return 1 if n is a power of two (n > 0), 0 otherwise. */
static inline int parin_is_pow2(unsigned int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

/* ── Absolute value ───────────────────────────────────────────────────────── */

static inline int parin_abs(int x) {
    return x < 0 ? -x : x;
}

/* ── Min / max ────────────────────────────────────────────────────────────── */

static inline int parin_min(int a, int b) { return a < b ? a : b; }
static inline int parin_max(int a, int b) { return a > b ? a : b; }

static inline unsigned int parin_minu(unsigned int a, unsigned int b) {
    return a < b ? a : b;
}
static inline unsigned int parin_maxu(unsigned int a, unsigned int b) {
    return a > b ? a : b;
}

/* ── Bit utilities ────────────────────────────────────────────────────────── */

/** Count the number of set bits (popcount) in a 32-bit word. */
static inline int parin_popcount(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (int)((x * 0x01010101u) >> 24);
}

/** Return the position of the highest set bit (0-based), or -1 if x == 0. */
static inline int parin_highest_bit(uint32_t x) {
    if (x == 0) return -1;
    int n = 0;
    if (x & 0xFFFF0000u) { n += 16; x >>= 16; }
    if (x & 0xFF00u)     { n +=  8; x >>=  8; }
    if (x & 0xF0u)       { n +=  4; x >>=  4; }
    if (x & 0xCu)        { n +=  2; x >>=  2; }
    if (x & 0x2u)        { n +=  1; }
    return n;
}

#endif /* PARIN_MATH_H */
