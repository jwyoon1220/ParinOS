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

/* ─── 사각형 채우기 ───────────────────────────────────────────────────────── */

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t row = y; row < y + h && row < g_height; row++) {
        for (uint32_t col = x; col < x + w && col < g_width; col++) {
            vesa_put_pixel(col, row, r, g, b);
        }
    }
}

/* ─── 스크롤 ──────────────────────────────────────────────────────────────── */

void vesa_scroll_up(uint32_t fh) {
    if (!g_vesa_active || fh == 0 || fh >= g_height) return;

    uint32_t copy_bytes = g_pitch * (g_height - fh);
    memcpy(g_fb, g_fb + g_pitch * fh, copy_bytes);
    memset(g_fb + copy_bytes, 0, g_pitch * fh);
}
