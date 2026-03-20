//
// src/std/kstdio.h — 커널 표준 입출력
// kprintf / kscanf 및 버퍼 기반 입출력 추상화
//

#ifndef PARINOS_KSTDIO_H
#define PARINOS_KSTDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ── 출력 (vga.h 래퍼) ────────────────────────────────────────────────────── */

/** kprintf — 커널 포맷 출력 (vga.h 의 kprintf 와 동일 프로토타입) */
void kprintf(const char *fmt, ...);

/** kvprintf — va_list 를 받는 포맷 출력 */
void kvprintf(const char *fmt, va_list ap);

/** ksprintf — 버퍼에 포맷 출력 (크기 제한 없음 — 가급적 ksnprintf 사용) */
int  ksprintf(char *buf, const char *fmt, ...);

/** ksnprintf — 크기 제한 버퍼 포맷 출력 */
int  ksnprintf(char *buf, size_t size, const char *fmt, ...);

/* ── 입력 ─────────────────────────────────────────────────────────────────── */

/**
 * kscanf — 커널 표준 입력으로부터 형식화된 입력 읽기.
 * 지원 포맷: %d, %u, %x, %s, %c, %%
 * @return 성공적으로 읽은 항목 수
 *
 * 내부적으로 keyboard_readline() 을 호출해 한 줄을 읽은 뒤 파싱합니다.
 */
int kscanf(const char *fmt, ...);

/**
 * ksscanf — 문자열에서 형식화된 입력 파싱.
 * @param src  파싱할 문자열
 * @param fmt  포맷 문자열
 * @return 성공적으로 읽은 항목 수
 */
int ksscanf(const char *src, const char *fmt, ...);

/**
 * keyboard_readline — 키보드로부터 개행 문자까지 읽어 buf 에 저장.
 * @param buf     출력 버퍼
 * @param maxlen  버퍼 크기 (개행 포함하지 않음, NUL 포함)
 * @return 읽은 문자 수 (NUL 제외)
 */
int keyboard_readline(char *buf, int maxlen);

#endif /* PARINOS_KSTDIO_H */
