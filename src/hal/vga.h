#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stdarg.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((char*)0xB8000)

// ─────────────────────────────────────────────────────────────────────────────
// VGA 색상 상수 (foreground / background nibble)
// 사용법: vga_set_color(VGA_COLOR(VGA_FG_WHITE, VGA_BG_BLACK))
// ─────────────────────────────────────────────────────────────────────────────
#define VGA_FG_BLACK         0x0
#define VGA_FG_BLUE          0x1
#define VGA_FG_GREEN         0x2
#define VGA_FG_CYAN          0x3
#define VGA_FG_RED           0x4
#define VGA_FG_MAGENTA       0x5
#define VGA_FG_BROWN         0x6
#define VGA_FG_LIGHT_GRAY    0x7
#define VGA_FG_DARK_GRAY     0x8
#define VGA_FG_LIGHT_BLUE    0x9
#define VGA_FG_LIGHT_GREEN   0xA
#define VGA_FG_LIGHT_CYAN    0xB
#define VGA_FG_LIGHT_RED     0xC
#define VGA_FG_LIGHT_MAGENTA 0xD
#define VGA_FG_YELLOW        0xE
#define VGA_FG_WHITE         0xF

#define VGA_BG_BLACK         0x0
#define VGA_BG_BLUE          0x1
#define VGA_BG_GREEN         0x2
#define VGA_BG_CYAN          0x3
#define VGA_BG_RED           0x4
#define VGA_BG_MAGENTA       0x5
#define VGA_BG_BROWN         0x6
#define VGA_BG_LIGHT_GRAY    0x7

// 색상 바이트 생성 매크로: (bg << 4) | fg
#define VGA_COLOR(fg, bg)    (((bg) << 4) | (fg))

// 자주 쓰는 색상 조합
#define VGA_COLOR_DEFAULT    VGA_COLOR(VGA_FG_WHITE,       VGA_BG_BLACK)
#define VGA_COLOR_INFO       VGA_COLOR(VGA_FG_LIGHT_GREEN, VGA_BG_BLACK)
#define VGA_COLOR_WARN       VGA_COLOR(VGA_FG_YELLOW,      VGA_BG_BLACK)
#define VGA_COLOR_ERROR      VGA_COLOR(VGA_FG_LIGHT_RED,   VGA_BG_BLACK)
#define VGA_COLOR_DEBUG      VGA_COLOR(VGA_FG_LIGHT_CYAN,  VGA_BG_BLACK)
#define VGA_COLOR_SUCCESS    VGA_COLOR(VGA_FG_LIGHT_GREEN, VGA_BG_BLACK)

extern int cursor_x;
extern int cursor_y;
extern uint8_t default_color;

// 화면을 깨끗하게 지우는 함수
void vga_clear();
void vga_set_color(uint8_t color);

// 기본 출력 함수들
void lkputchar(char c);
void kprint(const char* string);
void kprintln(const char* string);
void kprint_char(char c);

// 숫자 출력 함수들
void kprint_hex(uint32_t value);
void kprint_dec(uint32_t value);

// 가변 인자를 지원하는 kprintf (ex: kprintf("Value: %d, Hex: %x\n", 10, 255);)
void kprintf(const char* format, ...);

void update_cursor(int x, int y);
void vesa_scroll_up(uint32_t lines);

int vga_get_cx(void);
void vga_set_cx(int cx);
void vga_clear_current_line(void);
void vga_draw_cursor(int show);

// ─────────────────────────────────────────────────────────────────────────────
// Linux 스타일 컬러 로깅 함수
// ─────────────────────────────────────────────────────────────────────────────
void klog_info (const char* fmt, ...);
void klog_warn (const char* fmt, ...);
void klog_error(const char* fmt, ...);
void klog_debug(const char* fmt, ...);
void klog_ok   (const char* fmt, ...);

#endif