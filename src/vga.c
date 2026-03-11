#include "vga.h"
#include <stdarg.h>
#include "io.h"
#include "drivers/serial.h"
#include "std/string.h"

int cursor_x = 0;
int cursor_y = 0;
uint8_t default_color = 0x0F;

// 하드웨어 커서 업데이트
void update_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// 화면 스크롤
static void vga_scroll() {
    char* video_memory = (char*)0xB8000;
    for (int i = 0; i < 80 * (25 - 1); i++) {
        video_memory[i * 2] = video_memory[(i + 80) * 2];
        video_memory[i * 2 + 1] = video_memory[(i + 80) * 2 + 1];
    }
    for (int i = 80 * (25 - 1); i < 80 * 25; i++) {
        video_memory[i * 2] = ' ';
        video_memory[i * 2 + 1] = default_color;
    }
    cursor_y = 25 - 1;
}

// 화면 초기화 및 시리얼 로그
void vga_clear() {
    char* video_memory = (char*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i * 2] = ' ';
        video_memory[i * 2 + 1] = default_color;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor(0, 0);

    kprintf_serial("[VGA] Screen Cleared\n");
}

// 문자 하나 출력 (VGA + Serial)
void lkputchar(char c) {
    // 1. VGA 출력 로직
    char* video_memory = (char*)0xB8000;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
        else if (cursor_y > 0) { cursor_y--; cursor_x = 79; }
        int offset = (cursor_y * 80 + cursor_x) * 2;
        video_memory[offset] = ' ';
        video_memory[offset + 1] = default_color;
    } else {
        int offset = (cursor_y * 80 + cursor_x) * 2;
        video_memory[offset] = c;
        video_memory[offset + 1] = default_color;
        cursor_x++;
    }

    if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
    if (cursor_y >= 25) vga_scroll();
    update_cursor(cursor_x, cursor_y);

    // 2. 시리얼 출력 동기화 (kprintf_serial 내부에서 \r\n 처리를 하므로 단일 문자 전송)
    if (c == '\n') write_serial('\r');
    write_serial(c);
}

// 문자열 출력 (VGA + Serial)
void kprint(const char* string) {
    for (int i = 0; string[i] != '\0'; i++) {
        lkputchar(string[i]); // kputchar 내부에서 이미 양쪽 출력 처리됨
    }
}

// 줄바꿈 포함 출력 (VGA + Serial)
void kprintln(const char* string) {
    kprintf(string);
    lkputchar('\n');
}

// 16진수 출력 (VGA + Serial)
void kprint_hex(uint32_t value) {
    if (value == 0) { lkputchar('0'); return; }
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        if (nibble != 0 || started) {
            char c = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
            lkputchar(c);
            started = 1;
        }
    }
}

// 10진수 출력 (VGA + Serial)
void kprint_dec(uint32_t value) {
    if (value == 0) { lkputchar('0'); return; }
    char s[11]; int i = 0;
    while (value > 0) { s[i++] = '0' + (value % 10); value /= 10; }
    while (i > 0) lkputchar(s[--i]);
}

// vga.c

// 색상 변경을 위한 함수 추가
void vga_set_color(uint8_t color) {
    default_color = color;
}

// 기존 kputchar는 default_color를 사용하므로
// vga_set_color 호출 후 kprintf를 쓰면 색상이 적용됩니다.

// 포맷팅 출력 (VGA + Serial 핵심)
void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i + 1] != '\0') {
            i++;
            switch (format[i]) {
                case 'd': kprint_dec(va_arg(args, uint32_t)); break;
                case 'x':
                case 'X':
                    kprint("0x");
                    kprint_hex(va_arg(args, uint32_t));
                    break;
                case 's': kprint(va_arg(args, char*)); break;
                case 'c': lkputchar((char)va_arg(args, int)); break;
                case '%': lkputchar('%'); break;
                default:  lkputchar('%'); lkputchar(format[i]); break;
            }
        } else {
            lkputchar(format[i]);
        }
    }
    va_end(args);
}
