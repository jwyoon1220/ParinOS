#include "keyboard.h"
#include "vga.h"
#include "io.h"
#include "../hangul_ime.h"

static uint8_t key_states[128] = {0};
extern void shell_input(char c); /* shell.c 또는 kernel.c에 구현할 함수 */

/* 한영키 PS/2 스캔코드 (일부 키보드에서 0x72 or 확장 0xF1) */
#define SCANCODE_HANGUL_KEY 0x72   /* Korean keyboard: 한/영 전환 */
#define SCANCODE_F12        0x58   /* 대체 한/영 토글 (F12) */
#define SCANCODE_ALT        0x38   /* 우측 Alt(한/영)가 확장키 파싱 전엔 좌측 Alt(0x38)로 잡힘 */

void init_keyboard() {
    for (int i = 0; i < 128; i++) key_states[i] = 0;
    hangul_ime_init();
}

void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        /* 키를 뗐을 때 (Break Code) */
        key_states[scancode - 0x80] = 0;
    } else {
        /* 키를 눌렀을 때 (Make Code) */
        key_states[scancode] = 1;

        /* Shift 상태 확인 (왼쪽 0x2A, 오른쪽 0x36) */
        int shift = isKeyPressed(0x2A) || isKeyPressed(0x36);

        /* Shift 키 자체는 글자 출력 없음 */
        if (scancode == 0x2A || scancode == 0x36) {
            outb(0x20, 0x20);
            return;
        }

        /* 한/영 전환 키 */
        if (scancode == SCANCODE_HANGUL_KEY || scancode == SCANCODE_F12 || scancode == SCANCODE_ALT) {
            hangul_ime_toggle();
            outb(0x20, 0x20);
            return;
        }

        /* 방향키 처리 (Left=0x4B, Right=0x4D) */
        if (scancode == 0x4B || scancode == 0x4D) {
            uint32_t flush = hangul_ime_flush();
            if (flush != 0) {
                char utf8[5];
                int  n = unicode_to_utf8(flush, utf8);
                for (int i = 0; i < n; i++) shell_input(utf8[i]);
            }
            if (scancode == 0x4B) shell_input(17);
            if (scancode == 0x4D) shell_input(18);
            outb(0x20, 0x20);
            return;
        }

        /* 한글 모드 처리 */
        if (hangul_ime_is_korean()) {
            /* 백스페이스 */
            if (scancode == 0x0E) {
                uint32_t flush = hangul_ime_flush();
                if (flush == 0) {
                    shell_input('\b');
                }
                outb(0x20, 0x20);
                return;
            }
            /* 엔터 — 조합 확정 후 개행 */
            if (scancode == 0x1C) {
                uint32_t flush = hangul_ime_flush();
                if (flush != 0) {
                    char utf8[5];
                    int  n = unicode_to_utf8(flush, utf8);
                    for (int i = 0; i < n; i++) shell_input(utf8[i]);
                }
                shell_input('\n');
                outb(0x20, 0x20);
                return;
            }
            /* 스페이스 — 조합 확정 후 공백 */
            if (scancode == 0x39) {
                uint32_t flush = hangul_ime_flush();
                if (flush != 0) {
                    char utf8[5];
                    int  n = unicode_to_utf8(flush, utf8);
                    for (int i = 0; i < n; i++) shell_input(utf8[i]);
                }
                shell_input(' ');
                outb(0x20, 0x20);
                return;
            }

            uint32_t cp = hangul_ime_input(scancode, shift);

            if (cp == 0xFFFFFFFF) {
                /* IME 비활성 상태 (이론상 미발생) */
                char key = shift ? kbd_us_shift[scancode] : kbd_us[scancode];
                if (key) shell_input(key);
            } else if (cp != 0) {
                /* 확정된 코드포인트 */
                char utf8[5];
                int  n = unicode_to_utf8(cp, utf8);
                for (int i = 0; i < n; i++) shell_input(utf8[i]);
            }
            /* cp == 0: 아직 조합 중 */
        } else {
            /* 영문 모드 */
            char key = shift ? kbd_us_shift[scancode] : kbd_us[scancode];
            if (key != 0) {
                shell_input(key);
            }
        }
    }

    outb(0x20, 0x20);
}

int isKeyPressed(uint8_t scancode) {
    if (scancode < 128) return key_states[scancode];
    return 0;
}