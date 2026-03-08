[bits 32]
extern kmain      ; C 언어의 kmain 함수를 가져옴
global _start

_start:
    call kmain    ; kmain 호출
    jmp $         ; 혹시 리턴되면 무한 루프