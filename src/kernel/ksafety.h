//
// src/kernel/ksafety.h — 커널 안전성 유틸리티
//
// 페이지 폴트 / 트리플 폴트 발생 가능성을 줄이기 위한
// NULL 포인터 가드 매크로, 스택 카나리, 범위 검사 도우미입니다.
//

#ifndef PARINOS_KSAFETY_H
#define PARINOS_KSAFETY_H

#include <stdint.h>
#include <stddef.h>
#include "../kernel/kernel_status_manager.h"

/* ── NULL 포인터 / 주소 검사 ─────────────────────────────────────────────── */

/**
 * KASSERT — 조건이 거짓이면 커널 패닉.
 * 릴리즈 빌드에서도 유지됩니다.
 */
#define KASSERT(cond) \
    do { \
        if (!(cond)) \
            kernel_panic("KASSERT failed: " #cond, (uint32_t)__FILE__); \
    } while (0)

/** NULL 포인터 검사 후 패닉 */
#define KASSERT_NONNULL(ptr) \
    KASSERT((ptr) != NULL)

/** 정수가 범위 [lo, hi) 안에 있는지 확인 */
#define KASSERT_RANGE(val, lo, hi) \
    KASSERT((uint32_t)(val) >= (uint32_t)(lo) && (uint32_t)(val) < (uint32_t)(hi))

/**
 * KCHECK_PTR — 포인터가 NULL 이면 경고만 출력하고 리턴 (패닉 없음).
 * @param ptr    검사할 포인터
 * @param retval 포인터가 NULL 일 때 반환할 값 (void 함수는 사용 금지)
 */
#define KCHECK_PTR(ptr, retval) \
    do { \
        if ((ptr) == NULL) { \
            klog_error("[ksafety] NULL ptr at %s:%d\n", __FILE__, __LINE__); \
            return (retval); \
        } \
    } while (0)

#define KCHECK_PTR_VOID(ptr) \
    do { \
        if ((ptr) == NULL) { \
            klog_error("[ksafety] NULL ptr at %s:%d\n", __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/* ── 스택 카나리 ──────────────────────────────────────────────────────────── */

/** 스택 카나리 매직 값 */
#define KSTACK_CANARY_MAGIC 0xDEADB00F

/**
 * KSTACK_CANARY_DECLARE — 함수 첫 줄에서 카나리를 선언합니다.
 * KSTACK_CANARY_CHECK   — 함수 마지막에 카나리 값을 확인합니다.
 *
 * 사용 예:
 *   void my_func(void) {
 *       KSTACK_CANARY_DECLARE;
 *       // ... 로직 ...
 *       KSTACK_CANARY_CHECK;
 *   }
 */
#define KSTACK_CANARY_DECLARE \
    volatile uint32_t _ksafety_canary = KSTACK_CANARY_MAGIC

#define KSTACK_CANARY_CHECK \
    do { \
        if (_ksafety_canary != KSTACK_CANARY_MAGIC) \
            kernel_panic("Stack overflow / corruption detected", (uint32_t)__func__); \
    } while (0)

/* ── 주소 유효성 검사 헬퍼 ───────────────────────────────────────────────── */

/** 커널 코드/데이터 물리 범위 [1MB, 8MB) 안인지 확인 */
static inline int ksafety_is_kernel_range(uint32_t addr) {
    return (addr >= 0x100000u && addr < 0x800000u);
}

/** 유저 공간 [4MB, 3GB) 안인지 확인 */
static inline int ksafety_is_user_range(uint32_t addr) {
    return (addr >= 0x400000u && addr < 0xC0000000u);
}

/**
 * ksafety_validate_ptr — 포인터가 예상 메모리 영역 안에 있는지 확인합니다.
 * 문제가 있으면 커널 패닉.
 *
 * @param ptr    확인할 포인터
 * @param size   접근할 크기(바이트)
 * @param in_kernel 커널 범위이면 1, 유저 범위이면 0
 */
void ksafety_validate_ptr(const void *ptr, size_t size, int in_kernel);

/* ── 메모리 오버플로 방지 래퍼 ───────────────────────────────────────────── */

/**
 * ksafe_memcpy — 크기와 포인터를 검사한 뒤 memcpy.
 * dest 또는 src 가 NULL 이거나 크기가 0 이면 아무것도 하지 않고 NULL 반환.
 */
void *ksafe_memcpy(void *dest, const void *src, size_t n);

/**
 * ksafe_memset — 크기와 포인터를 검사한 뒤 memset.
 */
void *ksafe_memset(void *dest, int val, size_t n);

/**
 * ksafe_strncpy — n 바이트 제한 문자열 복사.
 * 항상 NUL 종결 보장.
 */
char *ksafe_strncpy(char *dest, const char *src, size_t n);

#endif /* PARINOS_KSAFETY_H */
