#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "mem/pmm.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/pci.h"
#include "drivers/ahci.h"
#include "std/malloc.h"
#include "shell/shell.h"
#include "mem/vmm.h"
#include "mem/mem.h"
#include "storge/ahci_adaptor.h"
#include "fs/fs.h"
#include "kernel/multitasking.h"

#define megaOf(x) ((x) * 1024 * 1024)

// ─── 유저 모드 데모 ──────────────────────────────────────────────────────────
// 위치 독립적(position-independent) i386 기계어 코드:
//   mov eax, 1       (SYS_WRITE)
//   mov ebx, msg_ptr (첫 번째 인자: 메시지 주소, 런타임에 패치)
//   mov ecx, 23      (두 번째 인자: 메시지 길이)
//   int 0x80
//   mov eax, 0       (SYS_EXIT)
//   xor ebx, ebx     (종료 코드 = 0)
//   int 0x80
//   jmp $            (만약 SYS_EXIT가 실패하면 무한 루프)
//
// 메시지는 코드 페이지 내 오프셋 32번지에 위치합니다.

// 유저 프로그램이 로드될 가상 주소
#define USER_CODE_VADDR  0x01000000U  // 16MB (identity map 바깥)
#define USER_STACK_VADDR 0x01001000U  // 16MB + 4KB (스택 페이지)
#define USER_ESP_INIT    (USER_STACK_VADDR + 0x1000U)  // 스택 최상단

// 메시지가 코드 페이지 내 오프셋 32에 위치하므로 런타임 주소는:
#define USER_MSG_VADDR   (USER_CODE_VADDR + 32U)

static void setup_user_mode_demo(void) {
    // 유저 코드 페이지와 스택 페이지 할당 (VMM_ALLOC_USER → PAGE_USER 플래그)
    vmm_result_t r1 = vmm_alloc_virtual_pages(USER_CODE_VADDR,  1, VMM_ALLOC_USER);
    vmm_result_t r2 = vmm_alloc_virtual_pages(USER_STACK_VADDR, 1, VMM_ALLOC_USER);

    if (r1 != VMM_SUCCESS || r2 != VMM_SUCCESS) {
        kprintf("[USER] Failed to allocate user pages (r1=%d, r2=%d)\n", r1, r2);
        return;
    }

    // 유저 코드 페이지를 0으로 초기화
    memset((void*)USER_CODE_VADDR, 0, 0x1000);

    // ── 기계어 작성 ──────────────────────────────────────────────────────────
    // 오프셋  0: mov eax, 1 (SYS_WRITE)
    // 오프셋  5: mov ebx, USER_MSG_VADDR
    // 오프셋 10: mov ecx, 23  (메시지 길이)
    // 오프셋 15: int 0x80
    // 오프셋 17: mov eax, 0 (SYS_EXIT)
    // 오프셋 22: xor ebx, ebx
    // 오프셋 24: int 0x80
    // 오프셋 26: jmp $ (무한 루프 안전망)
    // 오프셋 32: 메시지 문자열

    uint8_t* code = (uint8_t*)USER_CODE_VADDR;
    uint32_t msg_addr = USER_MSG_VADDR;

    // mov eax, 1  →  B8 01 00 00 00
    code[0] = 0xB8; code[1] = 0x01; code[2] = 0x00; code[3] = 0x00; code[4] = 0x00;
    // mov ebx, msg_addr  →  BB [4바이트 주소]
    code[5] = 0xBB;
    code[6]  = (uint8_t)(msg_addr);
    code[7]  = (uint8_t)(msg_addr >> 8);
    code[8]  = (uint8_t)(msg_addr >> 16);
    code[9]  = (uint8_t)(msg_addr >> 24);
    // mov ecx, 23  →  B9 17 00 00 00
    code[10] = 0xB9; code[11] = 23; code[12] = 0x00; code[13] = 0x00; code[14] = 0x00;
    // int 0x80  →  CD 80
    code[15] = 0xCD; code[16] = 0x80;
    // mov eax, 0  →  B8 00 00 00 00
    code[17] = 0xB8; code[18] = 0x00; code[19] = 0x00; code[20] = 0x00; code[21] = 0x00;
    // xor ebx, ebx  →  31 DB
    code[22] = 0x31; code[23] = 0xDB;
    // int 0x80  →  CD 80
    code[24] = 0xCD; code[25] = 0x80;
    // jmp $  →  EB FE
    code[26] = 0xEB; code[27] = 0xFE;

    // 오프셋 32: 메시지 (23바이트, null-terminated)
    const char *msg = "Hello from Ring 3!\n\0\0\0\0";
    uint8_t* msgptr = code + 32;
    for (int i = 0; i < 23; i++) msgptr[i] = (uint8_t)msg[i];

    // ── 유저 스레드 생성 ─────────────────────────────────────────────────────
    int tid = kcreate_user_thread("user_demo",
                                  (void (*)(void))USER_CODE_VADDR,
                                  USER_ESP_INIT);
    if (tid < 0) {
        kprintf("[USER] Failed to create user thread\n");
    } else {
        kprintf("[USER] User mode demo thread created (tid=%d)\n", tid);
        kprintf("[USER] Entry: 0x%x, Stack: 0x%x\n",
                USER_CODE_VADDR, USER_ESP_INIT);
    }
}

void kmain() {
    // 화면 초기화
    vga_clear();

    // === 1단계: 기본 시스템 초기화 ===
    init_gdt();   // GDT 초기화 (유저 세그먼트 + TSS 포함)
    init_idt();   // IDT 초기화 (int 0x80 시스템 콜 포함)

    // === 2단계: 메모리 관리 시스템 ===
    init_pmm();   // 물리 메모리 관리자
    init_vmm();   // 가상 메모리 관리자
    init_heap(0x800000, 10);  // 커널 힙

    // === 3단계: 하드웨어 드라이버 ===
    init_timer(1000);         // 타이머 (인터럽트 전에)

    // init_serial();            // 시리얼 통신 // 이미 로더에서 시리얼 포트를 초기화 했으므로 필요하지 않음

    // 인터럽트 활성화 (타이머와 시리얼 초기화 후)
    __asm__ __volatile__("sti");

    // === 4단계: PCI 및 저장장치 ===
    init_pci();               // PCI 버스 스캔
    init_ahci();              // AHCI 컨트롤러 초기화

    // === 5단계: 저장장치 추상화 스택 ===
    block_device_manager_init();   // Block Device Manager 초기화
    ahci_adapter_init();           // AHCI Adapter 초기화
    ahci_adapter_register_devices(); // AHCI 장치들을 Block Device로 등록

    init_fs();

    // === 6단계: 멀티태스킹 시스템 초기화 ===
    init_multitasking();

    // === 7단계: 유저 모드 데모 ===
    setup_user_mode_demo();

    // === 8단계: 사용자 인터페이스 ===
    init_keyboard();          // 키보드 드라이버
    shell_init();             // 셸 초기화

    while(1) {
        __asm__ __volatile__("hlt");
    }
}
