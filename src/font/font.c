/*
 * font.c – 커널 폰트 렌더링 구현
 *
 * BIOS 8×8 비트맵 폰트와 stb_truetype TrueType 폰트 두 가지를 지원합니다.
 * FPU 는 fpu_init() 이 호출된 이후에 사용 가능합니다.
 */

#include "font.h"
#include "../drivers/vesa.h"
#include "../std/malloc.h"
#include "../mem/mem.h"
#include "../std/string.h"
#include "../vga.h"
#include "../fs/fs.h"
#include "../fs/fat.h"
#include <stdint.h>

/* ====================================================================
 * stb_truetype 커널 어댑터
 * ==================================================================== */

/* 메모리 관리 */
#define STBTT_malloc(sz, u)   ((void)(u), kmalloc((uint32_t)(sz)))
#define STBTT_free(p, u)      ((void)(u), kfree(p))
#define STBTT_memcpy          memcpy
#define STBTT_memset          memset
#define STBTT_strlen          strlen
#define STBTT_assert(x)       ((void)0)

/* 커널 자체 수학 함수 구현 (libm 불필요) */
static inline float kf_fabs(float x)  { return x < 0.0f ? -x : x; }

static inline float kf_floor(float x) {
    int i = (int)x;
    return (float)(i - (x < (float)i ? 1 : 0));
}

static inline float kf_ceil(float x) {
    int i = (int)x;
    return (float)(i + (x > (float)i ? 1 : 0));
}

static float kf_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = (x > 1.0f) ? x * 0.5f : 1.0f;
    int i;
    for (i = 0; i < 24; i++) {
        float prev = g;
        g = (g + x / g) * 0.5f;
        if (kf_fabs(g - prev) < 1e-7f) break;
    }
    return g;
}

static inline float kf_fmod(float x, float y) {
    if (y == 0.0f) return 0.0f;
    int q = (int)(x / y);
    return x - (float)q * y;
}

/* SDF 경로에서만 사용됨 – 스텁 (일반 래스터라이저에서는 호출되지 않음) */
static float kf_pow(float x, float y)  { (void)x; (void)y; return 0.0f; }
static float kf_cos(float x)           { (void)x; return 1.0f; }
static float kf_acos(float x)          { (void)x; return 0.0f; }

/* stb_truetype 수학 함수 재정의 */
#define STBTT_ifloor(x)   ((int)kf_floor(x))
#define STBTT_iceil(x)    ((int)kf_ceil(x))
#define STBTT_sqrt(x)     kf_sqrt(x)
#define STBTT_pow(x,y)    kf_pow((x),(y))
#define STBTT_fabs(x)     kf_fabs(x)
#define STBTT_fmod(x,y)   kf_fmod((x),(y))
#define STBTT_cos(x)      kf_cos(x)
#define STBTT_acos(x)     kf_acos(x)

/* stb_truetype 구현 (경고 억제) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb/stb_truetype.h"
#pragma GCC diagnostic pop

/* ====================================================================
 * 폰트 상태
 * ==================================================================== */

#define BIOS_FONT_PADDR  0x9100u

typedef enum { FONT_NONE, FONT_BIOS8x8, FONT_TTF } FontType;

static FontType      g_font_type  = FONT_NONE;
static uint8_t      *g_bios_font  = NULL;     /* BIOS 8×8 폰트 포인터 */
static uint8_t      *g_ttf_buf    = NULL;     /* TTF 파일 버퍼 */
static stbtt_fontinfo g_ttf_info;             /* stb_truetype 폰트 정보 */
static float         g_ttf_scale  = 1.0f;    /* 픽셀 크기 스케일 */
static int           g_ttf_ascent = 0;        /* 베이스라인 오프셋 */
static int           g_font_w     = 8;
static int           g_font_h     = 8;

/* ====================================================================
 * BIOS 8×8 폰트 초기화
 * ==================================================================== */

void font_init(void) {
    /* boot.asm 이 0x9100 에 저장한 세그먼트:오프셋 읽기 */
    volatile uint16_t font_off = *(volatile uint16_t *)BIOS_FONT_PADDR;
    volatile uint16_t font_seg = *(volatile uint16_t *)(BIOS_FONT_PADDR + 2u);

    uint32_t flat = (uint32_t)font_seg * 16u + (uint32_t)font_off;
    if (flat == 0) return;

    g_bios_font = (uint8_t *)flat;
    g_font_type = FONT_BIOS8x8;
    g_font_w    = 8;
    g_font_h    = 8;
}

/* ====================================================================
 * TrueType 폰트 로드
 * ==================================================================== */

int font_load_ttf(const char *path, int size) {
    if (!path || size <= 0) return -1;

    /* 기존 TTF 버퍼 해제 */
    if (g_ttf_buf) {
        kfree(g_ttf_buf);
        g_ttf_buf = NULL;
    }

    /* 파일 열기 */
    File f;
    if (fat_file_open(&f, path, FAT_READ) != FAT_ERR_NONE) {
        klog_error("font: cannot open '%s'\n", path);
        return -2;
    }

    int file_size = (int)f.size;
    if (file_size <= 0) {
        fat_file_close(&f);
        return -3;
    }

    g_ttf_buf = (uint8_t *)kmalloc((uint32_t)file_size);
    if (!g_ttf_buf) {
        fat_file_close(&f);
        return -4;
    }

    int bytes_read = 0;
    if (fat_file_read(&f, g_ttf_buf, file_size, &bytes_read) != FAT_ERR_NONE
        || bytes_read != file_size) {
        kfree(g_ttf_buf);
        g_ttf_buf = NULL;
        fat_file_close(&f);
        return -5;
    }
    fat_file_close(&f);

    /* stb_truetype 폰트 초기화 */
    int offset = stbtt_GetFontOffsetForIndex(g_ttf_buf, 0);
    if (!stbtt_InitFont(&g_ttf_info, g_ttf_buf, offset)) {
        kfree(g_ttf_buf);
        g_ttf_buf = NULL;
        return -6;
    }

    g_ttf_scale = stbtt_ScaleForPixelHeight(&g_ttf_info, (float)size);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&g_ttf_info, &ascent, &descent, &line_gap);
    g_ttf_ascent = (int)((float)ascent * g_ttf_scale);

    /* 글자 너비: 'M' 기준 */
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_ttf_info, 'M', &adv, &lsb);
    g_font_w = (int)((float)adv * g_ttf_scale);
    if (g_font_w < 1) g_font_w = size / 2;

    g_font_h    = size;
    g_font_type = FONT_TTF;

    klog_ok("font: TTF loaded '%s' (%d px)\n", path, size);
    return 0;
}

/* ====================================================================
 * 공개 쿼리
 * ==================================================================== */

int font_is_ready(void)   { return g_font_type != FONT_NONE; }
int font_get_width(void)  { return g_font_w; }
int font_get_height(void) { return g_font_h; }

/* ====================================================================
 * 문자 렌더링
 * ==================================================================== */

void font_draw_char(int px, int py, char c,
                    uint8_t fr, uint8_t fg, uint8_t fb,
                    uint8_t br, uint8_t bg_c, uint8_t bb) {
    if (!vesa_is_active()) return;

    if (g_font_type == FONT_BIOS8x8 && g_bios_font) {
        /* 8×8 비트맵 렌더링 */
        const uint8_t *glyph = g_bios_font + ((uint8_t)c) * 8;
        int row, col;
        for (row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (col = 0; col < 8; col++) {
                if (bits & (0x80u >> (unsigned)col)) {
                    vesa_put_pixel((uint32_t)(px + col),
                                   (uint32_t)(py + row),
                                   fr, fg, fb);
                } else {
                    vesa_put_pixel((uint32_t)(px + col),
                                   (uint32_t)(py + row),
                                   br, bg_c, bb);
                }
            }
        }
    } else if (g_font_type == FONT_TTF && g_ttf_buf) {
        /* TrueType 래스터 렌더링 */
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&g_ttf_info,
                                    (int)(unsigned char)c,
                                    g_ttf_scale, g_ttf_scale,
                                    &x0, &y0, &x1, &y1);

        int bw = x1 - x0;
        int bh = y1 - y0;
        if (bw <= 0 || bh <= 0) return;

        uint8_t *bitmap = (uint8_t *)kmalloc((uint32_t)(bw * bh));
        if (!bitmap) return;
        memset(bitmap, 0, (uint32_t)(bw * bh));

        stbtt_MakeCodepointBitmap(&g_ttf_info, bitmap, bw, bh, bw,
                                  g_ttf_scale, g_ttf_scale,
                                  (int)(unsigned char)c);

        int base_y = py + g_ttf_ascent;
        int row, col;
        for (row = 0; row < bh; row++) {
            for (col = 0; col < bw; col++) {
                uint8_t alpha = bitmap[row * bw + col];
                int sx = px + x0 + col;
                int sy = base_y + y0 + row;
                if (sx < 0 || sy < 0) continue;

                /* 알파 블렌딩 */
                uint8_t r2 = (uint8_t)(((uint32_t)fr * alpha
                             + (uint32_t)br * (255u - alpha)) / 255u);
                uint8_t g2 = (uint8_t)(((uint32_t)fg * alpha
                             + (uint32_t)bg_c * (255u - alpha)) / 255u);
                uint8_t b2 = (uint8_t)(((uint32_t)fb * alpha
                             + (uint32_t)bb * (255u - alpha)) / 255u);
                vesa_put_pixel((uint32_t)sx, (uint32_t)sy, r2, g2, b2);
            }
        }
        kfree(bitmap);
    }
}
