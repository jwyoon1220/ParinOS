[bits 16]
[org 0x7c00]

KERNEL_OFFSET equ 0x1000

_start:
    ; 1. 세그먼트 초기화 (가장 먼저 수행)
    cli                 ; 초기화 중 인터럽트 방지
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00      ; 스택 하단 설정
    sti                 ; 초기화 완료 후 인터럽트 재개

    mov [BOOT_DRIVE], dl

    ; 화면 초기화 (먼저 수행해서 깨끗하게 만듦)
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; 환영 메시지 출력 (동작 확인용)
    mov si, MSG_LOAD
    call print_string

    ; --- 2. BIOS E820 메모리 맵 수집 ---
    mov di, 0x8004
    xor ebx, ebx
    xor bp, bp
    mov edx, 0x534D4150

.mmap_loop:
    mov eax, 0xE820
    mov [es:di + 20], dword 1
    mov ecx, 24
    int 0x15
    jc .mmap_done

    mov edx, 0x534D4150
    cmp eax, edx
    jne .mmap_done

    test ebx, ebx
    je .mmap_last_entry

    add di, 24
    inc bp
    jmp .mmap_loop

.mmap_last_entry:
    inc bp

.mmap_done:
    mov [0x8000], bp

    ; --- 메모리 맵 수집 끝 ---

    ; --- 🌟 3. 디스크 읽기 ---
    mov bx, KERNEL_OFFSET
    mov ah, 0x02
    mov al, 50          ; 안전하게 50섹터(25KB)만 읽음
    mov ch, 0x00
    mov dh, 0x00
    mov cl, 0x02        ; 2번 섹터부터
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error       ; 에러 나면 메시지 띄우러 감

    ; 32비트 전환 준비
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp 0x08:init_pm

disk_error:
    mov si, DISK_ERR
    call print_string
    jmp $

; --- 16비트 문자열 출력 함수 ---
print_string:
    pusha
    mov ah, 0x0e
.loop:
    lodsb
    test al, al         ; cmp al, 0 보다 빠르고 효율적
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

[bits 32]
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp
    sti

    call KERNEL_OFFSET
    jmp $

; --- 데이터 영역 ---
align 4
gdt_start:
    dq 0x0
gdt_code: dw 0xffff, 0x0, 0x9a00, 0x00cf
gdt_data: dw 0xffff, 0x0, 0x9200, 0x00cf
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE db 0
MSG_LOAD   db "Booting ParinOS...", 13, 10, 0
DISK_ERR   db "Disk Error!", 13, 10, 0

times 510-($-$$) db 0
dw 0xaa55