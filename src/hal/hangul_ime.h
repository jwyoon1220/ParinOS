//
// src/hangul_ime.h — 한글 조합 IME 기본 구현
//
// 두벌식 자판 기준으로 자모를 조합하여 KS X 1001 (완성형 한글) 코드포인트를
// 생성합니다.  Unicode 완성형 코드: 0xAC00 + (초성×21 + 중성)×28 + 종성
//
// 사용법:
//   1. hangul_ime_init() 호출 (초기화)
//   2. 키 입력마다 hangul_ime_input(scancode, shift) 호출
//      → 반환값이 0 이면 아직 조합 중, 양수이면 확정된 유니코드 코드포인트
//      → 0xFFFF_FFFF 이면 IME 가 끌 때 마지막 글자 출력
//   3. hangul_ime_flush() 로 미완성 글자 강제 확정 (커서 이동, 개행 등)
//

#ifndef PARINOS_HANGUL_IME_H
#define PARINOS_HANGUL_IME_H

#include <stdint.h>

/* 초성 (19자) */
static const uint8_t hangul_choseong[19] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18
};

/* 중성 (21자) */
static const uint8_t hangul_jungseong[21] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
};

/* 종성 (28자, 0 = 없음) */
static const uint8_t hangul_jongseong[28] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27
};

/* 두벌식 자판: 스캔코드 → 자소 매핑 */
/* 자소 값 인코딩: 0x00..0x12 = 초성, 0x20..0x34 = 중성, 0x40..0x5B = 종성 후보 */
#define HK_NONE  0xFF
#define HK_CHO(n)  (uint8_t)((n) & 0x1F)
#define HK_JUNG(n) (uint8_t)(0x20 | ((n) & 0x1F))

/* 두벌식 배열 (shift=0, 1) — 한글 자모만 (영문 키 기준 매핑) */
typedef struct {
    uint8_t normal; /* HK_NONE, HK_CHO, HK_JUNG */
    uint8_t shift;
} kbd_hangul_map_t;

/* IME 상태 */
typedef struct {
    int8_t  cho;    /* 현재 초성 인덱스 (-1: 없음) */
    int8_t  jung;   /* 현재 중성 인덱스 (-1: 없음) */
    int8_t  jong;   /* 현재 종성 인덱스 (-1: 없음) */
    int     active; /* 1 = 한글 모드, 0 = 영문 모드 */
} hangul_ime_state_t;

/** IME 초기화 */
void hangul_ime_init(void);

/** 한/영 모드 토글 */
void hangul_ime_toggle(void);

/** 현재 한글 모드 여부 반환 */
int hangul_ime_is_korean(void);

/**
 * 스캔코드를 입력으로 받아 완성된 유니코드 코드포인트를 반환합니다.
 * @param scancode  PS/2 스캔코드 (Make Code)
 * @param shift     Shift 키 눌림 여부
 * @return  0        : 아직 조합 중 (출력 없음)
 *          0xFFFFFFFE: 마지막 완성자 + 현재 초성 시작 (이전 글자 확정)
 *          양수      : 완성된 유니코드 코드포인트
 *          0xFFFFFFFF: IME 비활성
 */
uint32_t hangul_ime_input(uint8_t scancode, int shift);

/**
 * 미완성 글자를 강제로 확정합니다.
 * @return 완성된 유니코드 코드포인트 (조합 중 글자 없으면 0)
 */
uint32_t hangul_ime_flush(void);

/**
 * 유니코드 코드포인트를 UTF-8 바이트 시퀀스로 변환합니다.
 * @param cp   코드포인트
 * @param buf  출력 버퍼 (최소 5바이트)
 * @return 변환된 바이트 수
 */
int unicode_to_utf8(uint32_t cp, char *buf);

#endif /* PARINOS_HANGUL_IME_H */
