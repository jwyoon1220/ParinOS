[bits 16]
[org 0x7c00]

KERNEL_OFFSET equ 0x10000    ; 커널이 실행될 32비트 보호 모드 물리 주소

_start:
    ; 1. 세그먼트 및 스택 초기화
    cli                     ; 초기화 중 인터럽트 방지
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00          ; 스택을 부트로더 영역 아래로 설정
    mov [BOOT_DRIVE], dl    ; BIOS가 넘겨준 부팅 드라이브 번호 보관
    sti                     ; 초기화 완료 후 인터럽트 허용

    ; 2. 화면 초기화 (80x25 텍스트 모드)
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; 3. 환영 메시지 출력
    mov si, MSG_LOAD
    call print_string

    ; 4. E820 메모리 맵 수집 (PMM에서 사용)
    call collect_memory_map

    ; 5. 커널 로딩 (LBA 확장 읽기 방식)
    call load_kernel

    ; 6. 성공 메시지 출력
    mov si, MSG_OK
    call print_string

    ; 7. 32비트 보호 모드 전환 준비
    cli                     ; ⚠️ 중요: 보호 모드 진입 전 인터럽트 완전 금지
    lgdt [gdt_descriptor]   ; GDT 등록
    mov eax, cr0
    or eax, 1               ; PE(Protection Enable) 비트 켜기
    mov cr0, eax

    ; 8. 코드 세그먼트로 Far Jump (파이프라인 플러시)
    jmp 0x08:init_pm

; --- [함수: E820 메모리 맵 수집] ---
collect_memory_map:
    mov di, 0x8004          ; 0x8004부터 엔트리 저장
    xor ebx, ebx
    xor bp, bp              ; 엔트리 개수 카운터
    mov edx, 0x534D4150     ; 'SMAP' 매직 넘버

.mmap_loop:
    mov eax, 0xE820
    mov [es:di + 20], dword 1 ; ACPI 3.x 확장 속성
    mov ecx, 24
    int 0x15
    jc .mmap_done           ; 에러 시 수집 종료

    cmp eax, 0x534D4150     ; 'SMAP' 확인
    jne .mmap_done

    test ebx, ebx           ; ebx가 0이면 마지막 엔트리
    je .mmap_last

    add di, 24              ; 다음 엔트리 저장 위치로 이동
    inc bp
    jmp .mmap_loop

.mmap_last:
    inc bp

.mmap_done:
    mov [0x8000], bp        ; 0x8000번지에 총 메모리 맵 개수 저장
    ret

; --- [함수: 커널 로딩 (LBA 확장 방식, 64KB 장벽 돌파)] ---
load_kernel:
    mov [SECTORS_READ], word 0

    ; 대상 물리 주소 0x10000을 세그먼트:오프셋 (0x1000:0x0000)으로 표현
    mov word [DAP_BUFFER_SEGMENT], 0x1000   ; <-- 0x0100 을 0x1000 으로 수정!
    mov word [DAP_BUFFER_OFFSET], 0x0000

.read_loop:
    cmp [SECTORS_READ], word 200    ; 200섹터(100KB) 읽기 (커널이 76KB이므로 넉넉함)
    jge .success

    ; LBA 확장 읽기 서비스 (AH=0x42) 호출
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    mov si, DAP                     ; DAP(Disk Address Packet) 구조체 주소 지정
    int 0x13
    jc disk_error                   ; 에러 시 무한 대기

    ; --- 다음 1섹터(512바이트)를 읽기 위한 포인터 갱신 ---

    ; 1. 읽어올 디스크의 LBA 번호 1 증가
    add dword [DAP_START_LBA], 1

    ; 2. 메모리 세그먼트를 0x20 증가 (0x20 * 16 = 512 바이트)
    ; (BX 오프셋을 계속 더하면 64KB에서 오버플로우가 나므로 세그먼트를 올림)
    add word [DAP_BUFFER_SEGMENT], 0x20

    inc word [SECTORS_READ]

    ; 진행 상황을 화면에 점(.)으로 표시
    push ax
    mov ah, 0x0E
    mov al, '.'
    int 0x10
    pop ax

    jmp .read_loop

.success:
    clc
    ret

; --- [함수: 16비트 문자열 출력] ---
print_string:
    mov ah, 0x0e
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

; --- [에러 처리] ---
disk_error:
    mov si, DISK_ERR
    call print_string
    jmp $                   ; 시스템 정지 (무한 루프)

; --- [32비트 보호 모드 진입점] ---
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

    ; 안전하게 레지스터를 통한 점프로 변경
    mov eax, KERNEL_OFFSET
    call eax                ; 0x10000 번지로 점프
    jmp $

; --- [데이터 및 GDT 영역] ---
align 4
gdt_start:
    dq 0x0                  ; 널 디스크립터
gdt_code:
    dw 0xffff, 0x0, 0x9a00, 0x00cf ; 코드 세그먼트 (0x08)
gdt_data:
    dw 0xffff, 0x0, 0x9200, 0x00cf ; 데이터 세그먼트 (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE    db 0
SECTORS_READ  dw 0

; LBA 확장 읽기(AH=0x42)를 위한 DAP(Disk Address Packet) 구조체
align 4
DAP:
    db 0x10                 ; 구조체 크기 (16 바이트)
    db 0                    ; 항상 0 (사용 안 함)
    dw 1                    ; 한 번에 읽을 섹터 수 (안전하게 1개씩)
DAP_BUFFER_OFFSET:
    dw 0x0000               ; 메모리 오프셋 (항상 0)
DAP_BUFFER_SEGMENT:
    dw 0x0100               ; 초기 메모리 세그먼트 (0x0100:0x0000 -> 0x1000)
DAP_START_LBA:
    dd 1                    ; 시작 LBA (0번은 부트로더, 1번이 커널의 시작)
    dd 0                    ; LBA 상위 32비트 (2TB 이상일 때 쓰지만 우린 안 씀)

MSG_LOAD      db "Booting ParinOS...", 0
MSG_OK        db " OK", 13, 10, 0
DISK_ERR      db 13, 10, "Disk Error! (AH=0x42)", 0

; MBR 부트 섹터 서명
times 510-($-$$) db 0
dw 0xaa55