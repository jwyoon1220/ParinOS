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
    mov  eax, [esp+4]
    mov  esi, [esp]
    lea  ebp, [esp+4]
    sysenter
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
