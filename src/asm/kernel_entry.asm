[bits 32]
extern kmain      ; C 언어의 kmain 함수를 가져옴
global _start

; 전용 커널 스택 (256KB) — loader의 부트 스택(0x9E000)은 작으므로
; TTF 렌더링 등 스택 소모가 큰 작업을 위해 충분한 크기 확보
section .bss
align 16
kernel_stack_bottom:
    resb 256 * 1024         ; 256KB
kernel_stack_top:

section .text
_start:
    mov esp, kernel_stack_top   ; 전용 커널 스택으로 교체
    xor ebp, ebp                ; 스택 프레임 체인 초기화
    call kmain                  ; kmain 호출
    jmp $                       ; 혹시 리턴되면 무한 루프
