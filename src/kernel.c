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

/*
 * show_boot_time — KAIST NTP 서버에서 현재 시각을 가져와 VESA 화면에 표시합니다.
 *
 * 표시 위치: 화면 우측 상단 (가로 중앙 오른쪽, 세로 상단 패딩 20px)
 * 폰트: font_load_embedded_ttf() 로 로드된 TrueType (16px)
 * 형식: "2025-04-11  09:32:05 UTC"  (흰 글씨 / 반투명 어두운 배경 박스)
 *
 * VESA / 폰트가 준비되지 않았거나 NTP 응답이 없으면 조용히 건너뜁니다.
 */
static void show_boot_time(void) {
    if (!vesa_is_active() || !font_is_ready()) return;

    sntp_result_t t;
    kprintf_serial("[TIME] Querying KAIST NTP (time.kaist.ac.kr)...\n");
    if (sntp_query(&t) != 0) {
        kprintf_serial("[TIME] NTP query failed\n");
        return;
    }
    kprintf_serial("[TIME] %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                   t.year, t.month, t.day, t.hour, t.min, t.sec);

    /* ── 날짜/시간 문자열 조립 ─────────────────────────────────── */
    char date_str[32];  /* "2025-04-11" */
    char time_str[32];  /* "09:32:05 UTC" */

    /* ksnprintf 가 %02u 를 지원하지 않을 수 있으므로 직접 패딩 */
    ksnprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
              (int)t.year, (int)t.month, (int)t.day);
    ksnprintf(time_str, sizeof(time_str), "%02d:%02d:%02d UTC",
              (int)t.hour, (int)t.min, (int)t.sec);

    int fw = font_get_width();
    int fh = font_get_height();

    uint32_t sw = vesa_get_width();
    uint32_t sh = vesa_get_height();

    /* 날짜 문자열 길이 */
    int date_len = 0; { const char *p = date_str; while (*p++) date_len++; }
    int time_len = 0; { const char *p = time_str; while (*p++) time_len++; }

    /* 더 긴 줄을 기준으로 박스 너비 계산 */
    int max_len  = date_len > time_len ? date_len : time_len;
    int pad      = 12;                             /* 좌우 패딩 (px) */
    int box_w    = max_len * fw + pad * 2;
    int box_h    = fh * 2 + pad * 3;              /* 날짜 + 시각 두 줄 */

    /* 우측 상단 배치 (화면 오른쪽 끝에서 16px 여백) */
    int box_x = (int)sw - box_w - 16;
    int box_y = 16;
    if (box_x < 0) box_x = 0;

    /* 반투명 배경 박스 (어두운 남색) */
    vesa_fill_rect((uint32_t)box_x, (uint32_t)box_y,
                   (uint32_t)box_w, (uint32_t)box_h,
                   0x10, 0x18, 0x30);

    /* 날짜 줄 (밝은 하늘색) */
    int tx = box_x + pad;
    int ty = box_y + pad;
    for (int i = 0; i < date_len; i++) {
        font_draw_char(tx + i * fw, ty, (uint32_t)(unsigned char)date_str[i],
                       0xAD, 0xD8, 0xFF,   /* 전경: 하늘색 */
                       0x10, 0x18, 0x30);  /* 배경: 박스 색 */
    }

    /* 시각 줄 (밝은 흰색) */
    ty += fh + pad;
    for (int i = 0; i < time_len; i++) {
        font_draw_char(tx + i * fw, ty, (uint32_t)(unsigned char)time_str[i],
                       0xFF, 0xFF, 0xFF,   /* 전경: 흰색 */
                       0x10, 0x18, 0x30);  /* 배경: 박스 색 */
    }

    (void)sh;  /* 현재 미사용 */
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

    /* ── KAIST NTP 시간 동기화 + VESA 화면 표시 ───────────────────────────
     * font_load_embedded_ttf() 와 init_network() 가 모두 완료된 이후에
     * KAIST 시간 서버(time.kaist.ac.kr)에서 현재 UTC 시각을 가져와
     * 화면 우측 상단에 날짜/시각을 렌더링합니다.
     * ─────────────────────────────────────────────────────────────────── */
    show_boot_time();

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
