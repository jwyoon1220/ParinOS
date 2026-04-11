#include "hal/vga.h"
#include "hal/gdt.h"
#include "hal/idt.h"
#include "hal/keyboard.h"
#include "mem/pmm.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/pci.h"
#include "drivers/ahci.h"
#include "drivers/ne2000.h"
#include "std/malloc.h"
#include "mem/vmm.h"
#include "storge/ahci_adaptor.h"
#include "fs/fs.h"
#include "kernel/multitasking.h"
#include "kernel/fpu.h"
#include "kernel/kernel_status_manager.h"
#include "drivers/vesa.h"
#include "font/font.h"
#include "elf/elf.h"
#include "std/kstdio.h"
#include "net/lwip_port.h"
#include "net/sntp.h"

#define megaOf(x) ((x) * 1024 * 1024)

/* ── 네트워크 초기화 (NE2000 + lwIP) ───────────────────────────────────── */
static void init_network(void) {
    kprintf_serial("[NET] Initializing NE2000 NIC...\n");
    int ret = ne2000_init(lwip_rx_input);
    if (ret < 0) {
        kprintf_serial("[NET] NE2000 not found — networking disabled\n");
        return;
    }
    /*
     * 기본 네트워크 설정:
     *   IP  : 10.0.2.15   (QEMU user-mode networking DHCP 기본값)
     *   Mask: 255.255.255.0
     *   GW  : 10.0.2.2
     *
     * 향후 DHCP 클라이언트로 대체 가능합니다.
     */
    uint32_t my_ip = (10U<<24)|(0U<<16)|(2U<<8)|15U;  /* 10.0.2.15 */
    uint32_t mask  = (255U<<24)|(255U<<16)|(255U<<8)|0U;
    uint32_t gw    = (10U<<24)|(0U<<16)|(2U<<8)|2U;
    lwip_init(my_ip, mask, gw);
    kprintf_serial("[NET] Stack up — 10.0.2.15/24 gw 10.0.2.2\n");
}

/* 유저 셸 실행: 별도 스레드에서 Ring 3 로 /bin/shell 을 실행합니다. */
static void user_shell_thread(void) {
    const char *argv[] = { "/bin/shell", (const char*)0 };
    elf_execute_in_ring3("/bin/shell", 1, argv);
    /* elf_execute_in_ring3 는 noreturn — 절대 여기에 도달하지 않음 */
}

static void launch_shell(void) {
    int tid = kcreate_thread("shell", user_shell_thread, 32768);
    if (tid < 0) {
        kernel_panic("/bin/shell 스레드 생성 실패 — 유저 셸을 시작할 수 없습니다", (uint32_t)tid);
    }
}

void kmain() {
    /* ── 부동소수점 연산 유닛(FPU) 활성화 ──────────────────────────────── */
    fpu_init();

    /* ── VESA / 폰트 정보 사전 수집 (페이징 활성화 전) ──────────────────── */
    vesa_init();    /* VBE 모드 정보 읽기 (framebuffer 물리 주소 저장)  */
    font_init();    /* BIOS 8×8 폰트 포인터 읽기 (0x9100)               */

    // 화면 초기화
    vga_clear();

    kprintf_serial("[BOOT] ParinOS starting...\n");

    // === 1단계: 기본 시스템 초기화 ===
    init_gdt();   // GDT 초기화
    init_idt();   // IDT 초기화
    kprintf_serial("[BOOT] GDT/IDT OK\n");

    // === 2단계: 메모리 관리 시스템 ===
    init_pmm();   // 물리 메모리 관리자
    init_vmm();   // 가상 메모리 관리자
    init_heap(0x800000, 10);  // 커널 힙
    kprintf_serial("[BOOT] Memory OK\n");

    /* ── VMM 페이징 활성화 후 VESA 프레임버퍼 매핑 ──────────────────────── */
    vesa_map_fb();  /* 프레임버퍼를 가상 주소 공간에 identity 매핑       */
    if (vesa_is_active()) {
        vga_clear(); /* VESA 모드로 화면 클리어                          */
    }

    // === 3단계: 하드웨어 드라이버 ===
    init_timer(1000);         // 타이머 (인터럽트 전에)

    // 인터럽트 활성화 (타이머와 시리얼 초기화 후)
    __asm__ __volatile__("sti");
    kprintf_serial("[BOOT] Interrupts enabled\n");

    // === 4단계: PCI 및 저장장치 ===
    init_pci();               // PCI 버스 스캔
    init_ahci();              // AHCI 컨트롤러 초기화
    kprintf_serial("[BOOT] PCI/AHCI OK\n");

    // === 5단계: 저장장치 추상화 스택 ===
    block_device_manager_init();   // Block Device Manager 초기화
    ahci_adapter_init();           // AHCI Adapter 초기화
    ahci_adapter_register_devices(); // AHCI 장치들을 Block Device로 등록

    init_fs();
    kprintf_serial("[BOOT] FS OK\n");

    // 내장 폰트 파일 로드
    font_load_embedded_ttf(16);

    // === 6단계: 네트워크 초기화 ===
    init_network();

    // === 7단계: 멀티태스킹 시스템 초기화 ===
    init_multitasking();
    kprintf_serial("[BOOT] Multitasking OK\n");

    // === 8단계: 사용자 인터페이스 ===
    init_keyboard();          // 키보드 드라이버
    launch_shell();           // 유저 셸 실행 (/bin/shell, Ring 3)
    kprintf_serial("[BOOT] Shell launched\n");

    while(1) {
        /* 매 idle 루프마다 NIC 폴링 */
        lwip_poll();
        __asm__ __volatile__("hlt");
    }
}
