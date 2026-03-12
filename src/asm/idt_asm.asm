[bits 32]

global idt_load
global irq0_handler
global irq1_handler
global syscall_stub

extern keyboard_handler_main
extern scheduler_tick
extern do_syscall

; 1. IDT 로드 함수 (안전한 버전)
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ; sti는 여기서 하지 말고, 커널 메인이나 IDT 초기화 마지막에 하는 것이 안전합니다.
    ret

; --- 인터럽트 공통 매크로 (코드 중복 방지 및 안전성 확보) ---
%macro ISR_COMMON 1
    pusha           ; 모든 일반 레지스터 저장
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10    ; 커널 데이터 세그먼트 강제 설정
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call %1         ; C 함수 호출

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd           ; 인터럽트 종료 및 리턴
%endmacro

; 2. 타이머 핸들러 (IRQ 0)
;    scheduler_tick(uint32_t current_esp) 를 호출하고,
;    반환값(eax)을 새 ESP 로 사용하여 컨텍스트 전환을 수행합니다.
global irq0_handler

irq0_handler:
    cli                 ; 인터럽트 비활성화

    push 0              ; Dummy error code
    push 32             ; IRQ0 (타이머) 인터럽트 번호

    pusha               ; eax, ecx, edx, ebx, esp, ebp, esi, edi 푸시

    mov ax, ds          ; 현재 Data Segment 저장
    push eax

    mov ax, 0x10        ; 커널 Data Segment (0x10) 로드
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; scheduler_tick(current_esp) 호출
    ; 인자: 현재 ESP (저장된 컨텍스트 프레임 시작 주소, DS 위치를 가리킴)
    ; 반환: 다음 스레드의 ESP (전환 없으면 동일한 값)
    push esp
    call scheduler_tick
    add esp, 4          ; 넘겨준 인자(esp) 정리

    ; eax = scheduler_tick 의 반환값:
    ;   - 컨텍스트 전환 없음 → 현재 스레드의 원래 ESP (push esp 로 전달한 값)
    ;   - 컨텍스트 전환 있음 → 다음 스레드의 저장된 ESP (다른 스레드의 컨텍스트 프레임 시작)
    mov esp, eax        ; ESP 교체 → 이 순간 다른 스레드의 스택으로 전환됨

    ; 컨텍스트 복원
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                ; 범용 레지스터 복구
    add esp, 8          ; int_no와 err_code 스택 정리
    sti                 ; 인터럽트 다시 활성화
    iret                ; 인터럽트 종료 및 복귀 (EIP, CS, EFLAGS 복원)

; 3. 키보드 핸들러 (IRQ 1)
irq1_handler:
    ISR_COMMON keyboard_handler_main

global isr14_handler
extern page_fault_handler

; 🌟 Page Fault (ISR 14) 핸들러
isr14_handler:
    ; Page Fault는 CPU가 자동으로 에러 코드를 스택에 하나 더 push 합니다.
    ; 따라서 일반 핸들러와 스택 구조가 조금 다릅니다.
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; C 함수 호출
    call page_fault_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa

    ; CPU가 push한 에러 코드를 스택에서 제거 (4바이트)
    add esp, 4
    iretd

; ─── int 0x80 시스템 콜 핸들러 ────────────────────────────────────────────────
; 유저 모드(Ring 3)에서 int 0x80 호출 시 진입
; CPU가 자동으로 커널 스택으로 전환 (TSS.esp0/ss0 사용)하고
; SS_user, ESP_user, EFLAGS, CS_user, EIP_user를 커널 스택에 push
;
; 호출 규약:
;   EAX = 시스템 콜 번호
;   EBX = arg1, ECX = arg2, EDX = arg3
;   반환값: EAX

syscall_stub:
    ; ── 레지스터 보존 (EAX는 반환값이므로 나중에 덮어씀) ──
    push ebp
    push esi
    push edi
    push ebx       ; arg1 저장
    push ecx       ; arg2 저장
    push edx       ; arg3 저장

    ; ── 커널 데이터 세그먼트로 전환 ──
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; ── do_syscall(syscall_num, arg1, arg2, arg3) 호출 ──
    ; EAX, EBX, ECX, EDX는 push 이후에도 레지스터 값이 그대로 유지됨
    push edx       ; arg3
    push ecx       ; arg2
    push ebx       ; arg1
    push eax       ; syscall_num
    call do_syscall
    add esp, 16    ; 인자 스택 정리

    ; EAX = do_syscall 반환값 (유저 프로그램으로 전달)

    ; ── 세그먼트 복원 ──
    pop gs
    pop fs
    pop es
    pop ds

    ; ── 보존했던 레지스터 복원 (스택 역순) ──
    pop edx
    pop ecx
    pop ebx
    pop edi
    pop esi
    pop ebp

    ; iretd: EIP, CS, EFLAGS, ESP_user, SS_user 복원 (Ring 3으로 복귀)
    iretd
