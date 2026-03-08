#include "keyboard.h"
#include "vga.h"

#include "io.h"

static uint8_t key_states[128] = {0};
extern void shell_input(char c); // shell.c 또는 kernel.c에 구현할 함수

void init_keyboard() {
    for(int i = 0; i < 128; i++) key_states[i] = 0;
}

void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        // 1. 키를 뗐을 때 (Break Code)
        key_states[scancode - 0x80] = 0;
    } else {
        // 2. 키를 눌렀을 때 (Make Code)
        key_states[scancode] = 1;

        // Shift 상태 확인 (왼쪽 0x2A, 오른쪽 0x36)
        int shift = isKeyPressed(0x2A);

        // 🌟 스캔코드가 Shift 키 자체라면 글자를 출력하지 않고 종료
        if (scancode == 0x2A || scancode == 0x36) {
            outb(0x20, 0x20);
            return;
        }

        char key = shift ? kbd_us_shift[scancode] : kbd_us[scancode];

        if (key != 0) {
            shell_input(key);
        }
    }

    outb(0x20, 0x20);
}

int isKeyPressed(uint8_t scancode) {
    if (scancode < 128) return key_states[scancode];
    return 0;
}