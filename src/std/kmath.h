//
// src/std/kmath.h — 커널 수학 라이브러리
// libm 없이 동작하는 freestanding 수학 함수 모음
//

#ifndef PARINOS_KMATH_H
#define PARINOS_KMATH_H

#include <stdint.h>

/* ── 정수 유틸리티 ─────────────────────────────────────────────────────────── */
static inline int   kabs(int x)            { return x < 0 ? -x : x; }
static inline long  klabs(long x)          { return x < 0 ? -x : x; }

static inline int   kmin(int a, int b)     { return a < b ? a : b; }
static inline int   kmax(int a, int b)     { return a > b ? a : b; }
static inline int   kclamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t kmin_u(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t kmax_u(uint32_t a, uint32_t b) { return a > b ? a : b; }

/* ── 부동소수점 기초 ───────────────────────────────────────────────────────── */
static inline float kfabsf(float x)  { return x < 0.0f ? -x : x; }
static inline float kfloorf(float x) {
    int i = (int)x;
    return (float)(i - (x < (float)i ? 1 : 0));
}
static inline float kceilf(float x) {
    int i = (int)x;
    return (float)(i + (x > (float)i ? 1 : 0));
}
static inline float kroundf(float x) {
    return (x >= 0.0f) ? kfloorf(x + 0.5f) : kceilf(x - 0.5f);
}
static inline float ktruncf(float x) { return (float)(int)x; }

/* ── 제곱근 (Newton–Raphson) ──────────────────────────────────────────────── */
float ksqrtf(float x);

/* ── 거듭제곱 ─────────────────────────────────────────────────────────────── */
float kpowf(float base, float exp);
int   kipow(int base, int exp);     /* 정수 거듭제곱 */

/* ── 삼각 함수 (Taylor 급수, 소각도 근사) ─────────────────────────────────── */
float ksinf(float x);
float kcosf(float x);
float ktanf(float x);

/* ── 지수 / 로그 ──────────────────────────────────────────────────────────── */
float kexpf(float x);
float klogf(float x);   /* 자연 로그 */
float klog2f(float x);
float klog10f(float x);

/* ── 나머지 ───────────────────────────────────────────────────────────────── */
static inline float kfmodf(float x, float y) {
    if (y == 0.0f) return 0.0f;
    int q = (int)(x / y);
    return x - (float)q * y;
}

/* ── 선형 보간 ────────────────────────────────────────────────────────────── */
static inline float klerpf(float a, float b, float t) {
    return a + t * (b - a);
}

/* ── 수학 상수 ────────────────────────────────────────────────────────────── */
#define KM_PI       3.14159265358979323846f
#define KM_PI2      6.28318530717958647692f
#define KM_PI_2     1.57079632679489661923f
#define KM_PI_4     0.78539816339744830962f
#define KM_E        2.71828182845904523536f
#define KM_SQRT2    1.41421356237309504880f
#define KM_LN2      0.69314718055994530942f

#endif /* PARINOS_KMATH_H */
