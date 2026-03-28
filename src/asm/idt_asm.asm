[bits 32]

global idt_load
global irq0_handler
global irq1_handler
global syscall_handler
global sysenter_handler

extern keyboard_handler_main
extern scheduler_tick
extern syscall_dispatch

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

; ─────────────────────────────────────────────────────────────────────────────
; 4. 시스템 콜 핸들러 (int 0x80, DPL=3 → Ring 3에서 호출 가능)
;
;    Linux 호환 호출 규약:
;      EAX = syscall 번호
;      EBX = 인자 1
;      ECX = 인자 2
;      EDX = 인자 3
;    반환값: EAX
; ─────────────────────────────────────────────────────────────────────────────
syscall_handler:
    ; 커널 세그먼트 로드
    push ds
    push es
    push fs
    push gs

    mov cx, 0x10
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    ; 인자 전달: syscall_dispatch(eax, ebx, ecx, edx)
    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch
    add esp, 16         ; 인자 4개 정리

    ; EAX = 반환값 (syscall_dispatch의 return value)

    pop gs
    pop fs
    pop es
    pop ds

    iretd

; ─────────────────────────────────────────────────────────────────────────────
; 5. SYSENTER 핸들러 (Ring 3 → Ring 0 빠른 시스템 콜 진입)
;
;    sysenter 레지스터 규약 (유저 측에서 진입 전 설정):
;      EAX = 시스템 콜 번호
;      EBX = 인자 1, ECX = 인자 2, EDX = 인자 3
;      ESI = 복귀 EIP (sysexit 가 ECX 로 사용)
;      EBP = 복귀 ESP (sysexit 가 EDX 로 사용)
;
;    sysenter 실행 시 CPU 동작:
;      CS  ← SYSENTER_CS_MSR (0x08)
;      EIP ← SYSENTER_EIP_MSR (이 핸들러 주소)
;      SS  ← SYSENTER_CS_MSR + 8 (0x10)
;      ESP ← SYSENTER_ESP_MSR (커널 스택)
; ─────────────────────────────────────────────────────────────────────────────
sysenter_handler:
    ; 유저 복귀 정보를 커널 스택에 저장
    push ebp            ; 복귀 ESP (sysexit EDX 용)
    push esi            ; 복귀 EIP (sysexit ECX 용)

    ; 커널 데이터 세그먼트 로드
    push ds
    push es
    push fs
    push gs

    mov cx, 0x10
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    ; syscall_dispatch(eax, ebx, ecx, edx) 호출
    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch
    add esp, 16         ; 인자 4개 정리

    ; EAX = 반환값

    pop gs
    pop fs
    pop es
    pop ds

    ; sysexit 복귀: ECX = 유저 EIP, EDX = 유저 ESP
    pop ecx             ; 복귀 EIP
    pop edx             ; 복귀 ESP

    sysexit
