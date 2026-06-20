#include "vesa.h"
#include "../mem/vmm.h"
#include "../mem/mem.h"
#include <stdint.h>

/* ─── 드라이버 내부 상태 ─────────────────────────────────────────────────── */

static int      g_vesa_active  = 0;   /* 출력 활성화 여부 */
static uint8_t *g_fb           = NULL; /* 프레임버퍼 가상(또는 물리) 주소 */
static uint32_t g_fb_phys      = 0;   /* 프레임버퍼 물리 주소 */
static uint32_t g_width        = 0;
static uint32_t g_height       = 0;
static uint32_t g_pitch        = 0;
static uint8_t  g_bpp          = 0;
static uint8_t  g_red_pos      = 16;
static uint8_t  g_green_pos    = 8;
static uint8_t  g_blue_pos     = 0;
static uint8_t  g_bytes_pp     = 3;   /* 픽셀당 바이트 수 */

/* ─── vesa_init ──────────────────────────────────────────────────────────── */

/*
 * 페이징 활성화 이전에 호출합니다.
 * VBE 모드 정보(물리 0x9000)를 읽어 내부 변수에 저장합니다.
 * 프레임버퍼는 아직 매핑하지 않으므로 출력은 비활성 상태입니다.
 */
void vesa_init(void) {
    const VBEModeInfo *info = (const VBEModeInfo *)VESA_INFO_PADDR;

    if (info->framebuffer == 0) {
        /* boot.asm 이 VESA 모드 설정에 실패했거나 지원 안 됨 */
        g_vesa_active = 0;
        return;
    }

    g_fb_phys   = info->framebuffer;
    g_width     = info->width;
    g_height    = info->height;
    g_pitch     = info->pitch;
    g_bpp       = info->bpp;
    g_bytes_pp  = (uint8_t)((g_bpp + 7u) / 8u);
    g_red_pos   = info->red_pos;
    g_green_pos = info->green_pos;
    g_blue_pos  = info->blue_pos;

    /* 출력은 vesa_map_fb() 이후에 활성화 */
    g_vesa_active = 0;
}

/* ─── vesa_map_fb ────────────────────────────────────────────────────────── */

/*
 * init_vmm() 호출 이후에 사용합니다.
 * 프레임버퍼 물리 영역을 동일한 가상 주소로 identity 매핑하고
 * VESA 출력을 활성화합니다.
 */
void vesa_map_fb(void) {
    if (g_fb_phys == 0) return;

    uint32_t fb_size  = (uint32_t)g_pitch * g_height;
    uint32_t pages    = (fb_size + (PAGE_SIZE - 1u)) / PAGE_SIZE;
    uint32_t flags    = PAGE_FLAGS_KERNEL | PAGE_CACHEDISABLE;

    /* 프레임버퍼 물리 주소를 같은 가상 주소로 매핑 */
    vmm_map_pages(g_fb_phys, g_fb_phys, pages, flags);

    g_fb          = (uint8_t *)(uintptr_t)g_fb_phys;
    g_vesa_active = 1;
}

/* ─── 공개 쿼리 함수 ──────────────────────────────────────────────────────── */

int      vesa_is_active(void)  { return g_vesa_active; }
uint32_t vesa_get_width(void)  { return g_width;        }
uint32_t vesa_get_height(void) { return g_height;       }
uint32_t vesa_get_pitch(void)  { return g_pitch;        }
uint8_t  vesa_get_bpp(void)    { return g_bpp;          }

/* ─── 픽셀 출력 ───────────────────────────────────────────────────────────── */

void vesa_put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= g_width || y >= g_height) return;

    uint8_t *pixel = g_fb + y * g_pitch + x * g_bytes_pp;

    if (g_bpp == 32 || g_bpp == 24) {
        pixel[g_red_pos   >> 3] = r;
        pixel[g_green_pos >> 3] = g;
        pixel[g_blue_pos  >> 3] = b;
        if (g_bpp == 32) pixel[3] = 0xFF;
    } else if (g_bpp == 16) {
        uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        *(uint16_t *)pixel = color;
    }
}

/* ─── 화면 지우기 ─────────────────────────────────────────────────────────── */

void vesa_clear(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_vesa_active) return;

    /* 검정 배경은 memset 으로 빠르게 처리 */
    if (r == 0 && g == 0 && b == 0) {
        memset(g_fb, 0, (uint32_t)g_pitch * g_height);
        return;
    }

    for (uint32_t y = 0; y < g_height; y++) {
        uint8_t *row = g_fb + y * g_pitch;
        for (uint32_t x = 0; x < g_width; x++) {
            uint8_t *pixel = row + x * g_bytes_pp;
            if (g_bpp == 32 || g_bpp == 24) {
                pixel[g_red_pos   >> 3] = r;
                pixel[g_green_pos >> 3] = g;
                pixel[g_blue_pos  >> 3] = b;
                if (g_bpp == 32) pixel[3] = 0xFF;
            } else if (g_bpp == 16) {
                uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                *(uint16_t *)pixel = color;
            }
        }
    }
}

/* ─── 사각형 채우기 (인라인 어셈블리 최적화) ──────────────────────────────
 * 32bpp: rep stosd  — 픽셀당 4바이트를 한 번에 채움
 * 16bpp: rep stosw  — 픽셀당 2바이트
 * 24bpp: C fallback  — 3바이트 정렬 불가로 stosd 부적합
 */
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b) {
    if (!g_vesa_active || w == 0 || h == 0) return;

    /* 화면 경계 클리핑 */
    if (x >= g_width || y >= g_height) return;
    if (x + w > g_width)  w = g_width  - x;
    if (y + h > g_height) h = g_height - y;

    if (g_bpp == 32) {
        uint32_t pixel_val = (0xFFu << 24)
                           | ((uint32_t)r << g_red_pos)
                           | ((uint32_t)g << g_green_pos)
                           | ((uint32_t)b << g_blue_pos);

        for (uint32_t row = y; row < y + h; row++) {
            uint32_t *dst = (uint32_t *)(g_fb + row * g_pitch + x * 4u);
            uint32_t  cnt = w;
            __asm__ __volatile__ (
                "rep stosl"
                : "+D"(dst), "+c"(cnt)
                : "a"(pixel_val)
                : "memory"
            );
        }
    } else if (g_bpp == 16) {
        uint16_t pixel_val = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));

        for (uint32_t row = y; row < y + h; row++) {
            uint16_t *dst = (uint16_t *)(g_fb + row * g_pitch + x * 2u);
            uint32_t  cnt = w;
            __asm__ __volatile__ (
                "rep stosw"
                : "+D"(dst), "+c"(cnt)
                : "a"((uint32_t)pixel_val)
                : "memory"
            );
        }
    } else {
        /* 24bpp fallback */
        for (uint32_t row = y; row < y + h; row++) {
            uint8_t *p = g_fb + row * g_pitch + x * 3u;
            for (uint32_t col = 0; col < w; col++, p += 3) {
                p[g_red_pos   >> 3] = r;
                p[g_green_pos >> 3] = g;
                p[g_blue_pos  >> 3] = b;
            }
        }
    }
}

/* ─── 스크롤 (SSE2 128비트 복사) ─────────────────────────────────────────── */

void vesa_scroll_up(uint32_t fh) {
    if (!g_vesa_active || fh == 0 || fh >= g_height) return;

    uint32_t copy_bytes  = g_pitch * (g_height - fh);
    uint32_t clear_bytes = g_pitch * fh;
    uint8_t *src         = g_fb + g_pitch * fh;
    uint8_t *dst         = g_fb;

    /* SSE2 movdqu: 16바이트씩 복사 (비정렬 허용) */
    uint32_t qwords = copy_bytes / 16;
    uint32_t remain = copy_bytes % 16;

    __asm__ __volatile__ (
        "test %2, %2           \n"
        "jz   1f               \n"
        "0:                    \n"
        "movdqu (%1), %%xmm0   \n"
        "movdqu %%xmm0, (%0)   \n"
        "addl $16, %0          \n"
        "addl $16, %1          \n"
        "decl %2               \n"
        "jnz 0b                \n"
        "1:                    \n"
        : "+r"(dst), "+r"(src), "+r"(qwords)
        :
        : "xmm0", "memory"
    );

    /* 나머지 바이트 처리 */
    for (uint32_t i = 0; i < remain; i++) dst[i] = src[i];

    /* 하단 클리어 */
    memset(g_fb + copy_bytes, 0, clear_bytes);
}
