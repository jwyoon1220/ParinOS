//
// src/hangul_ime.c — 한글 조합 IME 구현 (두벌식)
//
// 유니코드 완성형 한글 코드포인트 계산:
//   codepoint = 0xAC00 + (cho * 21 + jung) * 28 + jong
//

#include "hangul_ime.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * 두벌식 자판 스캔코드 → 자소 매핑
 * 스캔코드 세트 1 기준 (PS/2 키보드 표준)
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * 두벌식 배열:
 *   일반: ㅂ ㅈ ㄷ ㄱ ㅅ ㅛ ㅕ ㅑ ㅐ ㅔ ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ
 *         ㅋ ㅌ ㅊ ㅍ ㅠ ㅜ ㅡ
 *   Shift: ㅃ ㅉ ㄸ ㄲ ㅆ ㅛ ㅕ ㅑ ㅒ ㅖ ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ
 *
 * 초성 인덱스 (KS X 1001):
 *   ㄱ=0 ㄲ=1 ㄴ=2 ㄷ=3 ㄸ=4 ㄹ=5 ㅁ=6 ㅂ=7 ㅃ=8 ㅅ=9 ㅆ=10
 *   ㅇ=11 ㅈ=12 ㅉ=13 ㅊ=14 ㅋ=15 ㅌ=16 ㅍ=17 ㅎ=18
 *
 * 중성 인덱스:
 *   ㅏ=0 ㅐ=1 ㅑ=2 ㅒ=3 ㅓ=4 ㅔ=5 ㅕ=6 ㅖ=7 ㅗ=8 ㅘ=9 ㅙ=10 ㅚ=11
 *   ㅛ=12 ㅜ=13 ㅝ=14 ㅞ=15 ㅟ=16 ㅠ=17 ㅡ=18 ㅢ=19 ㅣ=20
 *
 * 종성 인덱스:
 *   (없음)=0 ㄱ=1 ㄲ=2 ㄳ=3 ㄴ=4 ㄵ=5 ㄶ=6 ㄷ=7 ㄹ=8 ㄺ=9 ㄻ=10 ㄼ=11
 *   ㄽ=12 ㄾ=13 ㄿ=14 ㅀ=15 ㅁ=16 ㅂ=17 ㅄ=18 ㅅ=19 ㅆ=20 ㅇ=21
 *   ㅈ=22 ㅊ=23 ㅋ=24 ㅌ=25 ㅍ=26 ㅎ=27
 */

/* 자소 타입 마커 */
#define J_NONE   0xFF
#define J_CHO    0x00  /* 초성 전용 — 값에 초성 인덱스를 더함 */
#define J_JUNG   0x40  /* 중성 전용 — 값에 중성 인덱스를 더함 */
#define J_DUAL   0x80  /* 초성/종성 겸용 */

/* 자소 인코딩: 상위 2비트 = 타입, 하위 6비트 = 인덱스 */
#define CHO(i)  ((uint8_t)(J_CHO  | (i)))
#define JUNG(i) ((uint8_t)(J_JUNG | (i)))
#define DUAL(cho_i, jong_lookup) ((uint8_t)(J_DUAL | (cho_i))) /* 종성 처리는 별도 */

/*
 * 스캔코드 → 한글 자소 테이블 (일반 / Shift)
 * 인덱스: PS/2 스캔코드 (0x00 ~ 0x3A 범위만)
 * 값: J_NONE(=없음), CHO(초성인덱스), JUNG(중성인덱스)
 *
 * 두벌식 배치 (일반):
 *   q=ㅂ(초성7) w=ㅈ(12) e=ㄷ(3)  r=ㄱ(0) t=ㅅ(9)
 *   y=ㅛ(중12) u=ㅕ(6)  i=ㅑ(2)  o=ㅐ(1) p=ㅔ(5)
 *   a=ㅁ(6)    s=ㄴ(2)  d=ㅇ(11) f=ㄹ(5) g=ㅎ(18)
 *   h=ㅗ(8)    j=ㅓ(4)  k=ㅏ(0)  l=ㅣ(20)
 *   z=ㅋ(15)   x=ㅌ(16) c=ㅊ(14) v=ㅍ(17)
 *   b=ㅠ(17)   n=ㅜ(13) m=ㅡ(18)
 *
 * Shift:
 *   q=ㅃ(8)  w=ㅉ(13) e=ㄸ(4) r=ㄲ(1) t=ㅆ(10)
 *   o=ㅒ(3)  p=ㅖ(7)
 */

/* 초성 ↔ 종성 연결 테이블 (종성이 없으면 -1) */
/* 초성 인덱스 → 종성 인덱스 */
static const int8_t cho_to_jong[19] = {
    1,  /* ㄱ → 종성1  */
    2,  /* ㄲ → 종성2  */
    4,  /* ㄴ → 종성4  */
    7,  /* ㄷ → 종성7  */
    -1, /* ㄸ → 없음   */
    8,  /* ㄹ → 종성8  */
    16, /* ㅁ → 종성16 */
    17, /* ㅂ → 종성17 */
    -1, /* ㅃ → 없음   */
    19, /* ㅅ → 종성19 */
    20, /* ㅆ → 종성20 */
    21, /* ㅇ → 종성21 */
    22, /* ㅈ → 종성22 */
    -1, /* ㅉ → 없음   */
    23, /* ㅊ → 종성23 */
    24, /* ㅋ → 종성24 */
    25, /* ㅌ → 종성25 */
    26, /* ㅍ → 종성26 */
    27, /* ㅎ → 종성27 */
};

/* 스캔코드(0x00~0x37) → (일반 자소, Shift 자소) */
/* 0xFF = 자소 없음 */
typedef struct { uint8_t n, s; } HKeymap;

/* CHO(i): 초성 i   JUNG(i): 중성 i   0xFF: 해당 없음 */
static const HKeymap scancode_hangul[0x40] = {
    /* 0x00 */ {0xFF, 0xFF},
    /* 0x01 */ {0xFF, 0xFF}, /* ESC */
    /* 0x02..0x0B: 숫자 행 */
    {0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},
    {0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},
    {0xFF,0xFF},{0xFF,0xFF},
    /* 0x0C, 0x0D */ {0xFF,0xFF},{0xFF,0xFF},
    /* 0x0E: backspace */ {0xFF,0xFF},
    /* 0x0F: tab */ {0xFF,0xFF},
    /* 0x10: q */ {CHO(7),  CHO(8)  }, /* ㅂ / ㅃ */
    /* 0x11: w */ {CHO(12), CHO(13) }, /* ㅈ / ㅉ */
    /* 0x12: e */ {CHO(3),  CHO(4)  }, /* ㄷ / ㄸ */
    /* 0x13: r */ {CHO(0),  CHO(1)  }, /* ㄱ / ㄲ */
    /* 0x14: t */ {CHO(9),  CHO(10) }, /* ㅅ / ㅆ */
    /* 0x15: y */ {JUNG(12),JUNG(12)}, /* ㅛ */
    /* 0x16: u */ {JUNG(6), JUNG(6) }, /* ㅕ */
    /* 0x17: i */ {JUNG(2), JUNG(2) }, /* ㅑ */
    /* 0x18: o */ {JUNG(1), JUNG(3) }, /* ㅐ / ㅒ */
    /* 0x19: p */ {JUNG(5), JUNG(7) }, /* ㅔ / ㅖ */
    /* 0x1A, 0x1B */ {0xFF,0xFF},{0xFF,0xFF},
    /* 0x1C: enter */ {0xFF,0xFF},
    /* 0x1D: ctrl */  {0xFF,0xFF},
    /* 0x1E: a */ {CHO(6),  CHO(6)  }, /* ㅁ */
    /* 0x1F: s */ {CHO(2),  CHO(2)  }, /* ㄴ */
    /* 0x20: d */ {CHO(11), CHO(11) }, /* ㅇ */
    /* 0x21: f */ {CHO(5),  CHO(5)  }, /* ㄹ */
    /* 0x22: g */ {CHO(18), CHO(18) }, /* ㅎ */
    /* 0x23: h */ {JUNG(8), JUNG(8) }, /* ㅗ */
    /* 0x24: j */ {JUNG(4), JUNG(4) }, /* ㅓ */
    /* 0x25: k */ {JUNG(0), JUNG(0) }, /* ㅏ */
    /* 0x26: l */ {JUNG(20),JUNG(20)}, /* ㅣ */
    /* 0x27..0x29 */ {0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},
    /* 0x2A: shift */ {0xFF,0xFF},
    /* 0x2B */ {0xFF,0xFF},
    /* 0x2C: z */ {CHO(15), CHO(15) }, /* ㅋ */
    /* 0x2D: x */ {CHO(16), CHO(16) }, /* ㅌ */
    /* 0x2E: c */ {CHO(14), CHO(14) }, /* ㅊ */
    /* 0x2F: v */ {CHO(17), CHO(17) }, /* ㅍ */
    /* 0x30: b */ {JUNG(17),JUNG(17)}, /* ㅠ */
    /* 0x31: n */ {JUNG(13),JUNG(13)}, /* ㅜ */
    /* 0x32: m */ {JUNG(18),JUNG(18)}, /* ㅡ */
    /* 0x33..0x37 */ {0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},{0xFF,0xFF},
};

/* ─────────────────────────────────────────────────────────────────────────────
 * IME 상태
 * ───────────────────────────────────────────────────────────────────────────*/
static hangul_ime_state_t g_ime = { -1, -1, -1, 0 };

void hangul_ime_init(void) {
    g_ime.cho    = -1;
    g_ime.jung   = -1;
    g_ime.jong   = -1;
    g_ime.active = 0;
}

void hangul_ime_toggle(void) {
    g_ime.active = !g_ime.active;
    if (!g_ime.active) {
        g_ime.cho = g_ime.jung = g_ime.jong = -1;
    }
}

int hangul_ime_is_korean(void) {
    return g_ime.active;
}

/* 현재 조합 글자 코드포인트 계산 */
static uint32_t make_codepoint(int cho, int jung, int jong) {
    if (cho < 0 || jung < 0) return 0;
    int j = (jong < 0) ? 0 : jong;
    return (uint32_t)(0xAC00 + (cho * 21 + jung) * 28 + j);
}

uint32_t hangul_ime_flush(void) {
    if (g_ime.cho < 0 && g_ime.jung < 0) return 0;
    uint32_t cp = make_codepoint(g_ime.cho, g_ime.jung, g_ime.jong);
    g_ime.cho = g_ime.jung = g_ime.jong = -1;
    return cp;
}

uint32_t hangul_ime_input(uint8_t scancode, int shift) {
    if (!g_ime.active) return 0xFFFFFFFF;
    if (scancode >= 0x40) return 0;

    HKeymap km = scancode_hangul[scancode];
    uint8_t code = shift ? km.s : km.n;
    if (code == 0xFF) {
        /* 자소가 아닌 키 → 현재 조합 확정 */
        return hangul_ime_flush();
    }

    int type = code & 0xC0; /* 상위 2비트 */
    int idx  = code & 0x3F; /* 하위 6비트 */

    if (type == J_CHO) {
        /* 초성 입력 */
        if (g_ime.cho < 0) {
            /* 새로운 초성 시작 */
            g_ime.cho  = idx;
            g_ime.jung = -1;
            g_ime.jong = -1;
            return 0;
        } else if (g_ime.jung < 0) {
            /* 초성만 있을 때 새 초성 → 이전 초성 자모로 출력 */
            uint32_t prev = (uint32_t)(0x3131 + g_ime.cho); /* 자음 단독 코드포인트 근사 */
            g_ime.cho  = idx;
            g_ime.jung = -1;
            g_ime.jong = -1;
            return prev;
        } else if (g_ime.jong < 0) {
            /* 초성+중성 → 종성 후보로 시도 */
            int8_t jc = cho_to_jong[idx];
            if (jc >= 0) {
                g_ime.jong = jc;
                return 0;
            } else {
                /* 종성 불가 → 이전 글자 확정 후 새 초성 */
                uint32_t prev = make_codepoint(g_ime.cho, g_ime.jung, -1);
                g_ime.cho  = idx;
                g_ime.jung = -1;
                g_ime.jong = -1;
                return prev;
            }
        } else {
            /* 종성 있음 → 이전 글자 확정 후 새 초성 */
            uint32_t prev = make_codepoint(g_ime.cho, g_ime.jung, g_ime.jong);
            g_ime.cho  = idx;
            g_ime.jung = -1;
            g_ime.jong = -1;
            return prev;
        }
    } else { /* J_JUNG */
        /* 중성 입력 */
        if (g_ime.cho < 0) {
            /* 초성 없이 중성 → 모음 단독 출력 */
            uint32_t cp = (uint32_t)(0x3161 + idx); /* 모음 단독 코드포인트 근사 */
            return cp;
        } else if (g_ime.jung < 0) {
            /* 초성 + 중성 결합 */
            g_ime.jung = idx;
            return 0;
        } else if (g_ime.jong >= 0) {
            /* 종성 있을 때 새 중성 → 종성을 다음 글자 초성으로 분리 */
            /* 현재 글자는 종성 없이 확정, 종성은 새 초성 */
            int saved_jong = g_ime.jong;
            uint32_t prev  = make_codepoint(g_ime.cho, g_ime.jung, -1);

            /* 종성 → 초성 변환 (역방향) */
            int new_cho = -1;
            for (int i = 0; i < 19; i++) {
                if (cho_to_jong[i] == saved_jong) { new_cho = i; break; }
            }
            if (new_cho < 0) new_cho = 0;
            g_ime.cho  = new_cho;
            g_ime.jung = idx;
            g_ime.jong = -1;
            return prev;
        } else {
            /* 초성+중성 이미 있고 종성 없음 → 새 중성으로 교체? 불가. 확정 */
            uint32_t prev = make_codepoint(g_ime.cho, g_ime.jung, -1);
            g_ime.cho  = -1;
            g_ime.jung = idx; /* 초성 없이 중성 */
            g_ime.jong = -1;
            return prev;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * UTF-8 인코딩
 * ───────────────────────────────────────────────────────────────────────────*/
int unicode_to_utf8(uint32_t cp, char *buf) {
    if (!buf) return 0;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        buf[1] = '\0';
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        buf[2] = '\0';
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = '\0';
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6)  & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        buf[4] = '\0';
        return 4;
    }
}
