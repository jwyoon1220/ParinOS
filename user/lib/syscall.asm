; user/lib/syscall.asm — ParinOS 유저 라이브러리 시스템 콜 스텁 (sysenter 기반)
;
; 호출 규약 (cdecl):
;   인자:  [esp+4] = num, [esp+8] = a1, [esp+12] = a2, [esp+16] = a3
;   반환:  EAX
;
; sysenter 레지스터 규약:
;   EAX = 시스템 콜 번호
;   EBX = 인자 1, ECX = 인자 2, EDX = 인자 3
;   ESI = 복귀 EIP (sysexit 이 ECX 로 전달)
;   EBP = 복귀 ESP (sysexit 이 EDX 로 전달)

[bits 32]

; 스택이 실행 불가능함을 링커에 알립니다
section .note.GNU-stack noalloc noexec nowrite progbits

section .text

global syscall0
global syscall1
global syscall2
global syscall3

; ─────────────────────────────────────────────────────────────────────────────
; int syscall0(int num)
; ─────────────────────────────────────────────────────────────────────────────
syscall0:
    push ebp
    push esi
    push ebx
    mov  eax, [esp+16]
    mov  esi, .return
    mov  ebp, esp
    sysenter
.return:
    pop  ebx
    pop  esi
    pop  ebp
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall1(int num, int a1)
; ─────────────────────────────────────────────────────────────────────────────
syscall1:
    push ebp
    push esi
    push ebx
    mov  eax, [esp+16]
    mov  ebx, [esp+20]
    mov  esi, .return
    mov  ebp, esp
    sysenter
.return:
    pop  ebx
    pop  esi
    pop  ebp
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall2(int num, int a1, int a2)
; ─────────────────────────────────────────────────────────────────────────────
syscall2:
    push ebp
    push esi
    push ebx
    mov  eax, [esp+16]
    mov  ebx, [esp+20]
    mov  ecx, [esp+24]
    mov  esi, .return
    mov  ebp, esp
    sysenter
.return:
    pop  ebx
    pop  esi
    pop  ebp
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall3(int num, int a1, int a2, int a3)
; ─────────────────────────────────────────────────────────────────────────────
syscall3:
    push ebp
    push esi
    push ebx
    mov  eax, [esp+16]
    mov  ebx, [esp+20]
    mov  ecx, [esp+24]
    mov  edx, [esp+28]
    mov  esi, .return
    mov  ebp, esp
    sysenter
.return:
    pop  ebx
    pop  esi
    pop  ebp
    ret
