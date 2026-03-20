#ifndef VESA_H
#define VESA_H

#include <stdint.h>

/*
 * VESA/VBE 선형 프레임버퍼 드라이버
 *
 * boot.asm 이 INT 10h/AX=4F01h 로 획득한 VBE 모드 정보를
 * 물리 주소 0x9000 에 저장합니다.
 * vesa_init()  – 해당 구조체를 읽어 드라이버 상태를 초기화합니다.
 * vesa_map_fb() – VMM 페이징 활성화 후 프레임버퍼를 가상 주소 공간에 매핑합니다.
 */

/* boot.asm 이 VBE 모드 정보를 저장하는 물리 주소 */
#define VESA_INFO_PADDR  0x9000u

/* boot.asm 이 BIOS 8×8 폰트 포인터(세그먼트:오프셋)를 저장하는 물리 주소 */
#define BIOS_FONT_PADDR  0x9100u

/* VBE 모드 정보 블록 (VESA BIOS Extensions 2.0 기준) */
typedef struct __attribute__((packed)) {
    uint16_t attributes;       /* 0x00 모드 속성 비트 */
    uint8_t  window_a;         /* 0x02 */
    uint8_t  window_b;         /* 0x03 */
    uint16_t granularity;      /* 0x04 */
    uint16_t window_size;      /* 0x06 */
    uint16_t segment_a;        /* 0x08 */
    uint16_t segment_b;        /* 0x0A */
    uint32_t win_func_ptr;     /* 0x0C */
    uint16_t pitch;            /* 0x10 스캔 라인당 바이트 수 */
    uint16_t width;            /* 0x12 가로 해상도 */
    uint16_t height;           /* 0x14 세로 해상도 */
    uint8_t  w_char;           /* 0x16 */
    uint8_t  y_char;           /* 0x17 */
    uint8_t  planes;           /* 0x18 */
    uint8_t  bpp;              /* 0x19 픽셀당 비트 수 */
    uint8_t  banks;            /* 0x1A */
    uint8_t  memory_model;     /* 0x1B */
    uint8_t  bank_size;        /* 0x1C */
    uint8_t  image_pages;      /* 0x1D */
    uint8_t  reserved0;        /* 0x1E */
    uint8_t  red_mask;         /* 0x1F */
    uint8_t  red_pos;          /* 0x20 빨간색 비트 오프셋 */
    uint8_t  green_mask;       /* 0x21 */
    uint8_t  green_pos;        /* 0x22 초록색 비트 오프셋 */
    uint8_t  blue_mask;        /* 0x23 */
    uint8_t  blue_pos;         /* 0x24 파란색 비트 오프셋 */
    uint8_t  rsv_mask;         /* 0x25 */
    uint8_t  rsv_pos;          /* 0x26 */
    uint8_t  directcolor;      /* 0x27 */
    uint32_t framebuffer;      /* 0x28 선형 프레임버퍼 물리 주소 */
} VBEModeInfo;

/* ─── VESA 드라이버 API ─────────────────────────────────────────────────── */

/* VBE 모드 정보 저장 (페이징 활성화 이전에 호출) */
void vesa_init(void);

/*
 * VMM 페이징 활성화 후 프레임버퍼를 가상 주소 공간에 identity 매핑하고
 * VESA 출력을 활성화합니다. init_vmm() 호출 이후에 사용해야 합니다.
 */
void vesa_map_fb(void);

/* VESA 출력 활성화 여부 */
int vesa_is_active(void);

/* 해상도 / 색상 정보 */
uint32_t vesa_get_width(void);
uint32_t vesa_get_height(void);
uint32_t vesa_get_pitch(void);
uint8_t  vesa_get_bpp(void);

/* 픽셀 출력: (x, y) 위치에 RGB 색상 설정 */
void vesa_put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);

/* 화면 전체를 단색으로 지우기 */
void vesa_clear(uint8_t r, uint8_t g, uint8_t b);

/* 사각형 채우기 */
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b);

/* 화면을 위로 fh 픽셀 스크롤 (하단은 검정으로 채움) */
void vesa_scroll_up(uint32_t fh);

#endif /* VESA_H */
