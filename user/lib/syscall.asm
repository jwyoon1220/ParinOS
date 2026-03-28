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
    mov  eax, [esp+4]       ; 시스템 콜 번호
    mov  esi, [esp]         ; 복귀 EIP 저장
    lea  ebp, [esp+4]       ; 복귀 ESP 저장 (ret 이후 스택 위치)
    sysenter
    ; sysexit 가 복귀 주소로 점프하므로 아래는 실행되지 않음
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall1(int num, int a1)
; ─────────────────────────────────────────────────────────────────────────────
syscall1:
    mov  eax, [esp+4]
    mov  ebx, [esp+8]
    mov  esi, [esp]
    lea  ebp, [esp+4]
    sysenter
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall2(int num, int a1, int a2)
; ─────────────────────────────────────────────────────────────────────────────
syscall2:
    mov  eax, [esp+4]
    mov  ebx, [esp+8]
    mov  ecx, [esp+12]
    mov  esi, [esp]
    lea  ebp, [esp+4]
    sysenter
    ret

; ─────────────────────────────────────────────────────────────────────────────
; int syscall3(int num, int a1, int a2, int a3)
; ─────────────────────────────────────────────────────────────────────────────
syscall3:
    mov  eax, [esp+4]
    mov  ebx, [esp+8]
    mov  ecx, [esp+12]
    mov  edx, [esp+16]
    mov  esi, [esp]
    lea  ebp, [esp+4]
    sysenter
    ret
