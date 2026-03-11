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
#include "storge/ahci_adaptor.h"
#include "fs/fs.h"

#define megaOf(x) ((x) * 1024 * 1024)

void kmain() {
    // 화면 초기화
    vga_clear();

    // === 1단계: 기본 시스템 초기화 ===
    init_gdt();   // GDT 초기화
    init_idt();   // IDT 초기화

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

    // === 6단계: 사용자 인터페이스 ===
    init_keyboard();          // 키보드 드라이버
    shell_init();             // 셸 초기화


    // === 메인 루프 ===
    while(1) {
        __asm__("hlt");       // CPU 대기 상태 (전력 소모 감소)
    }
}