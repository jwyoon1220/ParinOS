/*
 * ParinOS 2단계 C 로더 (Stage 2 Loader)
 *
 * 디스크 레이아웃:
 *   LBA 0       : 1단계 부트로더 (boot.asm, 512바이트)
 *   LBA 1~128   : 2단계 C 로더 (loader.c, 64KB 영역)  ← 0x10000에 로드됨
 *   LBA 129~    : 실제 커널 (kernel.bin)               ← 0x1000000에 로드됨
 *
 * 역할:
 *   - ATA PIO 방식으로 LBA 129부터 커널을 읽어 0x1000000(16MB)에 적재
 *   - 커널 진입점 (0x1000000)으로 점프
 */

/* 디스크/메모리 레이아웃 상수 */
#define KERNEL_LBA_START    129         /* 커널 시작 LBA (로더 128섹터 이후) */
#define KERNEL_SECTORS      256         /* 읽을 커널 섹터 수 (256 * 512 = 128KB) */
#define KERNEL_LOAD_ADDR    0x1000000   /* 커널 로드 목적지: 16MB (0x1000000) */

/* ATA 포트 (Primary IDE, I/O 포트 0x1F0~0x1F7) */
#define ATA_DATA_PORT       0x1F0
#define ATA_SECTOR_COUNT    0x1F2
#define ATA_LBA_LOW         0x1F3
#define ATA_LBA_MID         0x1F4
#define ATA_LBA_HIGH        0x1F5
#define ATA_DRIVE_HEAD      0x1F6
#define ATA_CMD_STATUS      0x1F7

#define ATA_STATUS_BSY      0x80    /* Busy */
#define ATA_STATUS_DRQ      0x08    /* Data Request Ready */
#define ATA_CMD_READ        0x20    /* Read Sectors (LBA) */

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

/* ─── I/O 포트 헬퍼 ─── */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ─── VGA 텍스트 출력 (간단한 상태 표시용) ─── */
static uint16_t *const vga_buf = (uint16_t *)0xB8000;
static int vga_col = 0;
static int vga_row = 1;     /* 1단계 메시지 아래 줄부터 출력 */

static void vga_putchar(char c) {
    if (c == '\n') {
        vga_row++;
        vga_col = 0;
        return;
    }
    vga_buf[vga_row * 80 + vga_col] = (uint16_t)((0x0A << 8) | (uint8_t)c);
    vga_col++;
    if (vga_col >= 80) {
        vga_col = 0;
        vga_row++;
    }
}

static void vga_print(const char *s) {
    while (*s) vga_putchar(*s++);
}

/* ─── ATA PIO 단일 섹터 읽기 ─── */
static int ata_read_sector(uint32_t lba, uint8_t *buf) {
    uint32_t timeout;

    /* BSY 해제 대기 (타임아웃: ~500ms) */
    for (timeout = 100000; timeout > 0; timeout--) {
        if (!(inb(ATA_CMD_STATUS) & ATA_STATUS_BSY)) break;
    }
    if (timeout == 0) return -1;

    /* LBA 모드, 마스터 드라이브 선택 및 LBA 상위 4비트 설정 */
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba));
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_CMD_STATUS, ATA_CMD_READ);

    /* BSY 해제 대기 */
    for (timeout = 100000; timeout > 0; timeout--) {
        if (!(inb(ATA_CMD_STATUS) & ATA_STATUS_BSY)) break;
    }
    if (timeout == 0) return -1;

    /* DRQ 활성화 대기 */
    for (timeout = 100000; timeout > 0; timeout--) {
        if (inb(ATA_CMD_STATUS) & ATA_STATUS_DRQ) break;
    }
    if (timeout == 0) return -1;

    /* 256개의 16비트 워드 (= 512바이트) 읽기 */
    uint16_t *ptr = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_DATA_PORT);
    }
    return 0;
}

/* ─── 로더 메인 함수 ─── */
void loader_main(void) {
    vga_print("Stage2: Loading kernel to 0x1000000...");

    uint8_t *dest = (uint8_t *)KERNEL_LOAD_ADDR;

    for (int i = 0; i < KERNEL_SECTORS; i++) {
        if (ata_read_sector(KERNEL_LBA_START + i, dest + (uint32_t)i * 512U) != 0) {
            vga_print(" DISK ERROR!\n");
            for (;;) ; /* 시스템 정지 */
        }

        /* 진행 상태 점(.) 표시 (16섹터마다 한 번) */
        if ((i & 0x0F) == 0) {
            vga_putchar('.');
        }
    }

    vga_print(" OK\n");

    /* 커널 진입점 (0x1000000)으로 점프 */
    typedef void (*kernel_entry_t)(void);
    kernel_entry_t kernel_entry = (kernel_entry_t)KERNEL_LOAD_ADDR;
    kernel_entry();
}
