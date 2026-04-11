[bits 16]
[org 0x7c00]

; 2단계 로더가 로드될 물리 주소 (0x10000)
LOADER_OFFSET   equ 0x10000
; 2단계 로더 크기: 128섹터 = 64KB (LBA 1~128)
; 커널은 그 다음인 LBA 129부터 시작
LOADER_SECTORS  equ 128

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

    ; 5. 2단계 C 로더를 0x10000에 로딩 (LBA 확장 읽기 방식)
    call load_loader

    ; 6. 성공 메시지 출력
    mov si, MSG_OK
    call print_string

    ; [추가] BIOS 8×8 폰트 포인터를 0x9100 에 저장
    call get_bios_font

    ; [추가] VESA 그래픽 모드 설정 (모드 정보를 0x9000 에 저장)
    call setup_vesa

    ; 7. A20 게이트 활성화 (1MB 이상 메모리 접근 허용)
    call enable_a20

    ; 8. 32비트 보호 모드 전환 준비
    cli                     ; ⚠️ 중요: 보호 모드 진입 전 인터럽트 완전 금지
    lgdt [gdt_descriptor]   ; GDT 등록
    mov eax, cr0
    or eax, 1               ; PE(Protection Enable) 비트 켜기
    mov cr0, eax

    ; 9. 코드 세그먼트로 Far Jump (파이프라인 플러시)
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

; --- [함수: 2단계 C 로더 로딩 (LBA 1부터 128섹터 = 64KB)] ---
load_loader:
    mov [SECTORS_READ], word 0

    ; 대상 물리 주소 0x10000을 세그먼트:오프셋 (0x1000:0x0000)으로 표현
    mov word [DAP_BUFFER_SEGMENT], 0x1000
    mov word [DAP_BUFFER_OFFSET], 0x0000
    mov dword [DAP_START_LBA], 1        ; LBA 1부터 로더 시작

.read_loop:
    cmp [SECTORS_READ], word LOADER_SECTORS
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

; --- [함수: A20 게이트 활성화 (Fast A20 방식)] ---
enable_a20:
    in al, 0x92
    or al, 0x02             ; A20 비트 활성화
    and al, 0xFE            ; 시스템 리셋 방지 (비트 0 = 0)
    out 0x92, al
    ret

; --- [함수: BIOS 8×8 폰트 포인터 저장 → 0x9100] ---
; INT 10h/1130h/BH=6 : ES:BP → 8×8 폰트 데이터
; BP/ES 를 보존할 필요 없음 (이후 바로 보호 모드 진입)
get_bios_font:
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    mov word [0x9100], bp
    mov word [0x9102], es
    ret

; --- [함수: VESA 모드 설정 → 모드 정보 0x9000 저장] ---
; 0x0118 (1024×768×24bpp) 먼저 시도, 실패 시 0x0115 (800×600×24bpp)
; ES/DI 는 보존 불필요 (이후 보호 모드 진입)
setup_vesa:
    xor ax, ax
    mov es, ax
    mov di, 0x9000
    ; 시도 1: 1024×768×24bpp
    mov ax, 0x4F01
    mov cx, 0x0118
    int 0x10
    cmp ax, 0x004F
    jne .try2
    test word [0x9000], 0x0001
    jz .try2
    mov ax, 0x4F02
    mov bx, 0x4118          ; mode | 0x4000 (LFB)
    int 0x10
    cmp ax, 0x004F
    je .ok
.try2:
    ; 시도 2: 800×600×24bpp (4F01h 가 0x9000 덮어씀)
    mov di, 0x9000
    mov ax, 0x4F01
    mov cx, 0x0115
    int 0x10
    cmp ax, 0x004F
    jne .fail
    test word [0x9000], 0x0001
    jz .fail
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    je .ok
.fail:
    mov word [0x9028], 0    ; framebuffer = 0 → VESA 불가 신호
    mov word [0x902A], 0
.ok:
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

; --- [32비트 보호 모드 진입점 → 2단계 C 로더로 점프] ---
[bits 32]
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebp, 0x9E000        ; 부트 스택 628KB (0x9E000 - 0x1000)
    mov esp, ebp

    ; 2단계 C 로더 (0x10000)로 점프
    mov eax, LOADER_OFFSET
    call eax
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
    dw 0x1000               ; 초기 메모리 세그먼트 (0x1000:0x0000 -> 0x10000)
DAP_START_LBA:
    dd 1                    ; 시작 LBA (0번은 부트로더, 1번이 로더의 시작)
    dd 0                    ; LBA 상위 32비트 (2TB 이상일 때 쓰지만 우린 안 씀)

MSG_LOAD      db "ParinOS Stage1: Loading...", 0
MSG_OK        db " OK", 13, 10, 0
DISK_ERR      db 13, 10, "Disk Error! (AH=0x42)", 0

; MBR 부트 섹터 서명
times 510-($-$$) db 0
dw 0xaa55