#include "vga.h"
#include <stdarg.h>
#include "io.h"
#include "drivers/serial.h"
#include "std/string.h"
#include "drivers/vesa.h"
#include "font/font.h"

int cursor_x = 0;
int cursor_y = 0;
uint8_t default_color = 0x0F;

/* ─── VESA 텍스트 커서 (픽셀 단위) ─────────────────────────────────────── */
static int vesa_cx = 0;
static int vesa_cy = 0;

/* VGA 16색 팔레트 → RGB 변환 표 */
static const uint8_t vga_rgb[16][3] = {
    {  0,   0,   0},   /* 0: Black        */
    {  0,   0, 170},   /* 1: Blue         */
    {  0, 170,   0},   /* 2: Green        */
    {  0, 170, 170},   /* 3: Cyan         */
    {170,   0,   0},   /* 4: Red          */
    {170,   0, 170},   /* 5: Magenta      */
    {170,  85,   0},   /* 6: Brown        */
    {170, 170, 170},   /* 7: Light Gray   */
    { 85,  85,  85},   /* 8: Dark Gray    */
    { 85,  85, 255},   /* 9: Light Blue   */
    { 85, 255,  85},   /* A: Light Green  */
    { 85, 255, 255},   /* B: Light Cyan   */
    {255,  85,  85},   /* C: Light Red    */
    {255,  85, 255},   /* D: Light Magenta*/
    {255, 255,  85},   /* E: Yellow       */
    {255, 255, 255},   /* F: White        */
};

// ─────────────────────────────────────────────────────────────────────────────
// 하드웨어 커서 및 화면 관리
// ─────────────────────────────────────────────────────────────────────────────
void update_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

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

void vga_clear() {
    if (vesa_is_active() && font_is_ready()) {
        vesa_clear(0, 0, 0);
        vesa_cx = 0;
        vesa_cy = 0;
        kprintf_serial("[VESA] Screen Cleared\n");
        return;
    }
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

void vga_set_color(uint8_t color) {
    default_color = color;
}

// ─────────────────────────────────────────────────────────────────────────────
// 단일 문자 및 문자열 출력 로직
// ─────────────────────────────────────────────────────────────────────────────
void lkputchar(char c) {
    if (vesa_is_active() && font_is_ready()) {
        /* ── VESA 픽셀 폰트 출력 경로 ── */
        int fw = font_get_width();
        int fh = font_get_height();
        int sw = (int)vesa_get_width();
        int sh = (int)vesa_get_height();

        uint8_t fg_idx = default_color & 0x0Fu;
        uint8_t bg_idx = (default_color >> 4) & 0x0Fu;
        uint8_t r_fg = vga_rgb[fg_idx][0];
        uint8_t g_fg = vga_rgb[fg_idx][1];
        uint8_t b_fg = vga_rgb[fg_idx][2];
        uint8_t r_bg = vga_rgb[bg_idx][0];
        uint8_t g_bg = vga_rgb[bg_idx][1];
        uint8_t b_bg = vga_rgb[bg_idx][2];

        if (c == '\n') {
            vesa_cx = 0;
            vesa_cy += fh;
        } else if (c == '\r') {
            vesa_cx = 0;
        } else if (c == '\b') {
            if (vesa_cx >= fw) {
                vesa_cx -= fw;
                vesa_fill_rect((uint32_t)vesa_cx, (uint32_t)vesa_cy,
                               (uint32_t)fw, (uint32_t)fh,
                               r_bg, g_bg, b_bg);
            }
            write_serial('\b'); write_serial(' '); write_serial('\b');
            return;
        } else {
            font_draw_char(vesa_cx, vesa_cy, c,
                           r_fg, g_fg, b_fg,
                           r_bg, g_bg, b_bg);
            vesa_cx += fw;
            if (vesa_cx + fw > sw) {
                vesa_cx = 0;
                vesa_cy += fh;
            }
        }

        /* 스크롤 */
        if (vesa_cy + fh > sh) {
            vesa_scroll_up((uint32_t)fh);
            vesa_cy -= fh;
        }

        if (c == '\n') write_serial('\r');
        write_serial(c);
        return;
    }

    /* ── 기존 VGA 텍스트 모드 출력 경로 ── */
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

        // 시리얼 콘솔에서도 백스페이스 처리
        write_serial('\b'); write_serial(' '); write_serial('\b');
        return; // 백스페이스는 여기서 종료
    } else {
        int offset = (cursor_y * 80 + cursor_x) * 2;
        video_memory[offset] = c;
        video_memory[offset + 1] = default_color;
        cursor_x++;
    }

    if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
    if (cursor_y >= 25) vga_scroll();
    update_cursor(cursor_x, cursor_y);

    if (c == '\n') write_serial('\r');
    write_serial(c);
}

void kprint(const char* string) {
    for (int i = 0; string[i] != '\0'; i++) {
        lkputchar(string[i]);
    }
}

void kprintln(const char* string) {
    kprint(string);
    lkputchar('\n');
}

// ─────────────────────────────────────────────────────────────────────────────
// 범용 숫자 출력 헬퍼 (진법, 부호, 패딩 처리)
// ─────────────────────────────────────────────────────────────────────────────
static void print_number(uint32_t value, int base, int is_signed, int width, char pad_char, int uppercase) {
    char buffer[32]; // 2진수 32비트 처리를 위한 충분한 크기
    int ptr = 0;
    uint32_t uval = value;
    int is_negative = 0;

    // 부호 처리 (10진수 음수일 경우에만)
    if (is_signed && (int32_t)value < 0) {
        is_negative = 1;
        uval = (uint32_t)(-(int32_t)value);
    }

    // 숫자를 역순으로 버퍼에 저장
    if (uval == 0) {
        buffer[ptr++] = '0';
    } else {
        while (uval > 0) {
            int rem = uval % base;
            buffer[ptr++] = (rem < 10) ? ('0' + rem) : ((uppercase ? 'A' : 'a') + rem - 10);
            uval /= base;
        }
    }

    int num_len = ptr + is_negative;

    // 1. 공백 패딩 ('0' 채우기가 아닐 때, 오른쪽에 정렬)
    if (pad_char == ' ') {
        while (num_len < width) {
            lkputchar(' ');
            width--;
        }
    }

    // 2. 부호 출력 (0 패딩보다 앞에 있어야 함. ex: -0005)
    if (is_negative) lkputchar('-');

    // 3. '0' 채우기 패딩
    if (pad_char == '0') {
        while (num_len < width) {
            lkputchar('0');
            width--;
        }
    }

    // 4. 버퍼에 저장된 실제 숫자 출력 (역순)
    while (ptr > 0) {
        lkputchar(buffer[--ptr]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 완전판 kprintf (stdio.h 수준)
// ─────────────────────────────────────────────────────────────────────────────
void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i + 1] != '\0') {
            i++; // '%' 다음 문자로 이동

            char pad_char = ' ';
            int width = 0;

            // 1. 패딩 문자 확인 (0으로 시작하는지)
            if (format[i] == '0') {
                pad_char = '0';
                i++;
            }

            // 2. 출력 너비(Width) 계산
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }

            // 3. 포맷 지정자 처리
            switch (format[i]) {
                case 'd': // 부호 있는 10진수
                case 'i':
                    print_number(va_arg(args, uint32_t), 10, 1, width, pad_char, 0);
                    break;
                case 'u': // 부호 없는 10진수
                    print_number(va_arg(args, uint32_t), 10, 0, width, pad_char, 0);
                    break;
                case 'x': // 16진수 (소문자, 접두사 없음)
                    print_number(va_arg(args, uint32_t), 16, 0, width, pad_char, 0);
                    break;
                case 'X': // 16진수 (대문자, 접두사 없음)
                    print_number(va_arg(args, uint32_t), 16, 0, width, pad_char, 1);
                    break;
                case 'p': // 포인터 주소 (0x 자동 추가 + 8자리 0 채우기)
                    kprint("0x");
                    print_number(va_arg(args, uint32_t), 16, 0, width > 0 ? width : 8, '0', 1);
                    break;
                case 'b': // 2진수 (OS 개발 특화 기능)
                    kprint("0b");
                    print_number(va_arg(args, uint32_t), 2, 0, width, pad_char, 0);
                    break;
                case 'c': // 단일 문자
                    lkputchar((char)va_arg(args, int));
                    break;
                case 's': { // 문자열 (NULL 안전성 추가)
                    char* str = va_arg(args, char*);
                    if (!str) str = "(null)";
                    kprint(str);
                    break;
                }
                case '%': // '%' 자체 출력
                    lkputchar('%');
                    break;
                default: // 해석할 수 없는 포맷은 그대로 출력
                    lkputchar('%');
                    lkputchar(format[i]);
                    break;
            }
        } else {
            // 일반 문자는 그대로 출력
            lkputchar(format[i]);
        }
    }
    va_end(args);
}
// ─────────────────────────────────────────────────────────────────────────────
// Linux 스타일 컬러 로깅 함수
// 각 로그 레벨마다 컬러 태그를 출력하고, 메시지는 기본 색상으로 출력합니다.
// ─────────────────────────────────────────────────────────────────────────────

// kprintf와 동일하지만 va_list를 받는 내부 헬퍼
static void kvprintf(const char* format, va_list args) {
    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i + 1] != '\0') {
            i++;
            char pad_char = ' ';
            int width = 0;
            if (format[i] == '0') { pad_char = '0'; i++; }
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }
            switch (format[i]) {
                case 'd': case 'i':
                    print_number(va_arg(args, uint32_t), 10, 1, width, pad_char, 0); break;
                case 'u':
                    print_number(va_arg(args, uint32_t), 10, 0, width, pad_char, 0); break;
                case 'x':
                    print_number(va_arg(args, uint32_t), 16, 0, width, pad_char, 0); break;
                case 'X':
                    print_number(va_arg(args, uint32_t), 16, 0, width, pad_char, 1); break;
                case 'p':
                    kprint("0x");
                    print_number(va_arg(args, uint32_t), 16, 0, width > 0 ? width : 8, '0', 1); break;
                case 'c':
                    lkputchar((char)va_arg(args, int)); break;
                case 's': {
                    char* s = va_arg(args, char*);
                    if (!s) s = "(null)";
                    kprint(s);
                    break;
                }
                case '%': lkputchar('%'); break;
                default:  lkputchar('%'); lkputchar(format[i]); break;
            }
        } else {
            lkputchar(format[i]);
        }
    }
}

static void klog_emit(uint8_t label_color, const char* label,
                      const char* fmt, va_list args) {
    uint8_t saved = default_color;
    vga_set_color(label_color);
    kprint(label);
    vga_set_color(VGA_COLOR_DEFAULT);
    kvprintf(fmt, args);
    vga_set_color(saved);
}

void klog_info(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    klog_emit(VGA_COLOR_INFO,  "[ INFO ] ", fmt, args);
    va_end(args);
}

void klog_warn(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    klog_emit(VGA_COLOR_WARN,  "[ WARN ] ", fmt, args);
    va_end(args);
}

void klog_error(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    klog_emit(VGA_COLOR_ERROR, "[ERROR ] ", fmt, args);
    va_end(args);
}

void klog_debug(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    klog_emit(VGA_COLOR_DEBUG, "[DEBUG ] ", fmt, args);
    va_end(args);
}

void klog_ok(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    klog_emit(VGA_COLOR_SUCCESS, "[ OK  ] ", fmt, args);
    va_end(args);
}
