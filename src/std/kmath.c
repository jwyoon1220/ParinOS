//
// src/std/kmath.c — 커널 수학 라이브러리 구현
//

#include "kmath.h"

/* ── 제곱근 (Newton–Raphson 24회 반복) ─────────────────────────────────────── */
float ksqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = (x > 1.0f) ? x * 0.5f : 1.0f;
    int i;
    for (i = 0; i < 24; i++) {
        float prev = g;
        g = (g + x / g) * 0.5f;
        if (kfabsf(g - prev) < 1e-7f) break;
    }
    return g;
}

/* ── 정수 거듭제곱 ─────────────────────────────────────────────────────────── */
int kipow(int base, int exp) {
    int result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

/* ── 부동소수 거듭제곱 (exp=정수 부분만 지원, 소수 지수는 e^(y*ln x) 근사) ── */
float kpowf(float base, float exp) {
    if (exp == 0.0f) return 1.0f;
    if (base == 0.0f) return 0.0f;

    /* 지수가 정수인 경우 빠른 경로 */
    int iexp = (int)exp;
    if ((float)iexp == exp) {
        float r = 1.0f;
        int   n = (iexp < 0) ? -iexp : iexp;
        float b = base;
        while (n > 0) {
            if (n & 1) r *= b;
            b *= b;
            n >>= 1;
        }
        return (iexp < 0) ? (1.0f / r) : r;
    }

    /* 일반 경우: e^(y * ln(x)) */
    if (base < 0.0f) return 0.0f;
    return kexpf(exp * klogf(base));
}

/* ── 삼각 함수 (Maclaurin 급수) ─────────────────────────────────────────────
 *   범위 정규화: x를 [-π, π] 로 줄임
 * ─────────────────────────────────────────────────────────────────────────── */
static float normalize_angle(float x) {
    /* x를 [−π, π] 범위로 정규화 */
    while (x >  KM_PI) x -= KM_PI2;
    while (x < -KM_PI) x += KM_PI2;
    return x;
}

float ksinf(float x) {
    x = normalize_angle(x);
    float term = x;
    float sum  = x;
    float x2   = x * x;
    int   n;
    for (n = 1; n <= 7; n++) {
        term *= -x2 / (float)((2 * n) * (2 * n + 1));
        sum  += term;
    }
    return sum;
}

float kcosf(float x) {
    x = normalize_angle(x);
    float term = 1.0f;
    float sum  = 1.0f;
    float x2   = x * x;
    int   n;
    for (n = 1; n <= 7; n++) {
        term *= -x2 / (float)((2 * n - 1) * (2 * n));
        sum  += term;
    }
    return sum;
}

float ktanf(float x) {
    float c = kcosf(x);
    if (kfabsf(c) < 1e-7f) return 0.0f;
    return ksinf(x) / c;
}

/* ── 지수 함수 e^x (Maclaurin 급수) ────────────────────────────────────────── */
float kexpf(float x) {
    float term = 1.0f;
    float sum  = 1.0f;
    int   n;
    for (n = 1; n <= 20; n++) {
        term *= x / (float)n;
        sum  += term;
        if (kfabsf(term) < 1e-9f) break;
    }
    return sum;
}

/* ── 자연 로그 ln(x)   (x > 0)
 *   ln(x) = 2 * arctanh((x-1)/(x+1))  — 빠른 수렴
 * ─────────────────────────────────────────────────────────────────────────── */
float klogf(float x) {
    if (x <= 0.0f) return -3.402823466e+38f; /* -FLT_MAX 근사 */

    /* 지수를 정수 부분과 가수로 분리 */
    int   k = 0;
    float t = x;
    while (t >= 2.0f)  { t *= 0.5f; k++; }
    while (t < 1.0f)   { t *= 2.0f; k--; }
    /* t ∈ [1, 2) */
    float y   = (t - 1.0f) / (t + 1.0f);
    float y2  = y * y;
    float sum = 0.0f;
    float pw  = y;
    int   n;
    for (n = 0; n < 20; n++) {
        sum += pw / (float)(2 * n + 1);
        pw  *= y2;
        if (kfabsf(pw) < 1e-9f) break;
    }
    return 2.0f * sum + (float)k * KM_LN2;
}

float klog2f(float x)  { return klogf(x) / KM_LN2; }
float klog10f(float x) { return klogf(x) / 2.302585092994046f; }
