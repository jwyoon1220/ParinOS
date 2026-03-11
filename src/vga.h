#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stdarg.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((char*)0xB8000)

extern int cursor_x;
extern int cursor_y;
extern uint8_t default_color;

// 화면을 깨끗하게 지우는 함수
void vga_clear();

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

#endif