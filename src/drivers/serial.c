#include "serial.h"
#include "../io.h"
#include <stdarg.h>
#include <stdint.h>

#include "../vga.h"

// --- 내부 도우미 함수 (시리얼 전용) ---

static void serial_putc(char c) {
    // LSR(Line Status Register)의 5번 비트(THRE)가 1이 될 때까지 대기
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static void serial_puts(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) serial_putc(s[i]);
}

static void serial_put_dec(uint32_t n) {
    if (n == 0) { serial_putc('0'); return; }
    char buf[11]; int i = 0;
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

// 🌟 수정한 부분: 0x를 출력하지 않고 숫자만 출력하도록 변경
static void serial_put_hex(uint32_t n) {
    if (n == 0) { serial_putc('0'); return; }
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (n >> i) & 0xF;
        if (nibble > 0 || started) {
            serial_putc(nibble < 10 ? (nibble + '0') : (nibble - 10 + 'A'));
            started = 1;
        }
    }
}

// --- 공개 함수 ---

int init_serial() {
    outb(COM1 + 1, 0x00);    // 인터럽트 비활성화
    outb(COM1 + 3, 0x80);    // DLAB 활성화
    outb(COM1 + 0, 0x03);    // 38400 baud (115200 / 3)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8비트, 패리티 없음, 1 정지 비트
    outb(COM1 + 2, 0xC7);    // FIFO 설정
    outb(COM1 + 4, 0x0B);    // IRQs 활성화

    // 루프백 테스트
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0xAE);
    if(inb(COM1 + 0) != 0xAE) return 1;

    outb(COM1 + 4, 0x0F);    // 정상 모드로 복구

    return 0;
}

void kprintf_serial(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i + 1] != '\0') {
            i++;
            switch (format[i]) {
                case 'd':
                    serial_put_dec(va_arg(args, uint32_t));
                    break;
                case 'x':
                case 'X':
                    // 🌟 수정한 부분: 서식 처리 시점에 0x를 한 번만 출력
                    serial_puts("0x");
                    serial_put_hex(va_arg(args, uint32_t));
                    break;
                case 's': {
                    char* s = va_arg(args, char*);
                    if (s) serial_puts(s);
                    else serial_puts("(null)");
                    break;
                }
                case 'c':
                    serial_putc((char)va_arg(args, int));
                    break;
                case '%':
                    serial_putc('%');
                    break;
                default:
                    serial_putc('%');
                    serial_putc(format[i]);
                    break;
            }
        } else {
            // \n 제어 문자를 만나면 \r(Carriage Return)을 추가하여 줄바꿈 정렬
            if (format[i] == '\n') serial_putc('\r');
            serial_putc(format[i]);
        }
    }
    va_end(args);
}
void write_serial(char c) {
    // LSR(Line Status Register)의 5번 비트(THRE)가 1이 될 때까지 대기
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, c);
}
