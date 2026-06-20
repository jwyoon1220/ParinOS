; sdk/lib/syscall.asm — ParinOS SDK: Syscall Stubs (sysenter-based)
;
; Calling convention (cdecl):
;   Arguments: [esp+4]=num, [esp+8]=a1, [esp+12]=a2, [esp+16]=a3
;   Return:    EAX
;
; Sysenter register convention:
;   EAX = syscall number
;   EBX = arg1,  ECX = arg2,  EDX = arg3
;   ESI = return EIP (passed back via sysexit in ECX)
;   EBP = return ESP (passed back via sysexit in EDX)

[bits 32]

; Tell the linker the stack is non-executable
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
