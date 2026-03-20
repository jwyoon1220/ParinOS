//
// src/std/kstdlib.h — 커널 표준 라이브러리
// freestanding 환경에서 libc 없이 동작하는 stdlib 함수 모음
//

#ifndef PARINOS_KSTDLIB_H
#define PARINOS_KSTDLIB_H

#include <stdint.h>
#include <stddef.h>

/* ── 동적 메모리 (malloc.h 위임) ─────────────────────────────────────────── */
#include "malloc.h"     /* kmalloc / kcalloc / kfree / krealloc */

/* ── 숫자 ↔ 문자열 변환 ──────────────────────────────────────────────────── */

/** 10진 문자열을 int로 변환 (선행 공백과 부호 지원) */
int   katoi(const char *s);

/** 10진 문자열을 long으로 변환 */
long  katol(const char *s);

/** 부동소수점 문자열을 double로 변환 */
double katof(const char *s);

/**
 * int를 지정 진수(base) 문자열로 변환.
 * @param value   변환할 값
 * @param buf     출력 버퍼 (최소 33바이트 권장)
 * @param base    진수 (2–36)
 * @return buf 포인터
 */
char *kitoa(int value, char *buf, int base);

/** unsigned int를 문자열로 변환 */
char *kutoa(unsigned int value, char *buf, int base);

/* ── 랜덤 ────────────────────────────────────────────────────────────────── */
#define KRAND_MAX 0x7FFFFFFF

void ksrand(uint32_t seed);
int  krand(void);

/* ── 정렬 / 탐색 ──────────────────────────────────────────────────────────── */

/**
 * qsort 호환 인터페이스 (비재귀 quicksort).
 * @param base   배열 포인터
 * @param nmemb  원소 개수
 * @param size   원소 크기(바이트)
 * @param compar 비교 함수 포인터 (음수/0/양수)
 */
void kqsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

/**
 * bsearch 호환 인터페이스 (이진 탐색).
 * 정렬된 배열에서 key와 일치하는 원소의 포인터를 반환합니다.
 * 없으면 NULL 반환.
 */
void *kbsearch(const void *key, const void *base,
               size_t nmemb, size_t size,
               int (*compar)(const void *, const void *));

/* ── 환경 / 프로세스 (커널 내 스텁) ─────────────────────────────────────── */

/** 커널 패닉으로 처리되는 abort */
void kabort(void) __attribute__((noreturn));

/** atexit 스텁 (커널에서는 no-op) */
int  katexit(void (*func)(void));

#endif /* PARINOS_KSTDLIB_H */
